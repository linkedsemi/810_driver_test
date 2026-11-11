/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * spi 套件 —— LinkedSemi SPI master 驱动用例（zephyr/drivers/spi/spi_ls.c）。
 * 由 spi_test 合并进 spi_spif_test（与 spi_flash / spif 套件同固件）。
 *
 * 实例范围（★ 见 projects/spi_test/tests/COVERAGE.md「实例范围与 Step-0 可行性结论」）：
 *   只有 `spi1` / `spi2` 的 compatible 是 `linkedsemi,ls-spi`，才由 spi_ls.c 驱动。
 *   `spi3` / `spi4` 是 `snps,designware-spi`（spi_dw.c）；pfr_tpm 的
 *   `samples/spi_master_communication` 用的是 `spif3 → spi4` 的 master 通路，
 *   与 spi_ls.c 无关（那条通路已由 P4 的 spif_test 覆盖）。
 *
 * 判据来源（★ 无总线对端）：
 *   spi1 / spi2 的引脚组（板级 pinctrl 已分配 SCK/MOSI/MISO + CS GPIO）上
 *   **没有任何 DT 声明的从设备** ⇒ 全部用例的预期值只能来自**驱动源码分支 +
 *   寄存器语义**，不依赖任何从设备的应答。SPI master 的 `spi_transceive()`
 *   完成性只取决于自身 TX/RX FIFO/中断状态机（`spi_context_wait_for_completion()`
 *   在 master 模式带超时，见 `zephyr/drivers/spi/spi_context.h:135-172`），
 *   故"传输完成"本身可判定；凡需要从设备参与的判据（数据回环/回显）一律不写，
 *   见 projects/spi_test/tests/COVERAGE.md「跳过项」。
 *
 * ⚠️ 已发现的上游缺陷（只记录不修改，详见 projects/spi_test/tests/COVERAGE.md「上游缺陷观察」）：
 *   1. `spi_ls_transceive()` 错误路径在**首次调用**（`ctx->config == NULL`）时经
 *      `spi_context_release()` 解引用 NULL → load access fault（P0）。
 *      因此本工程所有"非法配置 / 非法频率"用例都**先用一个合法传输把
 *      `ctx->config` 建立起来**，再触发错误路径。
 *   2. `spi_ls_configure()`（`spi_ls.c:64-70`）在 `spi_context_configured()` 早退
 *      **之前**就把 `SPE=0`、`BR ← scaler[0](=/8)` 写进硬件 ⇒ 用同一个
 *      `spi_config` 指针第二次调用时，实际波特率被静默改成 clock/8。
 *      用例 `reconfigure_same_cfg_prescaler` 刻意固化该行为。
 *   3. `spi_ls.c:123` 的分频搜索循环上界 `i <= ARRAY_SIZE(scaler)` 越界（数组长 6，
 *      `i` 可达 6）⇒ `config->frequency < clock>>8`（本板 < 292968 Hz）时读
 *      `scaler[6]` 并写入不可预测的 BR 值。预期不可确定，**不生成触发用例**。
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <string.h>

#include "ztest_glue.h"
#include "spi_spif_test.h"

#include <reg_spi_type.h>

LOG_MODULE_REGISTER(spi_test_case, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------- */
/* 常量：全部取自驱动源码 / HAL 头，不用"看起来像"的值                          */
/* -------------------------------------------------------------------------- */

/* spi1/spi2 的 clock-frequency = PBUS4_CLOCK = 75 MHz（lsqsh.dtsi:15,1184,1196） */
#define SPI_LS_CLOCK_HZ       75000000U
/* 驱动 spi_ls.c:118-121 的上限：8 * frequency <= clock_frequency */
#define SPI_LS_FREQ_MAX       (SPI_LS_CLOCK_HZ / 8U)      /* 9375000 */
/* 驱动 spi_ls.c:123-129 分频搜索的最低安全点（再低即触发越界读，见文件头缺陷 3） */
#define SPI_LS_FREQ_MIN_SAFE  (SPI_LS_CLOCK_HZ / 256U)    /* 292968 */

/*
 * HAL 波特率分频编码（modules/hal/linkedsemi/hal_driver/api/ls_hal_spi_i2s.h:182-187）：
 *   SPI_BAUDRATEPRESCALER_N = n << SPI_CR1_BR_POS，n = 2..7 对应 /8../256。
 * 驱动的 scaler[] 数组（spi_ls.c:55-62）即按此顺序排列。
 */
#define SPI_LS_BR_DIV8        (2U << SPI_CR1_BR_POS)
#define SPI_LS_BR_DIV16       (3U << SPI_CR1_BR_POS)
#define SPI_LS_BR_DIV32       (4U << SPI_CR1_BR_POS)
#define SPI_LS_BR_DIV64       (5U << SPI_CR1_BR_POS)
#define SPI_LS_BR_DIV128      (6U << SPI_CR1_BR_POS)
#define SPI_LS_BR_DIV256      (7U << SPI_CR1_BR_POS)

/* HAL SPI_DATASIZE_8BIT / 16BIT（同上头文件 :155-156） */
#define SPI_LS_DS_8BIT        (0x7U << SPI_CR2_DS_POS)
#define SPI_LS_DS_16BIT       (0xFU << SPI_CR2_DS_POS)

/* 基础 operation：主模式 / 8 bit / MSB first（用例在此基础上叠加） */
#define SPI_LS_BASE_OP        (SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

/* -------------------------------------------------------------------------- */
/* 设备 / 寄存器定位                                                           */
/* -------------------------------------------------------------------------- */

static const struct device *const spi_devs[SPI_INST_NUM] = {
	DEVICE_DT_GET(DT_NODELABEL(spi1)),
	DEVICE_DT_GET(DT_NODELABEL(spi2)),
};

static const uintptr_t spi_bases[SPI_INST_NUM] = {
	DT_REG_ADDR(DT_NODELABEL(spi1)),
	DT_REG_ADDR(DT_NODELABEL(spi2)),
};

/*
 * CS 引脚：板级 pinctrl 已为 spi1/spi2 声明 cs-gpios
 * （lsqsh_evb_pinctrl.dtsi:382 → gpioa 3；:388 → gpioj 15）。
 */
static const struct gpio_dt_spec spi_cs_gpios[SPI_INST_NUM] = {
	GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(spi1), cs_gpios, 0),
	GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(spi2), cs_gpios, 0),
};

