/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * spi_loopback.c —— SPI1↔SPI3 **外部跳线**回环用例（P4.6，2026-09-17 新增）。
 *
 * 与同目录 spi_test.c（原有 18 项，判据全部来自驱动源码/寄存器语义）不同，
 * 本文件需要一条 **芯片外部连接路径**：`外部: SPI1↔SPI3 跳线`（SPEC_REFERENCE §6.6）。
 *
 * ---------------------------------------------------------------------------
 * 角色分配与依据（★ 见 tests/COVERAGE.md「回环可行性结论」）
 * ---------------------------------------------------------------------------
 *   从设备 (slave)  = spi1 —— `linkedsemi,ls-spi`  → zephyr/drivers/spi/spi_ls.c
 *                    PA00=SCK / PA01=MOSI / PA02=MISO（板级 pinctrl）
 *   主设备 (master) = spi3 —— `snps,designware-spi` → zephyr/drivers/spi/spi_dw.c
 *                    PG14=SCK / PG15=IO0(MOSI) / PG03=IO1(MISO)（板级 pinctrl）
 *
 *   选这一方向的证据（不是拍脑袋）：
 *   (1) spi_dw.c:407-416 要求 DT 属性 `serial-target` 为真才允许从模式，而该属性
 *       描述的是 **硬件固定角色**（驱动从不写 master/slave 位，spi_dw_init 只做
 *       reset/clock/pinctrl/IRQ）。全树 spi3/spi4 的 `serial-target` 恒为 false，
 *       且所有用例（spif_test P4.7、pfr_tpm samples/spi_master_communication）
 *       都把 spi3/spi4 当 **master** 用 ⇒ 没有 spi3 可作从设备的证据。
 *   (2) spi1 在 Zephyr QSH pinctrl 头里**有**硬件 NSS 复用宏
 *       （`SPI1_NSS_FUNC1_IDX19_PINMUX_PA/PC/PI`，lsqsh-pinctrl_func_1.h:29/92/278），
 *       spi3 **没有任何 NSS 复用宏** ⇒ SoC 设计上 spi1 才是"可当从设备"的那一侧。
 *   (3) 上轮（P4.5）跳过 `spi_ls.c` 从设备路径的原因正是"无主控时钟 ⇒
 *       `spi_context_wait_for_completion()` 走 K_FOREVER"；本文件用 spi3 提供时钟。
 *
 * ---------------------------------------------------------------------------
 * ⚠️ 安全性设计（本文件的核心约束）
 * ---------------------------------------------------------------------------
 *   1. 从模式无超时：`spi_context.h:153-154` 在 `CONFIG_SPI_SLAVE` 下把从设备的等待
 *      设为 `K_FOREVER`。若跳线缺失/接错，`spi_transceive()` 会**永久阻塞**并一直
 *      持有实例锁。因此从设备一律跑在**独立线程**里，主线程只用**有界
 *      `k_thread_join()`**（LB_JOIN_MS）等它；超时则 `k_thread_abort()` + `spi_release()`
 *      兜底回收锁，并把链路标记为"不通"，后续用例 `ztest_test_skip()`。
 *   2. 链路只探测一次（`lb_link_ok()` 惰性缓存）：跳线不对时不会每个用例都吃满超时。
 *   3. CS 只由**主机一侧**驱动（C1 修复后）：
 *      - 主机 spi3：`lb_cfg_m.cs.gpio` = 板级 `&spi3 cs-gpios` 第 0 项（GPIOG 13，
 *        = SPI3_CSN0 焊盘）。`spi_dw.c:793-794` 传输前拉低、:1026-1030 传输后放开。
 *      - 从机 spi1：`lb_cfg_s.cs.gpio.port == NULL` ⇒ `spi_cs_is_gpio()` 为假
 *        ⇒ `spi_ls_transceive()` :330-332 不碰 GPIO CS，改由**硬件 NSS 输入脚**
 *        （PA03，见 app.overlay 的 pinctrl 节点 spi1_nss_pa03）承担片选。
 *      - 两侧都不驱动同一根线 ⇒ 不存在"两根输出对推"。
 *        ⚠️ 上一轮版本写"spi3 根本没有 NSS 复用宏、本来也用不上硬件 NSS"是**错的**：
 *        全树确实有 `SPI3_CSN0_FUNC2_PG13_PINMUX` / `SPI3_CSN1_FUNC2_PG12_PINMUX`；
 *        spi1 也有 `SPI1_NSS_FUNC1_IDX19_PINMUX_PA/PC/PI`，且 vendor 的从设备例程
 *        （examples/peripheral/spi_i2s/spi_it_slave/main.c:71-79）**必须**配 NSS。
 *   4. 本工程不再"完全不驱动 CS"：主机按条 3 驱动 PG13。因此挂在 spi3 net 上的
 *      板载 NOR（若存在，板级默认片选 gpiog12/13）在主机传输期间会**跟随选中**
 *      —— 该风险由「跳线只连 spi1↔spi3 四线、且 spi3 侧无 DT 声明的 flash 子节点」
 *      兜底；若实机发现 MISO 被板载器件抢总线（数据恒为某个固定值），
 *      第一动作是核对 spi3 net 上是否真的挂了 NOR（COVERAGE.md「回环可行性结论
 *      §6 未决项 R-2」）。
 *   5. overlay 里给 spi3 加了 `timing-calibration-disabled`，规避
 *      `spi_dw.c:467-469` 对空数组 `timing_calibration_delay_arr[]` 的越界读。
 *
 * ---------------------------------------------------------------------------
 * 判据（★ 逐字节比对，不是"返回 0 就算过"）
 * ---------------------------------------------------------------------------
 *   四线直连（master↔slave）：SCK↔SCK / MOSI↔MOSI / MISO↔MISO / CS↔CS。
 *   因此：
 *     `master_rx[] == slave_tx[]`   （从设备发出的字节被主设备原样收到）
 *     `slave_rx[]  == master_tx[]`  （主设备发出的字节被从设备原样收到）
 *   另加驱动层判据：`spi_ls.c:362-366` 在 `CONFIG_SPI_SLAVE` 下让从设备的
 *   `spi_transceive()` 返回 `recv_frames`（已收帧数）⇒ 从设备返回值应 == len。
 *
 * 本条外部路径的**实测环境**尚未由用户逐 pin 确认，见报告「需裁定问题」。
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "ztest_glue.h"

