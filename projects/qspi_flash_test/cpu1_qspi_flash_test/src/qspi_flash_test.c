/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * QSPI flash 单核测试（ztest 原生框架）。
 *
 * 被测硬件：qspi2@40040000 + 板载 NOR（文档记作 32 MiB Puya，JEDEC ID 85 23 19；
 *           ⚠️ 2026-09-22 实测为 64 MiB / ID 20 40 20 —— 见 include/qspi_flash_test.h
 *           的常量说明与 flash_device_size() 注释），
 *           零外部接线（pinmux 板载默认 PC05/PC06-09/PC10，FUNC4）。
 *
 * 被测驱动（两层）：
 *   1. zephyr/drivers/spi/qspi_ls.c —— linkedsemi,ls-qspi 控制器驱动
 *      · qspi_ls_transceive() 恒返回 -ENOTSUP（标准 SPI API 不可用），
 *        只有 spi_nor_ops{transceive,read_init,write_init} 可用
 *      · STIG 单次数据相上限 4096 B（STG_DAT_PHASE_BYTES_MAX, :35 / :172-176）
 *      · stg_mem_write() 不切块 → 单次写 > 4096 直接 -EINVAL（:1181-1195）
 *      · stg_mem_read() 按 4096 切块（:1160-1176）→ 大读可跨 4096
 *      · stg_wire_cfg_fill() 未实现 JESD216_MODE_222 → 落 default: -EINVAL（:403-406）
 *      · apply_clk(): freq > bus(200 MHz) 或 clk_cyc > 31（freq < 6.25 MHz）→ -EINVAL；
 *        freq == 0 静默返回 0、不改时钟（:824-838）
 *      · qspi_ls_timing_calibration() + qspi_timing_calibration_status()（:1065）
 *   2. zephyr/drivers/flash/spi_nor_multi_dev.c —— jedec,spi-nor 上层
 *      · flash_write() 按页切块 ⇒ 单次 STG 写永远 < 4096，触不到 STG 上限
 *      · flash_erase() 要求地址扇区对齐、size 为 4096 整数倍
 *      · flash_read()/write() 要求 addr + size <= flash_size
 *
 * 器件运行态（raw_op 用例必须对齐，否则内容错位而驱动仍返回 0）：
 *   · **SPI 单线**：qspi2 的 DT `spi-ctrl-caps-mask = <0x40000>` 是 disable
 *     掩码（binding：bit18 = disable 4-4-4）⇒ 不进 QPI 4-4-4
 *     （spi_nor_multi_dev.c:2644-2647 的 enter_qpi 不会被调用）。
 *   · **4 字节地址**：容量 32 MiB > 16 MiB ⇒ `:2676` 使能 4 字节地址模式
 *     ⇒ flash 层存储访问一律 addr_len = 4（`:1380`）。
 *   ⇒ 所有 raw_op 的存储读/写都必须 addr_len = 4。
 *
 * 测试函数为纯 ztest：zassert_* 原生断言（失败详情 file:line+msg 由 zassert
 * 自身 printk，harness 归因进报告 reason 列）；数值上报用 ZT_INFO 约定行
 * （SPEC_REFERENCE §6.5.4，断言之前）。结果记账由 common/ztest_glue 的
 * zt_run_all 自动完成。
 *
 * 状态污染与执行顺序（ztest 套件按链接序 = 符号名字典序执行）：
 *   套件顺序 = qspi_flash_calib → qspi_flash_id → qspi_flash_rw → qspi_flash_stg
 *   qspi_flash_calib 会改总线频率并触发 spi_nor_re_init()，故其 after 钩子
 *   （用例级）在**每个**用例后把频率恢复为设备树值并重跑 init。
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/spi_nor.h>
#include <zephyr/drivers/jesd216.h>
#include <zephyr/drivers/qspi_ls.h>
#include <zephyr/logging/log.h>

#include "qspi_flash_test.h"
#include "ztest_glue.h"

LOG_MODULE_DECLARE(qspi_flash_test, LOG_LEVEL_INF);

/* qspi2 控制器（linkedsemi,ls-qspi） */
static const struct device *const qspi_dev = DEVICE_DT_GET(QSPI_CTRL_NODE);
/* flash2（jedec,spi-nor 实例，挂在 qspi2 下） */
static const struct device *const flash_dev = DEVICE_DT_GET(QSPI_FLASH_NODE);

/* 用例缓冲：放 .bss（不占 ztest 线程栈），4 字节对齐
 * （stg_cfg_set_dat 的 dat_offset = buf & 0x3，非对齐会触发首字部分写路径）。 */
static uint8_t wbuf[8192] __aligned(4);
static uint8_t rbuf[8192] __aligned(4);

/* 设备树编译期常量 */
#define QSPI_FLASH_MAX_FREQ DT_PROP(QSPI_FLASH_NODE, spi_max_frequency)
#define QSPI_FLASH_BUSWIDTH DT_PROP_OR(QSPI_FLASH_NODE, spi_max_buswidth, 1)
#define QSPI_CALIB_DISABLED DT_PROP_OR(QSPI_CTRL_NODE, timing_calibration_disabled, 0)