const struct device *spi_dev(int idx)
{
	return (idx >= 0 && idx < SPI_INST_NUM) ? spi_devs[idx] : NULL;
}

uintptr_t spi_reg_base(int idx)
{
	return (idx >= 0 && idx < SPI_INST_NUM) ? spi_bases[idx] : 0;
}

static reg_spi_t *spi_reg(int idx)
{
	return (reg_spi_t *)(uintptr_t)spi_reg_base(idx);
}

/* 回读 CR1/CR2 的字段（掩码来自 reg_spi_type.h 的 enum，不用猜位宽） */
static uint32_t spi_cr1_field(int idx, uint32_t mask)
{
	return spi_reg(idx)->CR1 & mask;
}

static uint32_t spi_cr2_field(int idx, uint32_t mask)
{
	return spi_reg(idx)->CR2 & mask;
}

/*
 * 构造 spi_config。
 * ★ 返回的对象必须存进**静态**槽位再用 —— 驱动把 `const struct spi_config *`
 *   保存在 `ctx->config`（spi_ls.c:140），指针在整个用例生命周期内都需有效。
 */
static struct spi_config spi_mkcfg(int idx, uint32_t freq, uint16_t op)
{
	struct spi_config cfg = {
		.frequency = freq,
		.operation = op,
		.slave = 0,
		.cs = { .gpio = spi_cs_gpios[idx], .delay = 0 },
	};

	return cfg;
}

/* -------------------------------------------------------------------------- */
/* 传输辅助                                                                    */
/* -------------------------------------------------------------------------- */

/*
 * 单次 tx-only 传输，返回驱动返回值。
 * 注意：不做任何断言 —— 调用方收集完返回值再断言，避免 zassert 失败中途跳出
 * 导致实例锁（ctx->lock）泄漏、污染后续用例。
 */
static int spi_tx_once(int idx, const struct spi_config *cfg, uint8_t *buf, size_t len)
{
	struct spi_buf tx = { .buf = buf, .len = len };
	struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };

	return spi_transceive(spi_dev(idx), cfg, &tx_set, NULL);
}

/*
 * 建立一个"合法配置态"：让 `ctx->config` 非 NULL。
 *
 * 必要前置（文件头缺陷 1）：spi_ls_transceive() 的配置错误路径会经
 * `spi_context_release()` 解引用 `ctx->config`；首调时它是 NULL → load access
 * fault（整个固件挂死、串口静默）。任何要触发配置错误的用例都必须先调本函数。
 */
static int spi_arm_instance(int idx)
{
	static struct spi_config arm_cfg[SPI_INST_NUM];
	uint8_t dummy = 0;

	arm_cfg[idx] = spi_mkcfg(idx, 1500000, SPI_LS_BASE_OP);

	return spi_tx_once(idx, &arm_cfg[idx], &dummy, 1);
}

/* -------------------------------------------------------------------------- */
/* 用例                                                                        */
/* -------------------------------------------------------------------------- */

/*
 * device_ready —— DT 契约：两个实例都可就绪；基址与 DT 一致；CS GPIO 已声明且端口就绪。
 *
 * 驱动路径：`LS_SPI_INIT`（spi_ls.c:474-503）→ `DEVICE_DT_INST_DEFINE`；
 *           `spi_ls_init()`（:415-472）→ `spi_context_cs_configure_all()`（:462）
 *           —— CS GPIO 端口不就绪时 init 返回 -ENODEV，设备静默不可用。
 */
ZTEST(spi, device_ready)
{
	int r0 = device_is_ready(spi_dev(0)) ? 0 : 1;
	int r1 = device_is_ready(spi_dev(1)) ? 0 : 1;

	ZT_INFO("device_ready", "spi1_base=0x%x,spi2_base=0x%x,cs1_pin=%u,cs2_pin=%u",
		(unsigned)spi_reg_base(0), (unsigned)spi_reg_base(1),
		spi_cs_gpios[0].pin, spi_cs_gpios[1].pin);

	zassert_equal(r0, 0, "spi1 device not ready (CS GPIO 端口未使能?)");
	zassert_equal(r1, 0, "spi2 device not ready (CS GPIO 端口未使能?)");

	zassert_equal((unsigned long)spi_reg_base(0), 0x4008a000UL, "spi1 base != DT reg");
	zassert_equal((unsigned long)spi_reg_base(1), 0x4008a400UL, "spi2 base != DT reg");

	zassert_equal(DT_PROP_LEN(DT_NODELABEL(spi1), cs_gpios), 1, "spi1 cs-gpios 数不是 1");
	zassert_equal(DT_PROP_LEN(DT_NODELABEL(spi2), cs_gpios), 1, "spi2 cs-gpios 数不是 1");

	zassert_true(gpio_is_ready_dt(&spi_cs_gpios[0]), "spi1 CS GPIO 端口不就绪");
	zassert_true(gpio_is_ready_dt(&spi_cs_gpios[1]), "spi2 CS GPIO 端口不就绪");
}