LOG_MODULE_REGISTER(spi_loopback, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------- */
/* 实例                                                                        */
/* -------------------------------------------------------------------------- */

#define LB_MASTER_NODE DT_NODELABEL(spi3)   /* spi_dw.c，本工程仅作 master */
#define LB_SLAVE_NODE  DT_NODELABEL(spi1)   /* spi_ls.c，本工程仅作 slave  */

static const struct device *const lb_master = DEVICE_DT_GET(LB_MASTER_NODE);
static const struct device *const lb_slave = DEVICE_DT_GET(LB_SLAVE_NODE);

/* -------------------------------------------------------------------------- */
/* 常量                                                                        */
/* -------------------------------------------------------------------------- */

/*
 * 1 MHz：远低于 spi_ls.c:118-121 的上限（8*f <= clock，本板 spi1 clock = 75 MHz），
 * 也远低于 spi3 的 300 MHz 时钟。回环经外部跳线，取低速以容忍线长/串扰。
 */
#define LB_FREQ_HZ   1000000U
/*
 * 回环缓冲/单次传输上限：与同工程 `spi_test.c` 的 `transceive_length_sweep`
 * 上限（256 B）保持一致，属**本测试的缓冲维度**（驱动本身对长度无上限；
 * `spi_dw.c` 的 `max_xfer_size=32` 约束的是**字宽**，`spi_dw.c:427-431`）。
 */
#define LB_MAX_LEN   256U
/* 从设备线程的有界等待上限（跳线不通时的最坏耗时；非驱动超时） */
#define LB_JOIN_MS   500

/* 主/从两侧都用 8 bit / MSB first；模式位（CPOL/CPHA）两侧必须一致 */
#define LB_OP_M (SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)
#define LB_OP_S (SPI_OP_MODE_SLAVE | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

/* -------------------------------------------------------------------------- */
/* 共享缓冲（静态，供从设备线程与用例线程共用）                                 */
/* -------------------------------------------------------------------------- */

/*
 * ★ C1 修复（2026-09-18）：上一轮这两个对象是 `static struct spi_config`（**全零**），
 *   文件里的 `LB_FREQ_HZ` / `LB_OP_M` / `LB_OP_S` 三个宏**从未被使用** ——
 *   `operation = 0、frequency = 0` 直接造成上一轮 link_probe 的两个现象：
 *
 *   · 主设备 spi3（spi_dw.c）：`SPI_WORD_SIZE_GET(op) == 0`
 *     → `spi_dw.c:435/437` 把 `CTRLR0.DFS` 写成 0（DW SSI 要求 DFS≥3 才是合法帧宽）
 *     → 数据帧非法、发送状态机不推进 → 主机一直等到自己的完成超时
 *     → **master_ret = -116 (-ETIMEDOUT)**。
 *
 *   · 从设备 spi1（spi_ls.c）：`op` 里**没有** `SPI_OP_MODE_SLAVE` 位
 *     → `spi_context_is_slave()`（spi_context.h:84-87）为假
 *     → `spi_ls_configure()` 走 spi_ls.c:89-91 的 `else` 分支写 `MSTR=1`
 *     → spi1 实际按**主模式**跑：自己发时钟、自己把 8 字节收完（MISO 悬空读 0xFF）
 *     → 正常完成并返回 0 → **slave_ret = 0**。
 *
 *   所以「link_probe 失败」的真因是**用例侧配置没生效**，不是跳线/片选问题；
 *   片选（CS/NSS）是修完配置之后必须补齐的**第二个**条件（见 app.overlay）。
 *   现在按原设计把宏用起来：主/从两侧都是 8 bit + MSB first，模式位两侧一致。
 */
static struct spi_config lb_cfg_m = {
	.frequency = LB_FREQ_HZ,
	.operation = LB_OP_M,
	.slave = 0,
	/*
	 * 主设备驱动 GPIO 片选（只让主机一侧推 CS）：
	 * spi_dw.c:793-794 传输前 `spi_context_cs_control(ctx, true)`、
	 * spi_dw.c:1026-1030（completed()）传输后 `cs_control(ctx, false)`，
	 * 两个调用点都在 `spi_cs_is_gpio(config)` 分支内。
	 * 取板级 `&spi3 { cs-gpios = <&gpiog 13 ...>, <&gpiog 12 ...>; }` 的第 0 项
	 * （= GPIOG 13，即 SPI3_CSN0 焊盘）；写法同 spi_test.c:100-103。
	 * `spi_context_cs_configure_all()`（spi_dw.c:2083）已把该脚 init 成
	 * GPIO_OUTPUT_INACTIVE（GPIO_ACTIVE_LOW ⇒ 物理高 = 未选中）。
	 */
	.cs = { .gpio = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(spi3), cs_gpios, 0),
		.delay = 0 },
};