/* ========== 辅助 ========== */

static void fill_pattern(uint8_t *b, size_t n, uint8_t seed)
{
	for (size_t i = 0; i < n; i++) {
		b[i] = (uint8_t)(seed + i * 7U + (i >> 3));
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

/* SFDP/驱动探测到的容量（字节）；0 = 不可用 */
static size_t qspi_flash_size(void)
{
	const struct flash_parameters *p = flash_get_parameters(flash_dev);

	if (p == NULL) {
		return 0;
	}
	return p->flash_size;
}

static bool jedec_id_plausible(const uint8_t *id, size_t n)
{
	bool all0 = true;
	bool allf = true;

	for (size_t i = 0; i < n; i++) {
		if (id[i] != 0x00u) {
			all0 = false;
		}
		if (id[i] != 0xffu) {
			allf = false;
		}
	}

	return !all0 && !allf;
}

/*
 * 直接走驱动的 spi_nor_ops 扩展（= spi_nor_multi_dev.c 内部用的同一条路径）。
 * qspi_ls_nor_transceive() 的 config 参数是 ARG_UNUSED，传 NULL 即可。
 * 用途：绕开 flash 层（flash_read/write 会按页/按 4096 切块），
 *       精确打到 STIG 单次数据相的字节上限与 wire-mode 分支。
 */
static int raw_op(enum jesd216_mode_type mode, uint8_t opcode, off_t addr,
		  uint8_t addr_len, uint8_t dummy_cycle, void *buf, size_t len,
		  uint32_t dir)
{
	const struct spi_driver_api *api = (const struct spi_driver_api *)qspi_dev->api;
	struct spi_nor_op_info op = SPI_NOR_OP_INFO(mode, opcode, addr, addr_len,
						    dummy_cycle, buf, len, dir);

	if (api == NULL || api->spi_nor_op == NULL ||
	    api->spi_nor_op->transceive == NULL) {
		return -ENOSYS;
	}

	return api->spi_nor_op->transceive(qspi_dev, NULL, &op);
}

/* =====================================================================
 * 套件 1：qspi_flash_id —— 身份 / 静态属性 / 参数校验
 * ===================================================================== */

ZTEST(qspi_flash_id, jedec_id_match)
{
	uint8_t id[SPI_NOR_MAX_ID_LEN] = { 0 };
	int ret = flash_read_jedec_id(flash_dev, id);

	ZT_INFO("jedec_id_match", "id=%02x%02x%02x,ret=%d", id[0], id[1], id[2], ret);

	zassert_ok(ret, "flash_read_jedec_id returned %d", ret);
	/* 不写死厂商 ID：非全 0 / 全 F 即视为在线 */
	zassert_true(jedec_id_plausible(id, SPI_NOR_MAX_ID_LEN),
		     "JEDEC id all-0 or all-FF: %02x %02x %02x", id[0], id[1], id[2]);
}

ZTEST(qspi_flash_id, jedec_id_cached)
{
	uint8_t live[SPI_NOR_MAX_ID_LEN] = { 0 };
	uint8_t cached[SPI_NOR_MAX_ID_LEN] = { 0 };
	int ret_live = flash_read_jedec_id(flash_dev, live);
	int ret = spi_nor_get_jedec_id(flash_dev, cached);

	ZT_INFO("jedec_id_cached", "live=%02x%02x%02x,cached=%02x%02x%02x,ret=%d",
		live[0], live[1], live[2], cached[0], cached[1], cached[2], ret);

	zassert_ok(ret_live, "flash_read_jedec_id returned %d", ret_live);
	zassert_ok(ret, "spi_nor_get_jedec_id returned %d", ret);
	zassert_true(jedec_id_plausible(cached, SPI_NOR_MAX_ID_LEN),
		     "cached JEDEC id all-0 or all-FF: %02x %02x %02x",
		     cached[0], cached[1], cached[2]);
	zassert_mem_equal(cached, live, SPI_NOR_MAX_ID_LEN,
			  "cached JEDEC != live read");
}

ZTEST(qspi_flash_id, jedec_id_null_arg)
{
	/* 异常参数：NULL 输出缓冲 */
	zassert_equal(flash_read_jedec_id(flash_dev, NULL), -EINVAL,
		      "flash_read_jedec_id(NULL) should be -EINVAL");
	zassert_equal(spi_nor_get_jedec_id(flash_dev, NULL), -EINVAL,
		      "spi_nor_get_jedec_id(NULL) should be -EINVAL");
}

ZTEST(qspi_flash_id, get_parameters)
{
	const struct flash_parameters *p = flash_get_parameters(flash_dev);

	zassert_not_null(p, "flash_get_parameters returned NULL");
	ZT_INFO("get_parameters", "write_block_size=%zu,erase_value=0x%02x",
		p->write_block_size, p->erase_value);

	zassert_equal(p->erase_value, 0xff, "erase_value: got 0x%02x want 0xff",
		      p->erase_value);
	/* qspi_ls + spi_nor_multi_dev 路径下 write_block_size 取设备树
	 * write-block-size 缺省值 0x1000（spi_nor_multi_dev.c SPI_NOR_INST 宏） */
	zassert_equal(p->write_block_size, QSPI_SECTOR_SIZE,
		      "write_block_size: got %zu want %u", p->write_block_size,
		      QSPI_SECTOR_SIZE);
}

ZTEST(qspi_flash_id, sfdp_header)
{
	uint8_t hdr[8] = { 0 };
	int ret = flash_sfdp_read(flash_dev, 0, hdr, sizeof(hdr));

	ZT_INFO("sfdp_header", "sfdp=%02x%02x%02x%02x,ret=%d", hdr[0], hdr[1], hdr[2],
		hdr[3], ret);

	zassert_ok(ret, "flash_sfdp_read returned %d", ret);
	/* JESD216 SFDP 头 4 字节 = ASCII "SFDP" */
	zassert_mem_equal(hdr, "SFDP", 4, "SFDP signature mismatch: %02x %02x %02x %02x",
			  hdr[0], hdr[1], hdr[2], hdr[3]);
}

ZTEST(qspi_flash_id, size_runtime)
{
	struct flash_pages_info info;
	const size_t sz = qspi_flash_size();
	int ret_last;
	int ret_over;

	ZT_INFO("size_runtime", "flash_size=%zu", sz);

	zassert_true(sz > 0, "flash_get_parameters()->flash_size is 0");
	/* 末字节应在布局内；末字节+1（即 size 偏移）应越界 */
	ret_last = flash_get_page_info_by_offs(flash_dev, (off_t)sz - 1, &info);
	ret_over = flash_get_page_info_by_offs(flash_dev, (off_t)sz, &info);

	ZT_INFO("size_runtime", "last_byte_ret=%d,over_end_ret=%d", ret_last, ret_over);

	zassert_ok(ret_last, "flash_size-1 should be inside (ret %d, size=%zu)",
		   ret_last, sz);
	zassert_not_equal(ret_over, 0,
			  "offset flash_size=%zu should be out of range", sz);
}

ZTEST(qspi_flash_id, line_width_set)
{
	int ret_bad = spi_nor_set_line_width(flash_dev, 8);
	int ret_ok = spi_nor_set_line_width(flash_dev, 1);

	ZT_INFO("line_width_set", "bad=%d,ok=%d", ret_bad, ret_ok);

	zassert_equal(ret_bad, -EINVAL, "buswidth 8 should be -EINVAL, got %d", ret_bad);
	zassert_ok(ret_ok, "buswidth 1 should be accepted, got %d", ret_ok);

	/* 恢复设备树值（本字段只影响 spi_nor_process_sfdp 的读取命令选择） */
	zassert_ok(spi_nor_set_line_width(flash_dev, QSPI_FLASH_BUSWIDTH),
		   "restore buswidth failed");
}

ZTEST(qspi_flash_id, four_byte_mode_unknown_mfr)
{
	bool en4b = false;
	int ret = spi_nor_get_4byte_mode(flash_dev, &en4b);

	ZT_INFO("four_byte_mode_unknown_mfr", "ret=%d,en4b=%d", ret, (int)en4b);

	/* spi_nor_get_4byte_mode() 的厂商白名单只有 MXIC/Winbond/Gigadevice，
	 * Puya(0x85) 落 default: → -ENXIO。属驱动功能缺口（未按 JESD216 通用
	 * 方式查询），本用例钉住现状：新增厂商支持时需同步更新。 */
	zassert_equal(ret, -ENXIO,
		      "Puya (0x85) not in manufacturer whitelist -> -ENXIO, got %d", ret);
}

/* =====================================================================
 * 套件 2：qspi_flash_rw —— flash API 层擦 / 写 / 读（功能通路 + 边界长度）
 * ===================================================================== */

ZTEST_SUITE(qspi_flash_rw, NULL, NULL, NULL, NULL, NULL);

ZTEST(qspi_flash_rw, erase_sector_all_ff)
{
	int ret;

	memset(rbuf, 0x00, QSPI_SECTOR_SIZE);
	ret = flash_erase(flash_dev, QSPI_TEST_BASE, QSPI_SECTOR_SIZE);
	zassert_ok(ret, "flash_erase(%#x, %u) failed: %d", QSPI_TEST_BASE,
		   QSPI_SECTOR_SIZE, ret);

	ret = flash_read(flash_dev, QSPI_TEST_BASE, rbuf, QSPI_SECTOR_SIZE);
	zassert_ok(ret, "flash_read after erase failed: %d", ret);

	/* 失败时给出首个非 0xFF 的偏移，便于归因 */
	int offender = -1;

	for (size_t i = 0; i < QSPI_SECTOR_SIZE; i++) {
		if (rbuf[i] != 0xff) {
			offender = (int)i;
			break;
		}
	}

	zassert_equal(offender, -1, "erased sector not all 0xFF (first offender @%d val=0x%02x)",
		      offender, (offender >= 0) ? rbuf[offender] : 0);
}

ZTEST(qspi_flash_rw, write_read_len1)
{
	const size_t len = 1;
	const uint32_t base = QSPI_TEST_BASE + 0 * QSPI_SECTOR_SIZE;
	int ret;

	fill_pattern(wbuf, len, 0x11);
	memset(rbuf, 0x00, len);

	zassert_ok(flash_erase(flash_dev, base, QSPI_SECTOR_SIZE), "erase failed");
	ret = flash_write(flash_dev, base, wbuf, len);
	zassert_ok(ret, "flash_write(len=%zu) failed: %d", len, ret);
	ret = flash_read(flash_dev, base, rbuf, len);
	zassert_ok(ret, "flash_read(len=%zu) failed: %d", len, ret);
	zassert_mem_equal(rbuf, wbuf, len, "len=%zu write/read mismatch", len);
}

ZTEST(qspi_flash_rw, write_read_len255)
{
	const size_t len = 255;
	const uint32_t base = QSPI_TEST_BASE + 1 * QSPI_SECTOR_SIZE;
	int ret;

	fill_pattern(wbuf, len, 0x22);
	memset(rbuf, 0x00, len);

	zassert_ok(flash_erase(flash_dev, base, QSPI_SECTOR_SIZE), "erase failed");
	ret = flash_write(flash_dev, base, wbuf, len);
	zassert_ok(ret, "flash_write(len=%zu) failed: %d", len, ret);
	ret = flash_read(flash_dev, base, rbuf, len);
	zassert_ok(ret, "flash_read(len=%zu) failed: %d", len, ret);
	zassert_mem_equal(rbuf, wbuf, len, "len=%zu write/read mismatch (%d bytes differ)",
			  len, count_diff(rbuf, wbuf, len));
}

ZTEST(qspi_flash_rw, write_read_len256)
{
	const size_t len = 256; /* 典型 NOR 页大小 */
	const uint32_t base = QSPI_TEST_BASE + 2 * QSPI_SECTOR_SIZE;
	int ret;

	fill_pattern(wbuf, len, 0x33);
	memset(rbuf, 0x00, len);

	zassert_ok(flash_erase(flash_dev, base, QSPI_SECTOR_SIZE), "erase failed");
	ret = flash_write(flash_dev, base, wbuf, len);
	zassert_ok(ret, "flash_write(len=%zu) failed: %d", len, ret);
	ret = flash_read(flash_dev, base, rbuf, len);
	zassert_ok(ret, "flash_read(len=%zu) failed: %d", len, ret);
	zassert_mem_equal(rbuf, wbuf, len, "len=%zu write/read mismatch (%d bytes differ)",
			  len, count_diff(rbuf, wbuf, len));
}

ZTEST(qspi_flash_rw, write_read_len4096)
{
	/* 恰好一个扇区：覆盖 STIG 数据相上限（4096 B）处的读切块边界 */
	const size_t len = QSPI_SECTOR_SIZE;
	const uint32_t base = QSPI_TEST_BASE + 3 * QSPI_SECTOR_SIZE;
	int ret;

	fill_pattern(wbuf, len, 0x44);
	memset(rbuf, 0x00, len);

	zassert_ok(flash_erase(flash_dev, base, QSPI_SECTOR_SIZE), "erase failed");
	ret = flash_write(flash_dev, base, wbuf, len);
	zassert_ok(ret, "flash_write(len=%zu) failed: %d", len, ret);
	ret = flash_read(flash_dev, base, rbuf, len);
	zassert_ok(ret, "flash_read(len=%zu) failed: %d", len, ret);
	zassert_mem_equal(rbuf, wbuf, len, "len=%zu write/read mismatch (%d bytes differ)",
			  len, count_diff(rbuf, wbuf, len));
}

ZTEST(qspi_flash_rw, write_read_len8192)
{
	/* > 4096：验证 stg_mem_read() 的 4096 切块拼接正确（跨块地址递增） */
	const size_t len = 8192;
	const uint32_t base = QSPI_TEST_BASE + 4 * QSPI_SECTOR_SIZE;
	int ret;

	fill_pattern(wbuf, len, 0x55);
	memset(rbuf, 0x00, len);

	/* 2 个连续扇区 */
	for (uint32_t i = 0; i < 2; i++) {
		zassert_ok(flash_erase(flash_dev, base + i * QSPI_SECTOR_SIZE,
				       QSPI_SECTOR_SIZE),
			   "erase sector %u failed", i);
	}

	ret = flash_write(flash_dev, base, wbuf, len);
	zassert_ok(ret, "flash_write(len=%zu) failed: %d", len, ret);
	ret = flash_read(flash_dev, base, rbuf, len);
	zassert_ok(ret, "flash_read(len=%zu) failed: %d", len, ret);

	ZT_INFO("write_read_len8192", "len=%zu,diff=%d", len, count_diff(rbuf, wbuf, len));
	zassert_mem_equal(rbuf, wbuf, len, "len=%zu write/read mismatch (%d bytes differ)",
			  len, count_diff(rbuf, wbuf, len));
}

ZTEST(qspi_flash_rw, erase_unaligned_addr_rejected)
{
	/* 异常参数：地址未扇区对齐（spi_nor_erase 的 SPI_NOR_IS_SECTOR_ALIGNED 检查） */
	int ret = flash_erase(flash_dev, QSPI_TEST_BASE + 1, QSPI_SECTOR_SIZE);

	zassert_equal(ret, -EINVAL, "unaligned erase addr should be -EINVAL, got %d", ret);
}

ZTEST(qspi_flash_rw, erase_non_sector_multiple_rejected)
{
	/* 异常参数：size 不是扇区整数倍 */
	int ret = flash_erase(flash_dev, QSPI_TEST_BASE, QSPI_SECTOR_SIZE + 1);

	zassert_equal(ret, -EINVAL,
		      "erase size %u should be -EINVAL, got %d", QSPI_SECTOR_SIZE + 1,
		      ret);
}

/*
 * 器件实际容量（字节），运行期推导。
 *
 * 本工程曾把容量写死为 32 MiB（文档记作板载 Puya NOR，JEDEC ID 85 23 19），
 * 但 2026-09-22 回归的串口实测（serial_TGT.log）该 qspi2 器件为 64 MiB：
 *   · `<inf> spi_nor: flash2@0: 64 MiBy flash`（= SFDP BFP DW2 密度 512 Mbit）
 *   · `<inf> spi_nor: flash2@0: flash present, jedec id 20 40 20`（ID 字节 2 = 0x20
 *     ⇒ 512 Mbit，非 85 23 19）
 * 容量写死后，"越界" 地址会落在合法区内 ⇒ 驱动正确返回 0，而用例误判为缺陷
 * （实测 `read past end should be -EINVAL, got 0`）。
 * 故容量改由 flash 层 layout 推导（CONFIG_SPI_NOR_SFDP_RUNTIME 下
 * layout.pages_count = flash_size / layout_page_size），器件换型时用例自动跟随。
 * 返回值 0 表示查询不可用（用例据此报错，不做静默兜底）。
 */
static size_t flash_device_size(void)
{
	const size_t pages = flash_get_page_count(flash_dev);
	struct flash_pages_info info;

	if (pages == 0U) {
		return 0U;
	}
	if (flash_get_page_info_by_idx(flash_dev, 0, &info) != 0) {
		return 0U;
	}

	return (size_t)info.size * pages;
}

ZTEST(qspi_flash_rw, read_past_end_rejected)
{
	const size_t sz = qspi_flash_size();
	int ret;

	zassert_true(sz > 0, "flash_size is 0");
	/* 异常参数：addr 恰好落在 flash_size（越界），应 -EINVAL */
	ret = flash_read(flash_dev, (off_t)sz, rbuf, 16);

	ZT_INFO("read_past_end_rejected", "flash_size=%zu,ret=%d", sz, ret);
	zassert_equal(ret, -EINVAL, "read past end(size=%zu) should be -EINVAL, got %d",
		      sz, ret);
}

ZTEST(qspi_flash_rw, write_past_end_rejected)
{
	const size_t sz = qspi_flash_size();
	int ret;

	zassert_true(sz > 0, "flash_size is 0");
	ret = flash_write(flash_dev, (off_t)sz, wbuf, 16);

	ZT_INFO("write_past_end_rejected", "flash_size=%zu,ret=%d", sz, ret);
	zassert_equal(ret, -EINVAL, "write past end(size=%zu) should be -EINVAL, got %d",
		      sz, ret);
}

/* =====================================================================
 * 套件 3：qspi_flash_stg —— STIG 单次数据相边界 + wire mode 分支
 *          （直接走 spi_nor_ops 扩展，绕开 flash 层的按页/按 4096 切块，
 *            否则永远触不到 qspi_ls.c 的单次 4096 B 上限）
 * ===================================================================== */

ZTEST_SUITE(qspi_flash_stg, NULL, NULL, NULL, NULL, NULL);

ZTEST(qspi_flash_stg, stg_cmd_wren_ok)
{
	/* addr_len == 0 且 data_len == 0 → stg_cmd() 纯命令路径 */
	int ret = raw_op(JESD216_MODE_111, SPI_NOR_CMD_WREN, 0, 0, 0, NULL, 0,
			 SPI_NOR_DATA_DIRECT_OUT);

	zassert_ok(ret, "raw WREN command failed: %d", ret);
}

ZTEST(qspi_flash_stg, raw_read_1_1_1_ok)
{
	const uint32_t base = QSPI_TEST_BASE + 6 * QSPI_SECTOR_SIZE;
	const size_t len = 256;
	int ret;

	fill_pattern(wbuf, len, 0x66);
	memset(rbuf, 0x00, len);

	zassert_ok(flash_erase(flash_dev, base, QSPI_SECTOR_SIZE), "erase failed");
	zassert_ok(flash_write(flash_dev, base, wbuf, len), "flash_write failed");

	/* 运行态：**SPI 单线 + 4 字节地址**，raw 事务必须与之一致。
	 *   · qspi2 的 DT `spi-ctrl-caps-mask = <0x40000>` 是 **disable 掩码**
	 *     （binding linkedsemi,ls-qspi.yaml：bit18 = disable 4-4-4；驱动按
	 *      `cap_mask = ~(ctrl|nor)` 取反后判 `& SPI_NOR_MODE_4_4_4_CAP`）
	 *     ⇒ 0x40000 恰好**关掉** 4-4-4，`spi_nor_enter_qpi_mode()`
	 *     （spi_nor_multi_dev.c:2644-2647）**不会**被调用，器件停在单线读。
	 *   · 容量 32 MiB > 16 MiB ⇒ `:2676` 调 `spi_nor_config_4byte_mode(dev,true)`
	 *     （0xB7）⇒ flash 层存储访问一律 addr_len = 4（`:1380`）。
	 * 原用例发 3 字节地址：器件把数据相首字节当作第 4 个地址字节 ⇒ 内容错位，
	 * 而驱动本身返回 0（"参数被接受"）。故修法是让 raw 读与运行态一致：
	 * 1-1-1 保持（0x03 在 4 字节地址态同样吃 4 字节地址），addr_len 3 → 4。
	 * 详见 TRIAGE_QSPI_SPIF_2026-09-18.md §1.1 Q1（其中"上电即进 QPI"的结论
	 * 应更正为本注释所述，见 COVERAGE.md「运行态」节）。 */
	ret = raw_op(JESD216_MODE_111, SPI_NOR_CMD_READ, base, 4, 0, rbuf, len,
		     SPI_NOR_DATA_DIRECT_IN);
	zassert_ok(ret, "raw 1-1-1 READ failed: %d", ret);
	ZT_INFO("raw_read_1_1_1_ok", "len=%zu,addr_len=4,diff=%d", len,
		count_diff(rbuf, wbuf, len));
	zassert_mem_equal(rbuf, wbuf, len, "1-1-1 readback mismatch (%d bytes differ)",
			  count_diff(rbuf, wbuf, len));
}

ZTEST(qspi_flash_stg, raw_read_1_1_4_ok)
{
	/* 1-1-4 读（QREAD 0x6B，3 字节地址 + 8 dummy 周期）
	 * → stg_wire_cfg_fill() JESD216_MODE_114 分支 */
	const uint32_t base = QSPI_TEST_BASE + 7 * QSPI_SECTOR_SIZE;
	const size_t len = 256;
	int ret;

	memset(rbuf, 0x00, len);
	ret = raw_op(JESD216_MODE_114, SPI_NOR_CMD_QREAD, base, 3, 8, rbuf, len,
		     SPI_NOR_DATA_DIRECT_IN);

	ZT_INFO("raw_read_1_1_4_ok", "ret=%d", ret);
	/* 只断言驱动接受该 wire mode（内容已由 1-1-1 用例校验）；
	 * 器件是否真的处于 QPI 使能态由 spi_nor 层在 init 时决定，
	 * 这里不引入对内容一致性的耦合。 */
	zassert_ok(ret, "raw 1-1-4 QREAD rejected by driver: %d", ret);
}

ZTEST(qspi_flash_stg, raw_read_1_4_4_ok)
{
	/* 1-4-4 读（4READ 0xEB，3 字节地址 + 6 dummy 周期）
	 * → stg_wire_cfg_fill() JESD216_MODE_144 分支 */
	const uint32_t base = QSPI_TEST_BASE + 7 * QSPI_SECTOR_SIZE;
	const size_t len = 256;
	int ret;

	memset(rbuf, 0x00, len);
	ret = raw_op(JESD216_MODE_144, SPI_NOR_CMD_4READ, base, 3, 6, rbuf, len,
		     SPI_NOR_DATA_DIRECT_IN);

	ZT_INFO("raw_read_1_4_4_ok", "ret=%d", ret);
	zassert_ok(ret, "raw 1-4-4 4READ rejected by driver: %d", ret);
}

ZTEST(qspi_flash_stg, mode_222_rejected)
{
	/* 驱动功能缺口：JESD216_MODE_222（2-2-2）未实现，
	 * stg_wire_cfg_fill() 无该 case → 落 default: → -EINVAL（qspi_ls.c:403-406）。
	 * 用非破坏性 opcode（WRDI 0x04，忽略地址与数据）只探 wire-mode 分支，
	 * 且 -EINVAL 在任何总线动作之前返回。 */
	int ret = raw_op(JESD216_MODE_222, SPI_NOR_CMD_WRDI, 0, 3, 0, rbuf, 16,
			 SPI_NOR_DATA_DIRECT_OUT);

	ZT_INFO("mode_222_rejected", "ret=%d", ret);
	zassert_equal(ret, -EINVAL, "MODE_222 should be -EINVAL (unimplemented), got %d",
		      ret);
}

ZTEST(qspi_flash_stg, stg_write_len_4096_accepted)
{
	/* STIG 单次数据相**上限**：4096 B 必须被接受。
	 * opcode 用 WRDI(0x04)：忽略地址与数据，非破坏性；
	 * addr_len = 3 走 stg_mem_write()（:1181-1195 是那条不切块的写路径）。 */
	int ret = raw_op(JESD216_MODE_111, SPI_NOR_CMD_WRDI, QSPI_TEST_BASE, 3, 0,
			 wbuf, QSPI_STG_DAT_MAX, SPI_NOR_DATA_DIRECT_OUT);

	ZT_INFO("stg_write_len_4096_accepted", "len=%u,ret=%d", QSPI_STG_DAT_MAX, ret);
	zassert_ok(ret, "single STG write of %u B should be accepted, got %d",
		   QSPI_STG_DAT_MAX, ret);
}

ZTEST(qspi_flash_stg, stg_write_len_4097_rejected)
{
	/* STIG 单次数据相**上限 + 1**：4097 B 必须被拒（-EINVAL）。
	 * 检查发生在 stg_cfg_set_dat()，先于任何总线动作 → 无副作用。 */
	int ret = raw_op(JESD216_MODE_111, SPI_NOR_CMD_WRDI, QSPI_TEST_BASE, 3, 0,
			 wbuf, QSPI_STG_DAT_MAX + 1, SPI_NOR_DATA_DIRECT_OUT);

	ZT_INFO("stg_write_len_4097_rejected", "len=%u,ret=%d", QSPI_STG_DAT_MAX + 1,
		ret);
	zassert_equal(ret, -EINVAL, "single STG write of %u B should be -EINVAL, got %d",
		      QSPI_STG_DAT_MAX + 1, ret);
}

ZTEST(qspi_flash_stg, stg_reg_read_len_4097_rejected)
{
	/* 寄存器读路径（addr_len == 0 → stg_reg_read()）同样受 4096 上限约束 */
	int ret = raw_op(JESD216_MODE_111, SPI_NOR_CMD_RDSR, 0, 0, 0, rbuf,
			 QSPI_STG_DAT_MAX + 1, SPI_NOR_DATA_DIRECT_IN);

	ZT_INFO("stg_reg_read_len_4097_rejected", "ret=%d", ret);
	zassert_equal(ret, -EINVAL,
		      "STG register read of %u B should be -EINVAL, got %d",
		      QSPI_STG_DAT_MAX + 1, ret);
}

ZTEST(qspi_flash_stg, stg_read_chunked_over_4096)
{
	/* 大读切块（stg_mem_read 的 MIN(remain, 4096) 循环）：8192 B 单次 raw 读，
	 * 数据与 flash 层写入的内容逐字节一致 → 证明切块拼接与地址递增正确 */
	const uint32_t base = QSPI_TEST_BASE + 4 * QSPI_SECTOR_SIZE; /* 同 rw 套件 */
	const size_t len = 8192;
	int ret;

	fill_pattern(wbuf, len, 0x55);
	memset(rbuf, 0x00, len);

	/* 该窗口已由 rw 套件擦写（0x55 pattern）；此处重新擦写保证自洽 */
	for (uint32_t i = 0; i < 2; i++) {
		zassert_ok(flash_erase(flash_dev, base + i * QSPI_SECTOR_SIZE,
				       QSPI_SECTOR_SIZE),
			   "erase sector %u failed", i);
	}
	zassert_ok(flash_write(flash_dev, base, wbuf, len), "flash_write failed");

	/* 与 raw_read_1_1_1_ok 同一运行态修正：器件为 4 字节地址模式，
	 * 地址长度必须给 4，否则切片循环的地址递增也一并错位。
	 * 切块逻辑（stg_mem_read 的 MIN(remain, 4096) 循环，qspi_ls.c:1147-1180）
	 * 本身与地址长度无关，改后两条覆盖（切块 + 内容）都保留。 */
	ret = raw_op(JESD216_MODE_111, SPI_NOR_CMD_READ, base, 4, 0, rbuf, len,
		     SPI_NOR_DATA_DIRECT_IN);
	zassert_ok(ret, "raw 8192 B read failed: %d", ret);
	ZT_INFO("stg_read_chunked_over_4096", "len=%zu,addr_len=4,diff=%d", len,
		count_diff(rbuf, wbuf, len));
	zassert_mem_equal(rbuf, wbuf, len, "chunked read mismatch (%d bytes differ)",
			  count_diff(rbuf, wbuf, len));
}

ZTEST(qspi_flash_stg, spi_transceive_notsup)
{
	/* qspi_ls_transceive() 恒返回 -ENOTSUP：标准 SPI API 在本控制器上不可用 */
	struct spi_buf tx = { .buf = wbuf, .len = 4 };
	struct spi_buf rx = { .buf = rbuf, .len = 4 };
	struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };
	struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };
	struct spi_config cfg = {
		.frequency = QSPI_FLASH_MAX_FREQ,
		.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8),
		.slave = 0,
	};

	int ret = spi_transceive(qspi_dev, &cfg, &tx_set, &rx_set);

	ZT_INFO("spi_transceive_notsup", "ret=%d", ret);
	zassert_equal(ret, -ENOTSUP, "spi_transceive should be -ENOTSUP, got %d", ret);

	/* release 是空实现，恒返回 0 */
	zassert_ok(spi_release(qspi_dev, &cfg), "spi_release should return 0");
}