/*
 * configure_mode_bits —— 配置位回读（功能通路）。
 *
 * 驱动路径：`spi_ls_configure()`（:46-144）
 *   MSTR（:84） / LSBFIRST（:93-97） / DS（:100-104） / CPOL（:106-110）
 *   / CPHA（:112-116） / SSOE（:137） / TXFTH（:138）
 *   + `spi_ls_complete()` 的 `SPE=0`（:269-272，无 SPI_HOLD_ON_CS）
 * 预期来源：驱动源码写值 + reg_spi_type.h 位域。
 */
ZTEST(spi, configure_mode_bits)
{
	static struct spi_config cfg;
	static uint8_t dummy = 0;
	int ret;
	uint32_t cr1, cr2;

	cfg = spi_mkcfg(0, 4687500, SPI_LS_BASE_OP | SPI_MODE_CPOL | SPI_MODE_CPHA);

	ret = spi_tx_once(0, &cfg, &dummy, 1);

	cr1 = spi_reg(0)->CR1;
	cr2 = spi_reg(0)->CR2;

	ZT_INFO("configure_mode_bits", "ret=%d,cr1=0x%x,cr2=0x%x", ret, cr1, cr2);

	zassert_equal(ret, 0, "合法配置的 1 字节传输应当成功");
	zassert_equal(cr1 & SPI_CR1_MSTR_MASK, SPI_CR1_MSTR_MASK, "CR1.MSTR 应为 master");
	zassert_equal(cr1 & SPI_CR1_CPOL_MASK, SPI_CR1_CPOL_MASK, "CR1.CPOL 应为 1");
	zassert_equal(cr1 & SPI_CR1_CPHA_MASK, SPI_CR1_CPHA_MASK, "CR1.CPHA 应为 1");
	zassert_equal(cr1 & SPI_CR1_LSBFIRST_MASK, 0U, "CR1.LSBFIRST 应为 0 (MSB)");
	zassert_equal(cr1 & SPI_CR1_BR_MASK, SPI_LS_BR_DIV16, "CR1.BR 应为 /16");
	zassert_equal(cr1 & SPI_CR1_SPE_MASK, 0U, "无 HOLD_ON_CS 时传输结束 SPE 应归 0");
	zassert_equal(cr2 & SPI_CR2_DS_MASK, SPI_LS_DS_8BIT, "CR2.DS 应为 8 bit");
	zassert_equal(cr2 & SPI_CR2_SSOE_MASK, SPI_CR2_SSOE_MASK, "CR2.SSOE 应为 1 (强制)");
	zassert_equal(cr2 & SPI_CR2_TXFTH_MASK, 4U << SPI_CR2_TXFTH_POS, "CR2.TXFTH 应为 4");
}

/*
 * configure_lsb_first —— LSB-first 分支（功能通路）。
 * 驱动路径：`spi_ls_configure()` :93-97（`SPI_TRANSFER_LSB` → SPI_FIRSTBIT_LSB）。
 * 用两个**不同的** spi_config 对象分别走两个分支（同指针会命中早退，见缺陷 2）。
 */
ZTEST(spi, configure_lsb_first)
{
	static struct spi_config cfg_lsb;
	static struct spi_config cfg_msb;
	static uint8_t dummy = 0;
	int ret_lsb, ret_msb;
	uint32_t lsb_bit, msb_bit;

	cfg_lsb = spi_mkcfg(1, 1500000,
			    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_LSB);
	cfg_msb = spi_mkcfg(1, 1500000, SPI_LS_BASE_OP);

	ret_lsb = spi_tx_once(1, &cfg_lsb, &dummy, 1);
	lsb_bit = spi_cr1_field(1, SPI_CR1_LSBFIRST_MASK);

	ret_msb = spi_tx_once(1, &cfg_msb, &dummy, 1);
	msb_bit = spi_cr1_field(1, SPI_CR1_LSBFIRST_MASK);

	ZT_INFO("configure_lsb_first", "ret_lsb=%d,lsb_bit=%u,ret_msb=%d,msb_bit=%u",
		ret_lsb, lsb_bit, ret_msb, msb_bit);

	zassert_equal(ret_lsb, 0, "LSB-first 传输应当成功");
	zassert_equal(lsb_bit, SPI_CR1_LSBFIRST_MASK, "SPI_TRANSFER_LSB 应置 CR1.LSBFIRST");
	zassert_equal(ret_msb, 0, "MSB-first 传输应当成功");
	zassert_equal(msb_bit, 0U, "默认 (MSB) 应清 CR1.LSBFIRST");
}

/*
 * configure_datasize16 —— 字宽分支（功能通路）。
 * 驱动路径：`spi_ls_configure()` :100-104（非 8 bit → SPI_DATASIZE_16BIT）；
 *           `spi_ls_transceive()` :324-328（16 bit → `spi_context_buffers_setup(..., 2)`）。
 * 预期来源：驱动源码写值（reg_spi_type.h SPI_CR2_DS）。
 */