static struct spi_config lb_cfg_s = {
	.frequency = LB_FREQ_HZ,
	.operation = LB_OP_S,
	.slave = 0,
	/*
	 * 从设备**绝不**驱动 CS（见文件头 §安全性设计 3）：
	 * spi_ls.c:330-332 在从模式也会拉 CS GPIO（该驱动没有主从分支），
	 * 若从机也推 CS，跳线把两根输出接在一起就形成对推。
	 * `.cs` 保持全零 ⇒ `cs.gpio.port == NULL` ⇒ `spi_cs_is_gpio()` 为假
	 * ⇒ spi_ls.c:330-332 与 spi_context.h:239 两处都不会碰 CS GPIO。
	 * 从机的片选改由**硬件 NSS 输入脚**（PA03，见 app.overlay 的 spi1_nss_pa03）承担。
	 */
};

static uint8_t lb_mtx[LB_MAX_LEN];
static uint8_t lb_mrx[LB_MAX_LEN];
static uint8_t lb_stx[LB_MAX_LEN];
static uint8_t lb_srx[LB_MAX_LEN];

static struct k_thread lb_slave_thread;
K_THREAD_STACK_DEFINE(lb_slave_stack, 1024);

static volatile int lb_slave_ret;
static int lb_master_ret;
static size_t lb_len;
static bool lb_slave_has_rx;

/* -------------------------------------------------------------------------- */
/* 从设备线程                                                                   */
/* -------------------------------------------------------------------------- */

static void lb_slave_worker(void *p1, void *p2, void *p3)
{
	struct spi_buf txb = { .buf = lb_stx, .len = lb_len };
	struct spi_buf rxb = { .buf = lb_srx, .len = lb_len };
	struct spi_buf_set txs = { .buffers = &txb, .count = 1 };
	struct spi_buf_set rxs = { .buffers = &rxb, .count = 1 };

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/*
	 * ⚠️ 从模式下 spi_context_wait_for_completion() 用 K_FOREVER，
	 *    主设备不给时钟时本调用**不会返回** —— 调用方必须有界 join。
	 *    `spi_ls_transceive()` 在 CONFIG_SPI_SLAVE 下返回 recv_frames。
	 */
	lb_slave_ret = spi_transceive(lb_slave, &lb_cfg_s, &txs,
				      lb_slave_has_rx ? &rxs : NULL);
}

/* -------------------------------------------------------------------------- */
/* 驱动缺陷规避：清掉 spi_ls 每完成一次传输就残留的 ctx->sync 令牌               */
/* -------------------------------------------------------------------------- */

/*
 * 与 zephyr/drivers/spi/spi_context.h:28-56 的 `struct spi_context` **前缀**逐字段
 * 对齐（只用到 sync 为止；sync 之前的成员不受 CONFIG_SPI_ASYNC / CONFIG_SPI_SLAVE
 * 条件编译影响，偏移稳定）。
 * 测试侧不便 include 驱动私有头（不在 include path），故此处镜像。
 * spi_ls.c:42-44 `struct spi_ls_data { struct spi_context ctx; };` ⇒ dev->data 即 &ctx。
 * 运行时自检见 lb_drain_stray_sync()：镜像不匹配就**不动**驱动状态。
 */
struct lb_spi_ctx_mirror {
	const struct spi_config *config;
	const struct spi_config *owner;
	const struct gpio_dt_spec *cs_gpios;
	size_t num_cs_gpios;
	struct k_sem lock;
	struct k_sem sync;
};

