/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * eMMC 块层测试（ztest 原生框架）。
 *
 * 被测硬件：板载 eMMC（非外插卡），7.28 GiB，8-bit，sdhci1@40049000，零外部接线。
 *
 * 被测驱动：zephyr/drivers/sdhc/sdhci-of-linkedsemi.c（linkedsemi,sdhci）
 *
 * 测什么（块层，不是文件系统层）：
 *   - 六个 API 入口的 `is_app_cpu_running()` 早退守卫（:189-194 等）与实际
 *     寄存器态的一致性 —— 该守卫先前被记为"唯一能让整个 eMMC 通路静默失效"
 *     的开关；2026-09-18 实测证伪（见下方"守卫实测态"说明）：写 2 后回读 = 0
 *     ⇒ 守卫在本平台不生效、eMMC 通路可用。用例仍固定"驱动行为 == 寄存器态"
 *     这一不变量，与守卫是否生效无关
 *   - `get_host_props` 的能力字与 DT 一致性；`host_caps.hs400_support == false`
 *     使 `linkedsemi_sdhci_execute_hs200_tuning()` 的 HS400 分支（:807-815
 *     两个 `while(1);`）在编译期外**不可达**
 *   - 块读写通路（`disk_access_*` → `mmc_read_blocks`/`mmc_write_blocks`
 *     → `sdhc_request`）：单块 / 多块、4 字节对齐 vs 非对齐缓冲
 *   - `set_io` 的时钟越界分支（:228-232 静默忽略）—— 缺陷钉住
 *   - `request` 的 data-opcode 白名单分支（:442-447）—— 非法 opcode 现在在
 *     取锁**之前**就被拒（修复：旧写法在持锁状态下 `return -ENOTSUP` 会泄漏
 *     `access_mutex`）。本工程用**探针线程内连发两次请求 + 有限超时 join**
 *     做自包含的非挂死判定，防止该缺陷回归（不依赖用例执行顺序）
 *
 * 守卫实测态（本工程的核心前提，务必先读）：
 *   `is_app_cpu_running()` 的定义是 `SYSC_SEC_CPU->APP_CPU_SRST > 0`
 *   （platform.h:180-183）。曾按 SDK 注释推断 APP_CPU_SRST 是 W1S 语义寄存器
 *   （写 2 = 保持 cpu2 复位、写 1 = 释放、写 0 无效）—— 即**"保持复位"也会让
 *   该函数返回 true** —— 又因 810 harness 的单核加载流程写 2，故曾断言本驱动
 *   全部 API 返回 -EBUSY、eMMC 通路不可用。
 *   **该推断已被实测证伪**（2026-09-18 硬件验证）：写 2 后**回读 = 0x0**（GDB
 *   与固件自读两路一致，见 KNOWN_ISSUES #75 更正批注）⇒ `is_app_cpu_running()`
 *   为假 ⇒ 守卫在本平台**不生效**，eMMC 通路**可用**（重建后 SKIP 计数 = 0；
 *   历史归档三份报告均为 FAIL(8/10)、10 项全部有结果、零 SKIP）。因此：
 *     · `guard_matches_driver_behavior` 断言"驱动行为 == 寄存器态"——该不变量
 *       与守卫是否生效无关，两种寄存器态下都必须成立；
 *     · `EMMC_SKIP_IF_GUARDED` 保留：若某加载流程真让守卫生效，其余用例应 SKIP
 *       而非 FAIL（判据与原因仍打进 INFO 与 COVERAGE.md）；当前平台走不生效分支。
 *
 * 测试函数为纯 ztest：zassert_* 原生断言；数值上报用 ZT_INFO 约定行
 * （SPEC_REFERENCE §6.5.4，断言之前）。结果记账由 common/ztest_glue 自动完成。
 *
 * 执行顺序（ztest 套件/用例均按链接序 = 符号名字典序）：
 *   emmc_app_cpu_guard → emmc_block_data → zz_emmc_block_edge
 *   `zz_emmc_block_edge` 仍排在最后：`set_io` 时钟边界会直接改控制器寄存器；
 *   mutex 泄漏探针在**修复后的驱动**上不再让互斥量永久不可用（探针已自包含），
 *   但仍按"最后执行"放置 —— 一旦该缺陷回归，不会污染同一套件内的其它用例。
 */