ZTEST(spi, configure_datasize16)
{
	static struct spi_config cfg;
	static uint8_t tx[4] = { 0x12, 0x34, 0x56, 0x78 };
	int ret;
	uint32_t ds;

	cfg = spi_mkcfg(0, 1500000, SPI_OP_MODE_MASTER | SPI_WORD_SET(16) | SPI_TRANSFER_MSB);

	ret = spi_tx_once(0, &cfg, tx, sizeof(tx));

	ds = spi_cr2_field(0, SPI_CR2_DS_MASK);

	ZT_INFO("configure_datasize16", "ret=%d,ds=0x%x", ret, ds);

	zassert_equal(ds, SPI_LS_DS_16BIT, "SPI_WORD_SET(16) 应置 CR2.DS = 16 bit");
	zassert_equal(ret, 0, "16 bit 字宽传输应当成功");
}

/*
 * configure_prescaler_table —— 分频表逐档（边界长度/时序）。
 *
 * 驱动路径：`spi_ls_configure()` :123-129 的分频搜索循环 + scaler[]（:55-62）。
 * 语义：取满足 `clock >> (i+3) <= frequency` 的最小 i（不超过请求频率的最快档）。
 * 预期来源：驱动源码公式，本板 clock = 75 MHz（PBUS4_CLOCK）。
 * 每档用**独立的 spi_config 对象**，避免命中 `spi_context_configured()` 早退。
 */
ZTEST(spi, configure_prescaler_table)
{
	static struct spi_config cfgs[7];
	static const uint32_t freqs[7] = {
		9375000,   /* clock/8 精确上界 */
		5000000,
		3000000,
		1500000,
		800000,
		400000,
		292968,    /* clock>>8 精确下界（再低触发越界读，见文件头缺陷 3） */
	};
	static const uint32_t expect_br[7] = {
		SPI_LS_BR_DIV8, SPI_LS_BR_DIV16, SPI_LS_BR_DIV32,
		SPI_LS_BR_DIV64, SPI_LS_BR_DIV128, SPI_LS_BR_DIV256, SPI_LS_BR_DIV256,
	};
	static uint8_t dummy = 0;
	int rets[7];
	uint32_t brs[7];

	for (int i = 0; i < 7; i++) {
		cfgs[i] = spi_mkcfg(0, freqs[i], SPI_LS_BASE_OP);
		rets[i] = spi_tx_once(0, &cfgs[i], &dummy, 1);
		brs[i] = spi_cr1_field(0, SPI_CR1_BR_MASK);
	}

	ZT_INFO("configure_prescaler_table",
		"f0=%u/br=0x%x,f1=%u/br=0x%x,f2=%u/br=0x%x,f3=%u/br=0x%x,f4=%u/br=0x%x,f5=%u/br=0x%x,f6=%u/br=0x%x",
		freqs[0], brs[0], freqs[1], brs[1], freqs[2], brs[2], freqs[3], brs[3],
		freqs[4], brs[4], freqs[5], brs[5], freqs[6], brs[6]);

	for (int i = 0; i < 7; i++) {
		zassert_equal(rets[i], 0, "freq=%u 的传输应当成功", freqs[i]);
		zassert_equal(brs[i], expect_br[i], "freq=%u 的 CR1.BR 分频档不符", freqs[i]);
	}
}

/*
 * configure_freq_boundary —— 频率上限 ±1（边界长度/异常参数）。
 *
 * 驱动路径：`spi_ls_configure()` :118-121（`8 * frequency > clock_frequency`
 *           → -EINVAL）。预期来源：驱动源码不等式，本板 clock = 75 MHz。
 *
 * ⚠️ 缺陷 1 规避：-EINVAL 发生在 `ctx->config = config` 之前，若实例首调即走这里，
 *    `spi_context_release()` 会解引用 NULL。故先 `spi_arm_instance()`。
 * ⚠️ 副作用登记：该 -EINVAL 发生在 MSTR/DS/CPOL/CPHA 已写入之后（非原子失败）；
 *    本用例断言返回码与 SPE 终态，并把"已被半配置"作为观察上报。
 */
ZTEST(spi, configure_freq_boundary)
{
	static struct spi_config cfg_ok;
	static struct spi_config cfg_bad;
	static uint8_t dummy = 0;
	int arm, ret_ok, ret_bad;
	uint32_t br_ok, spe_after_bad;

	arm = spi_arm_instance(0);

	cfg_ok = spi_mkcfg(0, SPI_LS_FREQ_MAX, SPI_LS_BASE_OP);          /* clock/8，恰好合法 */
	cfg_bad = spi_mkcfg(0, SPI_LS_FREQ_MAX + 1, SPI_LS_BASE_OP);     /* 8*f > clock → -EINVAL */

	ret_ok = spi_tx_once(0, &cfg_ok, &dummy, 1);
	br_ok = spi_cr1_field(0, SPI_CR1_BR_MASK);

	ret_bad = spi_tx_once(0, &cfg_bad, &dummy, 1);
	spe_after_bad = spi_cr1_field(0, SPI_CR1_SPE_MASK);

	ZT_INFO("configure_freq_boundary", "arm=%d,ret_ok=%d,br_ok=0x%x,ret_bad=%d,spe_after_bad=%u",
		arm, ret_ok, br_ok, ret_bad, spe_after_bad);

	zassert_equal(arm, 0, "前置合法传输应当成功");
	zassert_equal(ret_ok, 0, "frequency == clock/8 应当被接受");
	zassert_equal(br_ok, SPI_LS_BR_DIV8, "clock/8 对应 CR1.BR = /8 档");
	zassert_equal(ret_bad, -EINVAL, "frequency > clock/8 应当 -EINVAL");
	zassert_equal(spe_after_bad, 0U, "失败的 configure 已把 SPE 清 0（非原子失败的观察）");
}