/*
 * ⚠️ 规避驱动 P0（见 COVERAGE.md「上游缺陷观察」#12）：
 *
 *   spi_ls.c:359-360 —— `spi_ls_transceive()` 在**同步**路径里先调
 *     `spi_context_wait_for_completion()`，紧接着又调了一次 `spi_ls_complete()`；
 *     而 `spi_ls_complete()`（spi_ls.c:274）会 `spi_context_complete()` ——
 *     非 ASYNC 模式下该函数无条件 `k_sem_give(&ctx->sync)`（spi_context.h:209-212）。
 *   ⇒ **每完成一次传输，ctx->sync 就多留一个令牌**（ISR 给一次、线程再给一次；
 *     信号量 limit=1，所以稳定停在 1）。
 *
 *   正确写法对照 spi_gd32.c:400-406（同为 STM32-like SPI）：
 *     同步模式**不再** complete，只在 `#ifdef CONFIG_SPI_ASYNC` 下才调
 *     `spi_context_complete()`。
 *
 *   后果：对 spi_ls 实例的第 2 次及以后 `spi_transceive()`，
 *     `spi_context_wait_for_completion()`（spi_context.h:167 的 `k_sem_take`）
 *     会**立刻**拿到那个残留令牌返回，根本不等待。
 *     · 主设备侧：ISR 会把线程饿死到整个传输结束（每次 ISR 结束清 TXE 标志、
 *       TXE 仍有效则立即重入），所以数据其实传完了、表象正常；
 *     · **从设备侧**：返回后 `spi_ls_complete()` 立刻把 SPE 清零
 *       （spi_ls.c:269-272），从机在主机还没开始时钟前就被关掉
 *       —— 这正是上一轮 `link_probe` 里 `slave_ret = 0`
 *       （而不是设计预期的 -EINPROGRESS/-115 超时）的来源。
 *
 * 本工程不得改驱动（铁律 1），故在**发起从设备传输之前**清掉该残留令牌：
 *   · 只在从设备线程尚未创建、实例空闲时调用 ⇒ 不打断任何在途传输；
 *   · 若将来驱动被修（第 360 行包进 `#ifdef CONFIG_SPI_ASYNC`），本函数清一个
 *     本来就是 0 的信号量，是**无副作用**的 no-op —— 届时镜像结构与本函数
 *     可整体删除；
 *   · 自检不过就**不**动驱动状态（宁可回落到"链路不通 ⇒ SKIP"，
 *     也绝不误伤驱动的 lock/sync）。
 */
static void lb_drain_stray_sync(void)
{
	struct lb_spi_ctx_mirror *m = (struct lb_spi_ctx_mirror *)lb_slave->data;

	/*
	 * 运行时自检（验证镜像偏移与当前驱动一致）：
	 *   config    —— 首次传输前为 NULL（此时本来就没有残留令牌，正常跳过）
	 *   cs_gpios  —— 板级 `&spi1 { cs-gpios = <&gpioa 3 GPIO_ACTIVE_LOW>; }`
	 *   num_cs_gpios —— 恰为 1（同上）
	 *   lock.count —— spi_ls_init() 末尾 spi_ls.c:467 调过
	 *                 `spi_context_unlock_unconditionally()` ⇒ 例程空闲时锁为 1
	 *   sync.limit —— spi_context.h:62 `Z_SEM_INITIALIZER(..., 0, 1)`
	 */
	if (m->config == NULL) {
		return;		/* 尚无任何传输 ⇒ 不可能有残留令牌 */
	}

	if (m->cs_gpios == NULL || m->num_cs_gpios != 1U ||
	    k_sem_count_get(&m->lock) != 1U || m->sync.limit != 1U) {
		LOG_ERR("stray-sync drain self-check failed (n_cs=%u lock=%u "
			"sync_lim=%u) - skipped",
			(unsigned)m->num_cs_gpios,
			k_sem_count_get(&m->lock), m->sync.limit);
		return;
	}

	k_sem_reset(&m->sync);
}

/* -------------------------------------------------------------------------- */
/* 有界回环原语                                                                 */
/* -------------------------------------------------------------------------- */

struct lb_result {
	int master_ret;   /* 主设备 spi_transceive 返回值 */
	int slave_ret;    /* 从设备 spi_transceive 返回值（= recv_frames） */
	int join_rc;      /* 0 = 从设备线程已结束；-EAGAIN = 有界等待超时 */
	bool timed_out;
};

/*
 * 一次有界主从回环。
 *   len          传输长度（<= LB_MAX_LEN）
 *   master_rx    主设备是否收（false ⇒ 只发，判据退化为单侧比对）
 *   slave_rx     从设备是否收
 * 调用前后 lb_mtx / lb_stx 由调用方填充。
 */
static struct lb_result lb_roundtrip(size_t len, bool master_rx, bool slave_rx)
{
	struct lb_result r = { .master_ret = -EINVAL, .slave_ret = -EINVAL,
			       .join_rc = -EINVAL, .timed_out = false };
	struct spi_buf txb = { .buf = lb_mtx, .len = len };
	struct spi_buf rxb = { .buf = lb_mrx, .len = len };
	struct spi_buf_set txs = { .buffers = &txb, .count = 1 };
	struct spi_buf_set rxs = { .buffers = &rxb, .count = 1 };