/* =====================================================================
 * 套件 4：qspi_flash_calib —— 时序校准状态 + 总线频率边界
 *          （apply_clk 只在 read_init 路径被调用，故频率用例走
 *            spi_nor_set_freq() + spi_nor_re_init() 组合）
 * ===================================================================== */

static void qspi_calib_after(void *fixture)
{
	ARG_UNUSED(fixture);

	/* 每个用例后恢复设备树频率并重跑 init（避免污染后续套件） */
	(void)spi_nor_set_freq(flash_dev, QSPI_FLASH_MAX_FREQ);
	(void)spi_nor_re_init(flash_dev);
}

ZTEST_SUITE(qspi_flash_calib, NULL, NULL, NULL, qspi_calib_after, NULL);

ZTEST(qspi_flash_calib, calib_status_null)
{
	/* 异常参数：NULL 设备 */
	zassert_equal(qspi_timing_calibration_status(NULL), -EINVAL,
		      "qspi_timing_calibration_status(NULL) should be -EINVAL");
}

ZTEST(qspi_flash_calib, calib_status_expected)
{
	int st = qspi_timing_calibration_status(qspi_dev);

	ZT_INFO("calib_status_expected", "status=%d,calib_disabled=%d", st,
		(int)QSPI_CALIB_DISABLED);

	if (QSPI_CALIB_DISABLED) {
		/* overlay 关掉校准时：read_init → qspi_ls_timing_calibration()
		 * 首查 timing_calibration_disabled → QSPI_TIMING_CALIB_UNUSED */
		zassert_equal(st, QSPI_TIMING_CALIB_UNUSED,
			      "calibration disabled -> UNUSED, got %d", st);
	} else {
		/* 校准时钟 10 MHz <= 配置 50 MHz → 校准实际执行并应成功 */
		zassert_equal(st, QSPI_TIMING_CALIB_SUCCESS,
			      "calibration enabled -> SUCCESS, got %d", st);
	}
}