/*
 * transceive_null_bufs —— NULL 入参短路（异常参数）。
 * 驱动路径：`spi_ls_transceive()` :312-314（`!tx_bufs && !rx_bufs` → 直接 return 0，
 *           不经 lock/configure）。预期来源：驱动源码。
 * 旁证：因未 configure，`ctx->config` 不是本用例的 cfg ⇒ `spi_release()` 应 -EINVAL。
 */
ZTEST(spi, transceive_null_bufs)
{
	static struct spi_config cfg;
	int ret_null, ret_release;

	cfg = spi_mkcfg(0, 1500000, SPI_LS_BASE_OP);

	ret_null = spi_transceive(spi_dev(0), &cfg, NULL, NULL);
	ret_release = spi_release(spi_dev(0), &cfg);

	ZT_INFO("transceive_null_bufs", "ret_null=%d,ret_release=%d", ret_null, ret_release);

	zassert_equal(ret_null, 0, "tx/rx 均为 NULL 应直接返回 0");
	zassert_equal(ret_release, -EINVAL, "空传输不应把该 cfg 登记为已配置");
}

/*
 * transceive_length_sweep —— 长度扫描（边界长度）。
 *
 * 驱动路径：`spi_ls_transceive()` :324-328 → ISR 逐字节搬运
 *           `spi_ls_data_exchange_master()`（:168-205）→ `spi_context_update_tx/rx`。
 * 预期来源：驱动源码 + `spi_context_wait_for_completion()` 的 master 模式超时
 *           （`spi_context.h:160-164`，超时返回 -ETIMEDOUT 而非永久阻塞）。
 * 覆盖点：1 / 2 / 3（非对齐）/ 8 / 16 / 32 / 64 / 128 / 255 / 256。
 */
ZTEST(spi, transceive_length_sweep)
{
	static const size_t lens[] = { 1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256 };
	static struct spi_config cfgs[ARRAY_SIZE(lens)];
	static uint8_t tx[256];
	int rets[ARRAY_SIZE(lens)];
	uint32_t spe[ARRAY_SIZE(lens)];

	for (size_t i = 0; i < sizeof(tx); i++) {
		tx[i] = (uint8_t)(i ^ 0x5AU);
	}

	for (size_t i = 0; i < ARRAY_SIZE(lens); i++) {
		cfgs[i] = spi_mkcfg(0, 1500000, SPI_LS_BASE_OP);
		rets[i] = spi_tx_once(0, &cfgs[i], tx, lens[i]);
		spe[i] = spi_cr1_field(0, SPI_CR1_SPE_MASK);
	}

	ZT_INFO("transceive_length_sweep", "n=%u,min_len=%u,max_len=%u,last_ret=%d",
		(unsigned)ARRAY_SIZE(lens), (unsigned)lens[0],
		(unsigned)lens[ARRAY_SIZE(lens) - 1], rets[ARRAY_SIZE(lens) - 1]);

	for (size_t i = 0; i < ARRAY_SIZE(lens); i++) {
		zassert_equal(rets[i], 0, "len=%u 的传输应当成功", (unsigned)lens[i]);
		zassert_equal(spe[i], 0U, "len=%u 传输后 SPE 应归 0", (unsigned)lens[i]);
	}
}

/*
 * transceive_duplex_pair —— 全双工收发（功能通路）。
 *
 * 驱动路径：`spi_ls_transceive()` :337-343（rx_bufs 非空 → 先 RXNE 再被 TXE 覆盖）
 *           + `spi_ls_data_exchange_master()` 的 `spi_context_update_rx`。
 * 预期来源：驱动源码。**不对 RX 数据内容做断言** —— 总线上无从设备，
 *           MISO 悬空电平不确定（见 projects/spi_test/tests/COVERAGE.md「跳过项」），只判"完成"。
 */
ZTEST(spi, transceive_duplex_pair)
{
	static struct spi_config cfg;
	static uint8_t tx[32];
	static uint8_t rx[32];
	struct spi_buf txb = { .buf = tx, .len = sizeof(tx) };
	struct spi_buf rxb = { .buf = rx, .len = sizeof(rx) };
	struct spi_buf_set txs = { .buffers = &txb, .count = 1 };
	struct spi_buf_set rxs = { .buffers = &rxb, .count = 1 };
	int ret;

	cfg = spi_mkcfg(0, 1500000, SPI_LS_BASE_OP);

	for (size_t i = 0; i < sizeof(tx); i++) {
		tx[i] = (uint8_t)(0xA0U + i);
	}
	memset(rx, 0x5A, sizeof(rx));   /* 哨兵：驱动应逐字节覆盖成总线采样值 */

	ret = spi_transceive(spi_dev(0), &cfg, &txs, &rxs);

	ZT_INFO("transceive_duplex_pair", "ret=%d,rx0=%02x,rx1=%02x,rx31=%02x",
		ret, rx[0], rx[1], rx[sizeof(rx) - 1]);

	zassert_equal(ret, 0, "全双工 32 字节传输应当成功（-ETIMEDOUT 表示 ISR 链路未推进）");
}

/*
 * transceive_tx_only_rx_only —— 单向通路（功能通路）。
 *
 * 驱动路径：`spi_ls_data_exchange_master()` 的 `spi_context_tx_buf_on()`
 *           / `spi_context_rx_buf_on()` 两个分支（:175-204）：
 *           tx-only 时取缓冲内容；rx-only 时 tx_frame = 0。
 * 预期来源：驱动源码。
 */