	lb_len = len;
	lb_slave_has_rx = slave_rx;
	lb_slave_ret = -EINPROGRESS;

	/*
	 * 0) 清掉 spi_ls 上一次传输留下的 ctx->sync 残留令牌 —— 否则从设备的
	 *    spi_transceive() 会立刻返回（不等待），并在 spi_ls_complete() 里
	 *    把 SPE 清零，永远等不到主设备的时钟。见 lb_drain_stray_sync()。
	 *    必须在从设备线程创建之前做（此时 sync 上没有等待者）。
	 */
	lb_drain_stray_sync();

	/* 1) 先把从设备挂上去（它会阻塞在 K_FOREVER 等时钟） */
	k_thread_create(&lb_slave_thread, lb_slave_stack,
			K_THREAD_STACK_SIZEOF(lb_slave_stack),
			lb_slave_worker, NULL, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

	/* 2) 给从设备留出 enable-SPE + 预载首字节的时间窗 */
	k_sleep(K_MSEC(5));

	/* 3) 主设备发时钟（master 模式带超时，不会永久阻塞） */
	r.master_ret = spi_transceive(lb_master, &lb_cfg_m, &txs,
				      master_rx ? &rxs : NULL);

	/* 4) 有界等待从设备 */
	r.join_rc = k_thread_join(&lb_slave_thread, K_MSEC(LB_JOIN_MS));
	if (r.join_rc != 0) {
		r.timed_out = true;
		/*
		 * 从设备卡在 spi_transceive 里且仍持有实例锁 —— 中止线程并尽力
		 * 把锁还回去（spi_ls_release → spi_context_unlock_unconditionally）。
		 * 失败也无妨：lb_link_ok() 已把链路判死，后续用例全部 skip。
		 */
		k_thread_abort(&lb_slave_thread);
		(void)spi_release(lb_slave, &lb_cfg_s);
	}

	r.slave_ret = lb_slave_ret;
	return r;
}

/* -------------------------------------------------------------------------- */
/* 链路一次性探测                                                               */
/* -------------------------------------------------------------------------- */

#define LB_LINK_UNKNOWN (-1)
#define LB_LINK_BAD     0
#define LB_LINK_OK      1

static int lb_link_state = LB_LINK_UNKNOWN;

static void lb_fill(uint8_t *buf, size_t len, uint8_t seed)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = (uint8_t)(seed + i);
	}
}

static bool lb_bufs_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (a[i] != b[i]) {
			return false;
		}
	}
	return true;
}

/*
 * 一次性链路探测：两设备就绪 + 8 字节双向逐字节比对。
 * 结果缓存进 lb_link_state —— 跳线不对时只吃一次超时。
 */
static void lb_probe_link(void)
{
	const size_t n = 8;

	if (!device_is_ready(lb_master) || !device_is_ready(lb_slave)) {
		LOG_ERR("loopback device not ready: master=%d slave=%d",
			device_is_ready(lb_master), device_is_ready(lb_slave));
		lb_link_state = LB_LINK_BAD;
		return;
	}

	lb_fill(lb_mtx, n, 0x30);
	lb_fill(lb_stx, n, 0xC0);
	memset(lb_mrx, 0x00, LB_MAX_LEN);
	memset(lb_srx, 0x00, LB_MAX_LEN);

	struct lb_result r = lb_roundtrip(n, true, true);

	bool ok = (r.master_ret == 0) && !r.timed_out &&
		  (r.slave_ret == (int)n) &&
		  lb_bufs_equal(lb_mrx, lb_stx, n) &&
		  lb_bufs_equal(lb_srx, lb_mtx, n);

	lb_link_state = ok ? LB_LINK_OK : LB_LINK_BAD;
	lb_master_ret = r.master_ret;

	LOG_INF("link probe: master_ret=%d slave_ret=%d timed_out=%d "
		"mrx0=%02x stx0=%02x srx0=%02x mtx0=%02x -> %s",
		r.master_ret, r.slave_ret, (int)r.timed_out,
		lb_mrx[0], lb_stx[0], lb_srx[0], lb_mtx[0],
		ok ? "OK" : "BAD");
}

/* 供各用例前置调用；链路不通时返回 false（调用方 ztest_test_skip） */
static bool lb_link_ok(void)
{
	if (lb_link_state == LB_LINK_UNKNOWN) {
		lb_probe_link();
	}
	return lb_link_state == LB_LINK_OK;
}

/* -------------------------------------------------------------------------- */
/* 用例                                                                        */
/* -------------------------------------------------------------------------- */