#include <string.h>
#include <errno.h>
#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sd/sd_spec.h>
#include <zephyr/logging/log.h>

/* 裸读 APP_CPU_SRST（与驱动 is_app_cpu_running() 同一判据） */
#include <reg_sysc_sec_cpu.h>

#include "emmc_block_test.h"
#include "ztest_glue.h"

LOG_MODULE_DECLARE(emmc_block_test, LOG_LEVEL_INF);

static const struct device *const sdhci_dev = DEVICE_DT_GET(EMMC_SDHCI_NODE);

/* 报告/数据缓冲：放 .bss（不占 ztest 线程栈），4 字节对齐 */
static uint8_t buf_a[2048] __aligned(4);
static uint8_t buf_b[2048] __aligned(4);
static uint8_t buf_w[2048] __aligned(4);

/* ========== 守卫探测 + 卡片就绪（幂等） ========== */

static inline uint32_t app_cpu_srst(void)
{
	return SYSC_SEC_CPU->APP_CPU_SRST;
}

static inline bool emmc_guard_active(void)
{
	return app_cpu_srst() > 0U;
}

/* 守卫若生效则跳过本用例（不判 FAIL），并把判据打进 INFO 供报告"实际"列使用 */
#define EMMC_SKIP_IF_GUARDED(tag)                                              \
	do {                                                                   \
		if (emmc_guard_active()) {                                     \
			ZT_INFO(tag, "skipped,app_cpu_srst=0x%x",              \
				(unsigned int)app_cpu_srst());                 \
			ztest_test_skip();                                     \
		}                                                              \
	} while (0)

/*
 * 幂等地把 eMMC 盘拉起来（disk_access_init 内部有 refcnt，重复调用安全）。
 * 结果缓存，避免后续用例在 mutex 泄漏探针之后再触发一次真实初始化。
 */
static int ensure_card(void)
{
	static int cached;
	static bool probed;

	if (!probed) {
		cached = disk_access_init(EMMC_DISK_NAME);
		probed = true;
	}

	return cached;
}

static void fill_pattern(uint8_t *b, size_t n, uint8_t seed)
{
	for (size_t i = 0; i < n; i++) {
		b[i] = (uint8_t)(seed + i * 13U + (i >> 2));
	}
}

static int count_diff(const uint8_t *a, const uint8_t *b, size_t n)
{
	int diff = 0;

	for (size_t i = 0; i < n; i++) {
		if (a[i] != b[i]) {
			diff++;
		}
	}

	return diff;
}

/* =====================================================================
 * 套件 1：emmc_app_cpu_guard —— 早退守卫与寄存器态的一致性
 * ===================================================================== */

ZTEST(emmc_app_cpu_guard, guard_matches_driver_behavior)
{
	struct sdhc_host_props props;
	uint32_t srst = app_cpu_srst();
	int ret = sdhc_get_host_props(sdhci_dev, &props);
	int present = sdhc_card_present(sdhci_dev);

	ZT_INFO("guard_matches_driver_behavior",
		"app_cpu_srst=0x%x,get_host_props=%d,card_present=%d", srst, ret,
		present);

	if (srst > 0U) {
		/* 守卫生效：驱动必须一致地返回 -EBUSY */
		zassert_equal(ret, -EBUSY,
			      "app_cpu_srst=0x%x>0 -> get_host_props should be -EBUSY, got %d",
			      srst, ret);
		zassert_equal(present, -EBUSY,
			      "app_cpu_srst=0x%x>0 -> card_present should be -EBUSY, got %d",
			      srst, present);
	} else {
		/* 守卫不生效：驱动必须正常工作 */
		zassert_ok(ret, "app_cpu_srst==0 -> get_host_props should succeed, got %d",
			   ret);
		zassert_true(present >= 0,
			     "app_cpu_srst==0 -> card_present should succeed, got %d",
			     present);
	}
}