ZTEST(spi, transceive_tx_only_rx_only)
{
	static struct spi_config cfg_tx;
	static struct spi_config cfg_rx;
	static uint8_t buf[8];
	struct spi_buf b = { .buf = buf, .len = sizeof(buf) };
	struct spi_buf_set only = { .buffers = &b, .count = 1 };
	int ret_tx, ret_rx;

	cfg_tx = spi_mkcfg(1, 1500000, SPI_LS_BASE_OP);
	cfg_rx = spi_mkcfg(1, 1500000, SPI_LS_BASE_OP);

	ret_tx = spi_transceive(spi_dev(1), &cfg_tx, &only, NULL);
	ret_rx = spi_transceive(spi_dev(1), &cfg_rx, NULL, &only);

	ZT_INFO("transceive_tx_only_rx_only", "ret_tx=%d,ret_rx=%d", ret_tx, ret_rx);

	zassert_equal(ret_tx, 0, "tx-only 传输应当成功");
	zassert_equal(ret_rx, 0, "rx-only 传输应当成功");
}

/*
 * error_half_duplex —— 半双工不支持（异常参数）。
 * 驱动路径：`spi_ls_configure()` :72-75（`SPI_HALF_DUPLEX` → -ENOTSUP）。
 * ⚠️ 缺陷 1 规避：先 `spi_arm_instance()` 建立 `ctx->config`。
 */
ZTEST(spi, error_half_duplex)
{
	static struct spi_config cfg;
	static uint8_t dummy = 0;
	int arm, ret;

	arm = spi_arm_instance(0);

	cfg = spi_mkcfg(0, 1500000, SPI_LS_BASE_OP | SPI_HALF_DUPLEX);
	ret = spi_tx_once(0, &cfg, &dummy, 1);

	ZT_INFO("error_half_duplex", "arm=%d,ret=%d", arm, ret);

	zassert_equal(arm, 0, "前置合法传输应当成功");
	zassert_equal(ret, -ENOTSUP, "SPI_HALF_DUPLEX 应当返回 -ENOTSUP");
}

/*
 * error_ti_mode —— TI 帧格式不支持（异常参数）。
 * 驱动路径：`spi_ls_configure()` :77-80（`SPI_FRAME_FORMAT_TI` → -ENOTSUP）。
 * ⚠️ 缺陷 1 规避：先 `spi_arm_instance()`。
 */
ZTEST(spi, error_ti_mode)
{
	static struct spi_config cfg;
	static uint8_t dummy = 0;
	int arm, ret;

	arm = spi_arm_instance(1);

	cfg = spi_mkcfg(1, 1500000, SPI_LS_BASE_OP | SPI_FRAME_FORMAT_TI);
	ret = spi_tx_once(1, &cfg, &dummy, 1);

	ZT_INFO("error_ti_mode", "arm=%d,ret=%d", arm, ret);

	zassert_equal(arm, 0, "前置合法传输应当成功");
	zassert_equal(ret, -ENOTSUP, "SPI_FRAME_FORMAT_TI 应当返回 -ENOTSUP");
}

/*
 * release_semantics —— release() 契约（异常参数 + 功能通路）。
 *
 * 驱动路径：`spi_ls_release()` :373-393
 *   - `!spi_context_configured()`（指针比较）→ -EINVAL（:380-382）
 *   - 已配置且 `SR.BSY == 0` → `spi_context_unlock_unconditionally` + `SPE=0`（:384-392）
 * 预期来源：驱动源码。
 */
ZTEST(spi, release_semantics)
{
	static struct spi_config cfg_unused;
	static struct spi_config cfg_used;
	static uint8_t dummy = 0;
	int ret_unused, ret_tx, ret_release;
	uint32_t spe;

	cfg_unused = spi_mkcfg(1, 1500000, SPI_LS_BASE_OP);
	cfg_used = spi_mkcfg(1, 1500000, SPI_LS_BASE_OP);

	ret_unused = spi_release(spi_dev(1), &cfg_unused);
	ret_tx = spi_tx_once(1, &cfg_used, &dummy, 1);
	ret_release = spi_release(spi_dev(1), &cfg_used);
	spe = spi_cr1_field(1, SPI_CR1_SPE_MASK);

	ZT_INFO("release_semantics", "ret_unused=%d,ret_tx=%d,ret_release=%d,spe=%u",
		ret_unused, ret_tx, ret_release, spe);

	zassert_equal(ret_unused, -EINVAL, "未配置过的 cfg 调 release 应 -EINVAL");
	zassert_equal(ret_tx, 0, "前置合法传输应当成功");
	zassert_equal(ret_release, 0, "已配置且空闲时 release 应返回 0");
	zassert_equal(spe, 0U, "release 后 SPE 应为 0");
}

/*
 * reconfigure_same_cfg_prescaler —— 同指针重复配置（★ 固化上游缺陷 2）。
 *
 * 驱动路径：`spi_ls_configure()` :64-70：先无条件 `SPE=0` + `BR ← scaler[0]`
 *           （= /8），**之后**才做 `spi_context_configured()` 早退判断。
 * 后果：用同一个 `spi_config` 指针第二次 `spi_transceive` 时，分频搜索循环被跳过，
 *      实际 BR 停在 /8，与请求频率无关。
 * 预期来源：驱动源码（本用例刻意固化该行为，避免回归时"悄悄修好"而无人察觉）。
 */