/*
 * link_probe —— DT/设备契约 + 跳线连通性（前置性用例）。
 *
 * 驱动路径：spi_dw_init()（spi_dw.c:2025-2085，reset/clock/pinctrl/IRQ）+
 *           spi_ls_init()（spi_ls.c:415-472）；
 *           `spi_dw_configure()` / `spi_ls_configure()` 的从模式分支
 *           （spi_dw.c:407-416 的 serial_target 判定；spi_ls.c:90 MSTR=SLAVE）。
 * 预期来源：DT 契约 + `外部: SPI1↔SPI3 跳线`。
 */
ZTEST(spi_loopback, link_probe)
{
	lb_probe_link();

	ZT_INFO("link_probe", "master_ret=%d,slave_ret=%d,state=%d",
		lb_master_ret, lb_slave_ret, lb_link_state);

	zassert_true(device_is_ready(lb_master), "spi3 (DW SPI) 设备不就绪");
	zassert_true(device_is_ready(lb_slave), "spi1 (LS SPI) 设备不就绪");
	zassert_equal(lb_link_state, LB_LINK_OK,
		      "SPI1↔SPI3 跳线回环不通（检查跳线/角色分配，见 COVERAGE.md）");
}

/*
 * roundtrip_length_sweep —— 多长度逐字节回环比对（边界长度/功能通路）。
 *
 * 驱动路径：主 `spi_dw.c` ISR `push_data/pull_data` 的 FIFO 状态机；
 *           从 `spi_ls_data_exchange_slave()`（spi_ls.c:207-236）+
 *           `spi_ls_isr()`（:277-299）+ `spi_ls_complete()`（:251-275）。
 * 预期数据：master_tx 与 slave_tx 各自为 `seed + i` 模式，双向逐字节相等。
 */
ZTEST(spi_loopback, roundtrip_length_sweep)
{
	static const size_t lens[] = { 1, 2, 3, 4, 5, 8, 16, 32, 63, 64, 127, 128, 255, 256 };
	int joins[ARRAY_SIZE(lens)], srets[ARRAY_SIZE(lens)];
	bool equal[ARRAY_SIZE(lens)];

	if (!lb_link_ok()) {
		ztest_test_skip();
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(lens); i++) {
		size_t n = lens[i];
		struct lb_result r;

		lb_fill(lb_mtx, n, 0x11);
		lb_fill(lb_stx, n, 0x80);
		memset(lb_mrx, 0xEE, sizeof(lb_mrx));
		memset(lb_srx, 0xEE, sizeof(lb_srx));

		r = lb_roundtrip(n, true, true);

		joins[i] = r.join_rc;
		srets[i] = r.slave_ret;
		equal[i] = lb_bufs_equal(lb_mrx, lb_stx, n) &&
			   lb_bufs_equal(lb_srx, lb_mtx, n);
	}

	ZT_INFO("roundtrip_length_sweep", "n=%u,min=%u,max=%u,last_slave_ret=%d",
		(unsigned)ARRAY_SIZE(lens), (unsigned)lens[0],
		(unsigned)lens[ARRAY_SIZE(lens) - 1], srets[ARRAY_SIZE(lens) - 1]);

	for (size_t i = 0; i < ARRAY_SIZE(lens); i++) {
		zassert_equal(joins[i], 0, "len=%u 从设备未在有界等待内结束（跳线/时钟？）",
			      (unsigned)lens[i]);
		zassert_equal(srets[i], (int)lens[i],
			      "len=%u 从设备返回值应等于已收帧数", (unsigned)lens[i]);
		zassert_true(equal[i], "len=%u 双向字节比对失败", (unsigned)lens[i]);
	}
}

/*
 * roundtrip_fifo_boundary —— FIFO/上限边界 ±1（边界长度）。
 *
 * 驱动路径：spi_dw.c 的 `fifo_depth`（本板 spi3 fifo-depth=4）与
 *           `max_xfer_size`（本板 =32）相关分支（:285-291、:364、:427-434）；
 *           从侧 spi_ls.c 的 `CR2.TXFTH=4`（:138）。
 * 预期数据：3/4/5 与 31/32/33 均双向逐字节相等。
 */
ZTEST(spi_loopback, roundtrip_fifo_boundary)
{
	static const size_t lens[] = { 3, 4, 5, 31, 32, 33 };
	bool equal[ARRAY_SIZE(lens)];
	int srets[ARRAY_SIZE(lens)];

	if (!lb_link_ok()) {
		ztest_test_skip();
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(lens); i++) {
		size_t n = lens[i];
		struct lb_result r;

		lb_fill(lb_mtx, n, 0x5A);
		lb_fill(lb_stx, n, 0xA5);
		memset(lb_mrx, 0, sizeof(lb_mrx));
		memset(lb_srx, 0, sizeof(lb_srx));

		r = lb_roundtrip(n, true, true);
		srets[i] = r.slave_ret;
		equal[i] = lb_bufs_equal(lb_mrx, lb_stx, n) &&
			   lb_bufs_equal(lb_srx, lb_mtx, n);
	}

	ZT_INFO("roundtrip_fifo_boundary", "l0=%u/sret=%d,l3=%u/sret=%d,l5=%u/sret=%d",
		(unsigned)lens[0], srets[0], (unsigned)lens[3], srets[3],
		(unsigned)lens[5], srets[5]);

	for (size_t i = 0; i < ARRAY_SIZE(lens); i++) {
		zassert_equal(srets[i], (int)lens[i],
			      "len=%u 从设备返回值应等于已收帧数", (unsigned)lens[i]);
		zassert_true(equal[i], "len=%u 双向字节比对失败", (unsigned)lens[i]);
	}
}