ZTEST(qspi_flash_calib, set_freq_above_max_rejected)
{
	/* 频率上界：> 总线时钟 200 MHz → apply_clk() 返回 -EINVAL
	 * → read_init 失败 → spi_nor_init 失败 → re_init 非 0。
	 * 钉住：spi_nor_set_freq() 本身不校验、恒返回 0（错误延迟到 init 才暴露）。 */
	int ret_set = spi_nor_set_freq(flash_dev, QSPI_FREQ_ABOVE_MAX);
	int ret_init = spi_nor_re_init(flash_dev);

	ZT_INFO("set_freq_above_max_rejected", "set=%d,reinit=%d", ret_set, ret_init);

	zassert_ok(ret_set, "spi_nor_set_freq should not validate, got %d", ret_set);
	zassert_not_equal(ret_init, 0,
			  "re_init at %u Hz (> bus 200 MHz) should fail, got %d",
			  QSPI_FREQ_ABOVE_MAX, ret_init);
}

ZTEST(qspi_flash_calib, set_freq_below_min_rejected)
{
	/* 频率下界：clk_cyc = 200MHz/6MHz - 1 = 32 > 31 → apply_clk() -EINVAL */
	int ret_set = spi_nor_set_freq(flash_dev, QSPI_FREQ_BELOW_MIN);
	int ret_init = spi_nor_re_init(flash_dev);

	ZT_INFO("set_freq_below_min_rejected", "set=%d,reinit=%d", ret_set, ret_init);

	zassert_ok(ret_set, "spi_nor_set_freq should not validate, got %d", ret_set);
	zassert_not_equal(ret_init, 0, "re_init at %u Hz (< 6.25 MHz) should fail, got %d",
			  QSPI_FREQ_BELOW_MIN, ret_init);
}