ZTEST(spi, reconfigure_same_cfg_prescaler)
{
	static struct spi_config cfg;
	static uint8_t dummy = 0;
	int ret1, ret2;
	uint32_t br_first, br_second, spe;

	cfg = spi_mkcfg(1, 400000, SPI_LS_BASE_OP);   /* 首次应得到 /256 */

	ret1 = spi_tx_once(1, &cfg, &dummy, 1);
	br_first = spi_cr1_field(1, SPI_CR1_BR_MASK);

	ret2 = spi_tx_once(1, &cfg, &dummy, 1);
	br_second = spi_cr1_field(1, SPI_CR1_BR_MASK);
	spe = spi_cr1_field(1, SPI_CR1_SPE_MASK);

	ZT_INFO("reconfigure_same_cfg_prescaler",
		"ret1=%d,br_first=0x%x,ret2=%d,br_second=0x%x,spe=%u",
		ret1, br_first, ret2, br_second, spe);

	zassert_equal(ret1, 0, "首次传输应当成功");
	zassert_equal(br_first, SPI_LS_BR_DIV256, "frequency=400k 首次应落到 /256 档");
	zassert_equal(ret2, 0, "同 cfg 第二次传输应当成功（早退路径）");
	zassert_equal(br_second, SPI_LS_BR_DIV8,
		      "同 cfg 二次调用后 BR 被静默重置为 /8（缺陷 2，固化）");
	zassert_equal(spe, 0U, "传输结束后 SPE 应为 0");
}

/*
 * hold_on_cs_spe —— SPI_HOLD_ON_CS 分支（功能通路）。
 *
 * 驱动路径：`spi_ls_complete()` :269-272 —— 带 `SPI_HOLD_ON_CS` 时不关 SPE；
 *           配合 `_spi_context_cs_control()`（spi_context.h:236-253）保持 CS 有效。
 * 预期来源：驱动源码 + spi_context 源码。
 * 用例末尾用一次普通传输把实例恢复到常规态（保证用例间独立）。
 */
ZTEST(spi, hold_on_cs_spe)
{
	static struct spi_config cfg_hold;
	static struct spi_config cfg_norm;
	static uint8_t dummy = 0;
	int ret_hold, ret_norm, cs_hold;
	uint32_t spe_hold, spe_norm;

	cfg_hold = spi_mkcfg(0, 1500000, SPI_LS_BASE_OP | SPI_HOLD_ON_CS);
	cfg_norm = spi_mkcfg(0, 1500000, SPI_LS_BASE_OP);

	ret_hold = spi_tx_once(0, &cfg_hold, &dummy, 1);
	spe_hold = spi_cr1_field(0, SPI_CR1_SPE_MASK);
	cs_hold = gpio_pin_get_dt(&spi_cs_gpios[0]);

	ret_norm = spi_tx_once(0, &cfg_norm, &dummy, 1);
	spe_norm = spi_cr1_field(0, SPI_CR1_SPE_MASK);

	ZT_INFO("hold_on_cs_spe", "ret_hold=%d,spe_hold=%u,cs_hold=%d,ret_norm=%d,spe_norm=%u",
		ret_hold, spe_hold, cs_hold, ret_norm, spe_norm);

	zassert_equal(ret_hold, 0, "HOLD_ON_CS 传输应当成功");
	zassert_equal(spe_hold, SPI_CR1_SPE_MASK, "SPI_HOLD_ON_CS 时 SPE 应保持 1");
	zassert_equal(ret_norm, 0, "恢复用普通传输应当成功");
	zassert_equal(spe_norm, 0U, "普通传输结束 SPE 应归 0");
}

/*
 * lock_on_hold_release —— SPI_LOCK_ON 持有/释放序列（并发/时序）。
 *
 * 驱动路径：`spi_context_lock()`（spi_context.h:89-109）的 `SPI_LOCK_ON` 短路分支
 *           + `spi_context_release()`（:111-130）的"持锁不释放"分支
 *           + `spi_ls_release()` :388 的 `spi_context_unlock_unconditionally`。
 * 预期来源：spi_context 源码契约。
 * ★ 锁安全：`spi_release()` 在任何断言之前调用，避免断言跳出时把锁泄漏给后续用例
 *   （泄漏会让本实例后续用例永久阻塞在 K_FOREVER 上）。
 */
ZTEST(spi, lock_on_hold_release)
{
	static struct spi_config cfg;
	static uint8_t dummy = 0;
	int ret1, ret2, ret_rel;

	cfg = spi_mkcfg(1, 1500000, SPI_LS_BASE_OP | SPI_LOCK_ON);

	ret1 = spi_tx_once(1, &cfg, &dummy, 1);
	ret2 = spi_tx_once(1, &cfg, &dummy, 1);
	ret_rel = spi_release(spi_dev(1), &cfg);

	ZT_INFO("lock_on_hold_release", "ret1=%d,ret2=%d,ret_rel=%d", ret1, ret2, ret_rel);

	zassert_equal(ret1, 0, "LOCK_ON 首次传输应当成功");
	zassert_equal(ret2, 0, "LOCK_ON 持锁期间第二次传输应当成功（lock 短路分支）");
	zassert_equal(ret_rel, 0, "spi_release 应当成功归还锁");
}

/*
 * concurrent_two_threads —— 两线程同时访问同一实例（并发）。
 *
 * 驱动路径：`spi_ls_transceive()` :316 `spi_context_lock()`（k_sem，K_FOREVER）
 *           → :369 `spi_context_release()`；两线程靠该信号量串行化。
 * 预期来源：驱动 + spi_context 的锁契约（两线程各自 ret == 0，无 -ETIMEDOUT）。
 * 两线程使用**各自独立的 spi_config 数组**（每轮不同指针），以走完整配置路径。
 */
#define CONC_ITER 4
#define CONC_LEN  16

static struct spi_config conc_cfgs_a[CONC_ITER];
static struct spi_config conc_cfgs_b[CONC_ITER];

struct conc_arg {
	struct spi_config *cfgs;
	int ret;
};