ZTEST(emmc_app_cpu_guard, host_props_match_devicetree)
{
	struct sdhc_host_props props;

	EMMC_SKIP_IF_GUARDED("host_props_match_devicetree");

	zassert_ok(sdhc_get_host_props(sdhci_dev, &props),
		   "sdhc_get_host_props failed");

	ZT_INFO("host_props_match_devicetree",
		"f_max=%u,f_min=%u,power_delay=%u,hs200=%d,hs400=%d,bus8=%d,bus4=%d,is_spi=%d",
		props.f_max, props.f_min, props.power_delay,
		(int)props.host_caps.hs200_support, (int)props.host_caps.hs400_support,
		(int)props.host_caps.bus_8_bit_support, (int)props.host_caps.bus_4_bit_support,
		(int)props.is_spi);

	zassert_equal(props.f_max, DT_PROP(EMMC_SDHCI_NODE, max_bus_freq),
		      "f_max: got %u want DT max-bus-freq %u", props.f_max,
		      (unsigned int)DT_PROP(EMMC_SDHCI_NODE, max_bus_freq));
	zassert_equal(props.f_min, DT_PROP(EMMC_SDHCI_NODE, min_bus_freq),
		      "f_min: got %u want DT min-bus-freq %u", props.f_min,
		      (unsigned int)DT_PROP(EMMC_SDHCI_NODE, min_bus_freq));
	zassert_equal(props.power_delay, 500, "power_delay: got %u want 500",
		      props.power_delay);
	zassert_true(props.host_caps.hs200_support, "hs200_support should be true");
	zassert_true(props.host_caps.bus_8_bit_support,
		     "bus_8_bit_support should be true（板载 eMMC 8-bit）");
	zassert_false(props.is_spi, "is_spi should be false（native MMC 模式）");

	/* 缺陷/可达性钉住：驱动把 hs400_support 置 false（:206），
	 * 因此 linkedsemi_sdhci_execute_hs200_tuning() 里 `SDHC_TIMING_HS400`
	 * 与 else 两个 `while(1);` 分支（:807-815）在本板**不可达**。
	 * 本断言固定该前提；HS400 死循环本身**未覆盖**（走到即挂死，无确定性
	 * 判定手段 —— 见 COVERAGE.md「跳过 / 未覆盖」）。 */
	zassert_false(props.host_caps.hs400_support,
		      "hs400_support must stay false: 该值为 true 会让 HS400 分支"
		      "（sdhci-of-linkedsemi.c:807-815 的 while(1);）变成可达路径");
}

/* =====================================================================
 * 套件 2：emmc_block_data —— 块读写通路（disk_access 层）
 * ===================================================================== */

ZTEST_SUITE(emmc_block_data, NULL, NULL, NULL, NULL, NULL);

ZTEST(emmc_block_data, disk_present_and_init)
{
	int ret;

	EMMC_SKIP_IF_GUARDED("disk_present_and_init");

	ret = ensure_card();
	ZT_INFO("disk_present_and_init", "disk_access_init=%d,status=%d,dev=%s", ret,
		disk_access_status(EMMC_DISK_NAME), EMMC_DISK_NAME);

	zassert_ok(ret, "disk_access_init(\"%s\") failed: %d", EMMC_DISK_NAME, ret);
	zassert_equal(disk_access_status(EMMC_DISK_NAME), DISK_STATUS_OK,
		      "disk status should be DISK_STATUS_OK, got %d",
		      disk_access_status(EMMC_DISK_NAME));
}

ZTEST(emmc_block_data, disk_ioctl_sector_count)
{
	uint32_t count = 0;
	int32_t c;
	int ret;

	EMMC_SKIP_IF_GUARDED("disk_ioctl_sector_count");
	zassert_ok(ensure_card(), "card not usable");

	ret = disk_access_ioctl(EMMC_DISK_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &count);
	ZT_INFO("disk_ioctl_sector_count", "ret=%d,sectors=%u,bytes=%llu", ret, count,
		(unsigned long long)count * EMMC_SECTOR_SIZE);

	zassert_ok(ret, "GET_SECTOR_COUNT failed: %d", ret);
	c = (int32_t)count;
	zassert_within(c, (int32_t)EMMC_EXPECT_SECTORS, (int32_t)EMMC_SECTORS_TOLERANCE,
		       "sector count %d not within 2%% of expected %u (roadmap §5.1)",
		       c, EMMC_EXPECT_SECTORS);
}

ZTEST(emmc_block_data, disk_ioctl_sector_size)
{
	uint32_t size = 0;
	int ret;

	EMMC_SKIP_IF_GUARDED("disk_ioctl_sector_size");
	zassert_ok(ensure_card(), "card not usable");

	ret = disk_access_ioctl(EMMC_DISK_NAME, DISK_IOCTL_GET_SECTOR_SIZE, &size);
	ZT_INFO("disk_ioctl_sector_size", "ret=%d,size=%u", ret, size);

	zassert_ok(ret, "GET_SECTOR_SIZE failed: %d", ret);
	zassert_equal(size, EMMC_SECTOR_SIZE, "sector size: got %u want %u", size,
		      EMMC_SECTOR_SIZE);
}