ZTEST(qspi_flash_calib, set_freq_boundary_6m25_accepted)
{
	/* 边界：6.25 MHz 恰好 clk_cyc == 31（不超过 31）→ 驱动接受。
	 * 该点与 set_freq_below_min_rejected 构成 ±1 边界对。 */
	int ret_set = spi_nor_set_freq(flash_dev, QSPI_FREQ_MIN_OK);
	int ret_init = spi_nor_re_init(flash_dev);

	ZT_INFO("set_freq_boundary_6m25_accepted", "set=%d,reinit=%d", ret_set, ret_init);

	zassert_ok(ret_set, "spi_nor_set_freq failed: %d", ret_set);
	zassert_ok(ret_init, "re_init at 6.25 MHz (clk_cyc == 31) should succeed, got %d",
		   ret_init);
}

ZTEST(qspi_flash_calib, set_freq_zero_silent)
{
	/* 驱动缺陷钉住（roadmap §5.2 qspi#4）：frequency == 0 时 apply_clk()
	 * 直接 return 0，**不改时钟也不报错**。正确语义应为 -EINVAL。
	 * 本用例断言 re_init 仍成功（即"静默接受"这一现状），并把返回值上报；
	 * 若驱动改为返回 -EINVAL，本用例会失败并提示同步更新。 */
	int ret_set = spi_nor_set_freq(flash_dev, 0);
	int ret_init = spi_nor_re_init(flash_dev);

	ZT_INFO("set_freq_zero_silent", "set=%d,reinit=%d", ret_set, ret_init);

	zassert_ok(ret_set, "spi_nor_set_freq(0) returned %d", ret_set);
	zassert_ok(ret_init,
		   "frequency 0 is silently accepted by apply_clk() (documented defect "
		   "roadmap §5.2 qspi#4); got re_init=%d", ret_init);
}