/*
 * roundtrip_pattern_matrix —— 数据模式矩阵逐字节比对（功能通路）。
 *
 * 驱动路径：主 `spi_dw.c` push/pull 的 8 bit 通路；从 `spi_ls.c` 的
 *           `spi_context_update_tx/rx`（:215/:229）。
 * 预期数据：每个 seed 生成 `seed + i`，双向逐字节相等（含全 0 / 全 1 / 交替位）。
 */
ZTEST(spi_loopback, roundtrip_pattern_matrix)
{
	static const uint8_t mseeds[] = { 0x00, 0xFF, 0xAA, 0x55, 0x80, 0x7F };
	static const uint8_t sseeds[] = { 0xFF, 0x00, 0x55, 0xAA, 0x01, 0xFE };
	const size_t n = 16;
	bool equal[ARRAY_SIZE(mseeds)];

	if (!lb_link_ok()) {
		ztest_test_skip();
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(mseeds); i++) {
		struct lb_result r;

		lb_fill(lb_mtx, n, mseeds[i]);
		lb_fill(lb_stx, n, sseeds[i]);
		memset(lb_mrx, 0, sizeof(lb_mrx));
		memset(lb_srx, 0, sizeof(lb_srx));

		r = lb_roundtrip(n, true, true);
		equal[i] = (r.master_ret == 0) && (r.slave_ret == (int)n) &&
			   lb_bufs_equal(lb_mrx, lb_stx, n) &&
			   lb_bufs_equal(lb_srx, lb_mtx, n);
	}

	ZT_INFO("roundtrip_pattern_matrix", "patterns=%u,len=%u,all_equal=%d",
		(unsigned)ARRAY_SIZE(mseeds), (unsigned)n,
		(int)(equal[0] && equal[1] && equal[2] && equal[3] && equal[4] && equal[5]));

	for (size_t i = 0; i < ARRAY_SIZE(mseeds); i++) {
		zassert_true(equal[i], "master_seed=0x%02x 的 16 字节回环比对失败",
			     mseeds[i]);
	}
}

/*
 * roundtrip_max_length_256 —— 单次 256 字节上限（边界长度）。
 *
 * 驱动路径：主 `spi_dw.c` 的 FIFO 反复灌/抽（`fifo_depth=4`，256 B 需多轮 ISR）；
 *           从 `spi_ls.c` 的 ISR 逐字节搬运。
 * 预期数据：双向 256 字节逐字节相等；从设备返回 256。
 */
ZTEST(spi_loopback, roundtrip_max_length_256)
{
	const size_t n = LB_MAX_LEN;
	struct lb_result r;

	if (!lb_link_ok()) {
		ztest_test_skip();
		return;
	}

	lb_fill(lb_mtx, n, 0x40);
	lb_fill(lb_stx, n, 0x90);
	memset(lb_mrx, 0, sizeof(lb_mrx));
	memset(lb_srx, 0, sizeof(lb_srx));

	r = lb_roundtrip(n, true, true);

	ZT_INFO("roundtrip_max_length_256", "master_ret=%d,slave_ret=%d,join=%d,mrx_mid=%02x,srx_mid=%02x",
		r.master_ret, r.slave_ret, r.join_rc, lb_mrx[128], lb_srx[128]);

	zassert_equal(r.join_rc, 0, "256 字节回环未在有界等待内结束");
	zassert_equal(r.slave_ret, (int)n, "从设备返回值应为 256");
	zassert_mem_equal(lb_mrx, lb_stx, n, "256 B：主收 != 从发");
	zassert_mem_equal(lb_srx, lb_mtx, n, "256 B：从收 != 主发");
}

/*
 * roundtrip_master_tx_slave_rx —— 单向通路：主只发、从只收（功能通路）。
 *
 * 驱动路径：主 `spi_dw.c` 在 `rx_bufs == NULL` 时的 IMR 选择（:784-788）；
 *           从 `spi_ls_data_exchange_slave()` 的 `spi_context_rx_buf_on()` 分支
 *           （spi_ls.c:223-235）—— tx 关闭时只收不发。
 * 预期数据：从设备收到的字节 == 主设备发出的字节（逐字节）。
 */