ZTEST(emmc_block_data, read_aligned_matches_unaligned)
{
	/* 数据通路对齐象限：两块缓冲读同一扇区必须逐字节一致。
	 * buf_a 4 字节对齐；buf_b+1 故意非 4 字节对齐（SDHC_BUFFER_ALIGNMENT
	 * 在本 SoC 取默认值 1，即驱动不要求对齐）。非破坏性（读扇区 0）。 */
	int ret_a;
	int ret_b;

	EMMC_SKIP_IF_GUARDED("read_aligned_matches_unaligned");
	zassert_ok(ensure_card(), "card not usable");

	memset(buf_a, 0, EMMC_SECTOR_SIZE);
	memset(buf_b, 0, sizeof(buf_b));

	ret_a = disk_access_read(EMMC_DISK_NAME, buf_a, 0, 1);
	ret_b = disk_access_read(EMMC_DISK_NAME, buf_b + 1, 0, 1);

	ZT_INFO("read_aligned_matches_unaligned", "aligned=%d,unaligned=%d,diff=%d",
		ret_a, ret_b, count_diff(buf_a, buf_b + 1, EMMC_SECTOR_SIZE));

	zassert_ok(ret_a, "aligned read failed: %d", ret_a);
	zassert_ok(ret_b, "unaligned (buf+1) read failed: %d", ret_b);
	zassert_mem_equal(buf_a, buf_b + 1, EMMC_SECTOR_SIZE,
			  "aligned vs unaligned read mismatch (%d bytes differ)",
			  count_diff(buf_a, buf_b + 1, EMMC_SECTOR_SIZE));
}

ZTEST(emmc_block_data, read_unknown_drive_rejected)
{
	/* 异常参数：不存在的盘名（disk_access_get_di 返回 NULL → -EINVAL） */
	int ret = disk_access_read("NO_SUCH_DISK", buf_a, 0, 1);

	zassert_equal(ret, -EINVAL, "unknown drive should be -EINVAL, got %d", ret);
}

ZTEST(emmc_block_data, write_read_scratch_single)
{
	/* 破坏性（用户裁定允许格式化整片）：单个 512 B 块写后回读比对。
	 * scratch 扇区取 8 MiB 偏移处，避开 MBR / FAT / 盘尾。 */
	int ret;

	EMMC_SKIP_IF_GUARDED("write_read_scratch_single");
	zassert_ok(ensure_card(), "card not usable");

	fill_pattern(buf_w, EMMC_SECTOR_SIZE, 0x3c);
	memset(buf_a, 0, EMMC_SECTOR_SIZE);

	ret = disk_access_write(EMMC_DISK_NAME, buf_w, EMMC_SCRATCH_SECTOR, 1);
	zassert_ok(ret, "single-block write @%u failed: %d", EMMC_SCRATCH_SECTOR, ret);

	ret = disk_access_read(EMMC_DISK_NAME, buf_a, EMMC_SCRATCH_SECTOR, 1);
	zassert_ok(ret, "single-block read @%u failed: %d", EMMC_SCRATCH_SECTOR, ret);

	ZT_INFO("write_read_scratch_single", "sector=%u,diff=%d", EMMC_SCRATCH_SECTOR,
		count_diff(buf_a, buf_w, EMMC_SECTOR_SIZE));
	zassert_mem_equal(buf_a, buf_w, EMMC_SECTOR_SIZE,
			  "single-block write/read mismatch (%d bytes differ)",
			  count_diff(buf_a, buf_w, EMMC_SECTOR_SIZE));
}