K_THREAD_STACK_DEFINE(conc_stack_a, 1024);
K_THREAD_STACK_DEFINE(conc_stack_b, 1024);
static struct k_thread conc_thread_a;
static struct k_thread conc_thread_b;

static void conc_worker(void *p1, void *p2, void *p3)
{
	struct conc_arg *arg = p1;
	uint8_t tx[CONC_LEN];

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (size_t i = 0; i < sizeof(tx); i++) {
		tx[i] = (uint8_t)i;
	}

	for (int i = 0; i < CONC_ITER; i++) {
		int r = spi_tx_once(1, &arg->cfgs[i], tx, sizeof(tx));

		if (r != 0 && arg->ret == 0) {
			arg->ret = r;
		}
	}
}

ZTEST(spi, concurrent_two_threads)
{
	static struct conc_arg arg_a = { .cfgs = conc_cfgs_a, .ret = 0 };
	static struct conc_arg arg_b = { .cfgs = conc_cfgs_b, .ret = 0 };
	int arm;

	/* 先建立 ctx->config，规避缺陷 1（错误路径 NULL 解引用）的窗口 */
	arm = spi_arm_instance(1);

	for (int i = 0; i < CONC_ITER; i++) {
		conc_cfgs_a[i] = spi_mkcfg(1, 1500000, SPI_LS_BASE_OP);
		conc_cfgs_b[i] = spi_mkcfg(1, 1500000, SPI_LS_BASE_OP);
	}

	k_thread_create(&conc_thread_a, conc_stack_a, K_THREAD_STACK_SIZEOF(conc_stack_a),
			conc_worker, &arg_a, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	k_thread_create(&conc_thread_b, conc_stack_b, K_THREAD_STACK_SIZEOF(conc_stack_b),
			conc_worker, &arg_b, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

	k_thread_join(&conc_thread_a, K_FOREVER);
	k_thread_join(&conc_thread_b, K_FOREVER);

	ZT_INFO("concurrent_two_threads", "arm=%d,ret_a=%d,ret_b=%d,iter=%d",
		arm, arg_a.ret, arg_b.ret, CONC_ITER);

	zassert_equal(arm, 0, "前置合法传输应当成功");
	zassert_equal(arg_a.ret, 0, "线程 A 的 %d 轮传输应当全部成功", CONC_ITER);
	zassert_equal(arg_b.ret, 0, "线程 B 的 %d 轮传输应当全部成功", CONC_ITER);
}

/*
 * post_reset_state —— 复位/掉电维度：复位终态为默认值，且能重新配置恢复。
 *
 * 驱动路径：`spi_ls_init()` :433-446 的 reset 路径（`reset_line_toggle`）—— 本用例
 *           在运行期由测试侧再做一次同样的复位，验证（a）复位终态确为默认值，
 *           （b）`spi_ls_configure()` 具备**全量**重配能力（不依赖残留寄存器）。
 * 预期来源：`reg_spi_type.h` 位域 + 驱动 configure 的写入集合。
 * ⚠️ 用全新 spi_config 指针做恢复传输，避免命中缺陷 2 的早退。
 */
ZTEST(spi, post_reset_state)
{
	static const struct reset_dt_spec rst = RESET_DT_SPEC_GET(DT_NODELABEL(spi1));
	static struct spi_config cfg;
	static uint8_t dummy = 0;
	int ret_rst, ret_recover;
	uint32_t cr1_after, br_after;

	ret_rst = reset_line_toggle_dt(&rst);

	cr1_after = spi_reg(0)->CR1;

	cfg = spi_mkcfg(0, 3000000, SPI_LS_BASE_OP);
	ret_recover = spi_tx_once(0, &cfg, &dummy, 1);
	br_after = spi_cr1_field(0, SPI_CR1_BR_MASK);

	ZT_INFO("post_reset_state", "ret_rst=%d,cr1_after=0x%x,ret_recover=%d,br_after=0x%x",
		ret_rst, cr1_after, ret_recover, br_after);

	zassert_equal(ret_rst, 0, "SPI1 复位线 toggle 应当成功");
	zassert_equal(cr1_after & SPI_CR1_SPE_MASK, 0U, "复位后 CR1.SPE 应为 0");
	zassert_equal(cr1_after & SPI_CR1_MSTR_MASK, 0U, "复位后 CR1.MSTR 应为 0");
	/* 该 IP 的 **复位默认档** 不是全 0：实测 cr1_after = 0x10，即 BR 字段 = 2 = /8。
	 * 复位确已生效的证据：MSTR 由复位前的 1 变为 0（本 suite 无任何从模式配置，
	 * 唯一写 MSTR=0 的 spi_ls.c:90 只在从模式分支，且本 suite 不配置从模式）。
	 * 旁证：驱动 spi_ls.c:65-66 每次 configure 开头都把 BR 写回 scaler[0]（也是 /8），
	 * 即作者本就以 /8 作安全默认档。详见 TRIAGE_MISC_2026-09-18.md §2。 */
	zassert_equal(cr1_after & SPI_CR1_BR_MASK, SPI_LS_BR_DIV8,
		      "复位后 CR1.BR 应为复位默认档 /8");
	zassert_equal(ret_recover, 0, "复位后可重新配置并完成传输");
	zassert_equal(br_after, SPI_LS_BR_DIV32, "恢复传输后 BR 应回到请求的 /32 档");
}

/* -------------------------------------------------------------------------- */
/* 套件                                                                        */
/* -------------------------------------------------------------------------- */

ZTEST_SUITE(spi, NULL, NULL, NULL, NULL, NULL);