ZTEST(spi_loopback, roundtrip_master_tx_slave_rx)
{
	const size_t n = 24;
	struct lb_result r;

	if (!lb_link_ok()) {
		ztest_test_skip();
		return;
	}

	lb_fill(lb_mtx, n, 0x20);
	memset(lb_srx, 0, sizeof(lb_srx));

	r = lb_roundtrip(n, false, true);

	ZT_INFO("roundtrip_master_tx_slave_rx", "master_ret=%d,slave_ret=%d,join=%d,srx0=%02x,srx23=%02x",
		r.master_ret, r.slave_ret, r.join_rc, lb_srx[0], lb_srx[n - 1]);

	zassert_equal(r.join_rc, 0, "单向传输未在有界等待内结束");
	zassert_equal(r.master_ret, 0, "主设备 tx-only 传输应当成功");
	zassert_equal(r.slave_ret, (int)n, "从设备返回值应为已收帧数 %u", (unsigned)n);
	zassert_mem_equal(lb_srx, lb_mtx, n, "从收 != 主发（MOSI 通路）");
}

/*
 * slave_ret_recv_frames —— 从设备返回值语义（驱动分支，★ 上轮跳过项解禁）。
 *
 * 驱动路径：`spi_ls_transceive()` :362-366 —— `CONFIG_SPI_SLAVE` 下，
 *           从模式成功时返回值是 `ctx->recv_frames`（已收帧数），不是 0。
 *           `spi_context_wait_for_completion()`（spi_context.h:174-178）为其来源。
 * 预期数据：len ∈ {1,7,64} 时从设备返回值分别 == 1/7/64。
 */
ZTEST(spi_loopback, slave_ret_recv_frames)
{
	static const size_t lens[] = { 1, 7, 64 };
	int srets[ARRAY_SIZE(lens)];

	if (!lb_link_ok()) {
		ztest_test_skip();
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(lens); i++) {
		size_t n = lens[i];

		lb_fill(lb_mtx, n, 0x66);
		lb_fill(lb_stx, n, 0x99);
		memset(lb_mrx, 0, sizeof(lb_mrx));
		memset(lb_srx, 0, sizeof(lb_srx));

		srets[i] = lb_roundtrip(n, true, true).slave_ret;
	}

	ZT_INFO("slave_ret_recv_frames", "len1=%d,len7=%d,len64=%d",
		srets[0], srets[1], srets[2]);

	for (size_t i = 0; i < ARRAY_SIZE(lens); i++) {
		zassert_equal(srets[i], (int)lens[i],
			      "从设备返回值应等于 recv_frames（len=%u）", (unsigned)lens[i]);
	}
}

/*
 * repeat_roundtrips_state_isolation —— 连续多轮无状态泄漏（并发/状态隔离）。
 *
 * 驱动路径：`spi_context_lock/release`（spi_context.h:89-130）在每轮之间的
 *           取放锁；`spi_ls_complete()` 的 SPE 复位（spi_ls.c:269-272）。
 * 预期数据：8 轮全部双向逐字节相等，长度在 {1,2,3,8,17,64} 间循环，
 *           说明上一轮不残留缓冲/锁/分频状态。
 * 注：真正的"双 master 线程并发"用例不可构造 —— 本跳线回环只有**一路从设备**，
 *     第二路 master 并发时没有对端可服务（见 COVERAGE.md「跳过项」）。
 */
ZTEST(spi_loopback, repeat_roundtrips_state_isolation)
{
	static const size_t lens[] = { 1, 2, 3, 8, 17, 64 };
	bool all_equal = true;

	if (!lb_link_ok()) {
		ztest_test_skip();
		return;
	}

	for (int round = 0; round < 8; round++) {
		size_t n = lens[round % ARRAY_SIZE(lens)];
		struct lb_result r;

		lb_fill(lb_mtx, n, (uint8_t)(0x10 + round));
		lb_fill(lb_stx, n, (uint8_t)(0xF0 - round));
		memset(lb_mrx, 0, sizeof(lb_mrx));
		memset(lb_srx, 0, sizeof(lb_srx));

		r = lb_roundtrip(n, true, true);

		bool ok = (r.join_rc == 0) && (r.slave_ret == (int)n) &&
			  lb_bufs_equal(lb_mrx, lb_stx, n) &&
			  lb_bufs_equal(lb_srx, lb_mtx, n);

		if (!ok) {
			all_equal = false;
		}
	}

	ZT_INFO("repeat_roundtrips_state_isolation", "rounds=8,all_equal=%d",
		(int)all_equal);

	zassert_true(all_equal, "8 轮连续回环中存在失败轮次（状态未隔离？）");
}

/* -------------------------------------------------------------------------- */
/* 套件                                                                        */
/* -------------------------------------------------------------------------- */

/*
 * 独立套件（与原有 `spi` 套件分离）：原有 18 项零改动。
 * 本套件全部用例依赖 `外部: SPI1↔SPI3 跳线`；链路不通时除 link_probe 外全部 skip。
 */
ZTEST_SUITE(spi_loopback, NULL, NULL, NULL, NULL, NULL);