ZTEST(emmc_block_data, write_read_scratch_multiblock)
{
	/* 多块通路（CMD18/CMD25 + auto CMD12）：4 块 = 2048 B。
	 * 覆盖 sdhci_data.enable_auto_command12 的读写两个分支（:454-466）。 */
	const uint32_t nblocks = 4;
	const uint32_t nbytes = nblocks * EMMC_SECTOR_SIZE;
	int ret;

	EMMC_SKIP_IF_GUARDED("write_read_scratch_multiblock");
	zassert_ok(ensure_card(), "card not usable");

	fill_pattern(buf_w, nbytes, 0x77);
	memset(buf_a, 0, nbytes);

	ret = disk_access_write(EMMC_DISK_NAME, buf_w, EMMC_SCRATCH_SECTOR, nblocks);
	zassert_ok(ret, "%u-block write @%u failed: %d", nblocks, EMMC_SCRATCH_SECTOR,
		   ret);

	ret = disk_access_read(EMMC_DISK_NAME, buf_a, EMMC_SCRATCH_SECTOR, nblocks);
	zassert_ok(ret, "%u-block read @%u failed: %d", nblocks, EMMC_SCRATCH_SECTOR,
		   ret);

	ZT_INFO("write_read_scratch_multiblock", "blocks=%u,bytes=%u,diff=%d", nblocks,
		nbytes, count_diff(buf_a, buf_w, nbytes));
	zassert_mem_equal(buf_a, buf_w, nbytes,
			  "multi-block write/read mismatch (%d bytes differ)",
			  count_diff(buf_a, buf_w, nbytes));
}

/* =====================================================================
 * 套件 3：zz_emmc_block_edge —— 驱动缺陷靶点（必须最后执行）
 *           名义排序 zz_* 保证本套件排在最后：mutex 泄漏探针一旦命中，
 *           access_mutex 永久不可用，任何后续 sdhc_request 都会挂住。
 * ===================================================================== */

ZTEST_SUITE(zz_emmc_block_edge, NULL, NULL, NULL, NULL, NULL);

ZTEST(zz_emmc_block_edge, request_data_invalid_opcode_notsup)
{
	/* 驱动缺陷靶点①：data != NULL 且 opcode 不在白名单 → default 分支
	 * `return -ENOTSUP`（:475-478），**在持锁状态下返回** → 泄漏 access_mutex。 */
	struct sdhc_command cmd = { 0 };
	struct sdhc_data data = { 0 };
	int ret;

	EMMC_SKIP_IF_GUARDED("request_data_invalid_opcode_notsup");
	zassert_ok(ensure_card(), "card not usable");

	cmd.opcode = EMMC_INVALID_DATA_OPCODE;
	cmd.arg = 0;
	cmd.response_type = SD_RSP_TYPE_R1;
	cmd.timeout_ms = 1000;
	data.block_size = EMMC_SECTOR_SIZE;
	data.blocks = 1;
	data.data = buf_a;
	data.timeout_ms = 1000;

	ret = sdhc_request(sdhci_dev, &cmd, &data);

	ZT_INFO("request_data_invalid_opcode_notsup", "opcode=%u,ret=%d",
		EMMC_INVALID_DATA_OPCODE, ret);
	zassert_equal(ret, -ENOTSUP,
		      "opcode %u with data != NULL should be -ENOTSUP, got %d",
		      EMMC_INVALID_DATA_OPCODE, ret);
}

static K_THREAD_STACK_DEFINE(mutex_probe_stack, 2048);
static struct k_thread mutex_probe_thread;
static int mutex_probe_ret;    /* 第一次请求的返回值 */
static int mutex_probe_ret2;   /* 第二次请求的返回值（缺陷存在时永远到不了这里） */
static bool mutex_probe_done;

static void mutex_probe_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct sdhc_command cmd = { 0 };
	struct sdhc_data data = { 0 };

	cmd.opcode = EMMC_INVALID_DATA_OPCODE;
	cmd.arg = 0;
	cmd.response_type = SD_RSP_TYPE_R1;
	cmd.timeout_ms = 1000;
	data.block_size = EMMC_SECTOR_SIZE;
	data.blocks = 1;
	data.data = buf_a;
	data.timeout_ms = 1000;

	/* 自包含：连发**两次**同样的非法请求（不依赖前一项是否已泄漏锁）。
	 *   · 驱动正确：请求在取锁**之前**就被拒（sdhci-of-linkedsemi.c:442-447，
	 *     `if (data && !data_opcode_supported(opcode)) return -ENOTSUP;`
	 *     位于 :449 的 k_mutex_lock 之前）⇒ 两次都立即返回 -ENOTSUP；
	 *   · 驱动缺陷：在持锁状态下 return -ENOTSUP（旧的 :475-478 写法 vs
	 *     :422/:506 的 K_FOREVER 锁）⇒ 第一次返回时锁未释放，第二次调用
	 *     永久阻塞 → 本线程不返回 → join 超时。
	 * 因此无论前一项是否执行过，本项都能独立证伪。 */
	mutex_probe_ret = sdhc_request(sdhci_dev, &cmd, &data);
	mutex_probe_ret2 = sdhc_request(sdhci_dev, &cmd, &data);
	mutex_probe_done = true;
}

ZTEST(zz_emmc_block_edge, request_mutex_not_leaked)
{
	/* 驱动缺陷靶点①的**非挂死判定**（自包含，不依赖用例执行顺序）：
	 * 探针线程内连发两次同样的非法请求，用有限超时 join：
	 *   · 驱动正确（取锁前拒绝）→ 两次都立即返回 -ENOTSUP → join 成功；
	 *   · 缺陷存在（持锁返回）→ 第二次调用永久阻塞在
	 *     `k_mutex_lock(K_FOREVER)`（:422）→ join 超时（-EAGAIN）→ FAIL。
	 * 探针线程优先级低于 ztest 测试线程（K_PRIO_PREEMPT 与默认值的关系见
	 * CONFIG_ZTEST_THREAD_PRIORITY），阻塞而非自旋，不会饿死测试线程。 */
	int jret;

	EMMC_SKIP_IF_GUARDED("request_mutex_not_leaked");
	zassert_ok(ensure_card(), "card not usable");

	mutex_probe_ret = 0x7fffffff;
	mutex_probe_ret2 = 0x7fffffff;
	mutex_probe_done = false;

	k_thread_create(&mutex_probe_thread, mutex_probe_stack,
			K_THREAD_STACK_SIZEOF(mutex_probe_stack), mutex_probe_entry,
			NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

	jret = k_thread_join(&mutex_probe_thread, K_MSEC(2000));

	ZT_INFO("request_mutex_not_leaked", "join=%d,probe_ret=%d,probe_ret2=%d,done=%d",
		jret, mutex_probe_ret, mutex_probe_ret2, (int)mutex_probe_done);

	zassert_ok(jret,
		   "second in-probe sdhc_request() did not return within 2000 ms: "
		   "access_mutex leaked by a -ENOTSUP early return taken while holding "
		   "the lock (sdhci-of-linkedsemi.c: data-opcode default branch vs "
		   ":422 k_mutex_lock K_FOREVER)");
	zassert_equal(mutex_probe_ret, -ENOTSUP,
		      "probe request #1 should be -ENOTSUP, got %d", mutex_probe_ret);
	zassert_equal(mutex_probe_ret2, -ENOTSUP,
		      "probe request #2 should be -ENOTSUP, got %d", mutex_probe_ret2);
}

ZTEST(zz_emmc_block_edge, set_io_clock_out_of_range_not_silent)
{
	/* 驱动缺陷靶点②：set_io 的时钟越界分支（:228-232）—— 条件不满足时
	 * **不报错、不改频，仍返回 0**，调用方无法感知参数被静默忽略。
	 * 用低于 f_min 的时钟（1000 < 50000）触发：该路径完全不碰控制器寄存器，
	 * 因此用例本身对 eMMC 总线无副作用（无需恢复）。 */
	struct sdhc_host_props props;
	struct sdhc_io io = { 0 };
	int ret;

	EMMC_SKIP_IF_GUARDED("set_io_clock_out_of_range_not_silent");
	zassert_ok(ensure_card(), "card not usable");
	zassert_ok(sdhc_get_host_props(sdhci_dev, &props), "get_host_props failed");

	io.clock = 1000; /* < f_min(50000)：越界 */
	io.bus_width = SDHC_BUS_WIDTH8BIT;
	io.power_mode = SDHC_POWER_ON;
	io.timing = SDHC_TIMING_HS;
	io.driver_type = SD_DRIVER_TYPE_B;

	ret = sdhc_set_io(sdhci_dev, &io);

	ZT_INFO("set_io_clock_out_of_range_not_silent",
		"f_min=%u,clock=%d,ret=%d", props.f_min, (int)io.clock, ret);

	zassert_not_equal(ret, 0,
			  "out-of-range clock %d (< f_min %u) was silently accepted "
			  "(defect roadmap §5.2 sdhc#4: sdhci-of-linkedsemi.c:228-232 "
			  "不报错、不改频，仍返回 0); got ret=%d",
			  (int)io.clock, props.f_min, ret);
}
