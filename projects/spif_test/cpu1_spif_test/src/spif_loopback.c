/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * spif_test 回环用例：spif1(master) + spif3(filter/passthrough) -> flash3_0。
 *
 * 参考资产：modules/lib/zephyr_pfr_tpm/samples/spi_monitor_filter/src/main.c
 *   - spif3 置 filter 态、flash_size=MB(32)、filter_enable、passthrough；
 *   - spif1 置 master 态，经其配对 spi3（CS0 = GPIOI 8）发裸 0x9F / 0x03；
 *   - spif3 在 spi4 侧做监视（in 侧外部接到 spif1/spi3 总线）。
 *
 * 本文件只读 flash，不擦写：JEDEC 读用于判定链路真的通了（不是全 0/全 FF），
 * 0x03 读用于覆盖参考资产的带地址命令形态。每项自带 setup/teardown，失败时
 * 也先把 spif1 切回 filter，避免污染后续用例。
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "ztest_glue.h"
#include "spif_test.h"

#include <spi_filter.h>

LOG_MODULE_REGISTER(spif_loopback, LOG_LEVEL_INF);

/* 参考资产的实例/地址选择：spif3=filter，spif1=master，flash3_0 在 spi3 CS0 */
#define LOOP_FILTER_IDX 2
#define LOOP_MASTER_IDX 0
#define LOOP_SIBLING_IDX 1
#define LOOP_READ_ADDR 0x4000u
#define LOOP_READ_LEN 8u
#define LOOP_FREQ MHZ(1)

static int loopback_setup(void)
{
	const struct device *filter = spif_dev(LOOP_FILTER_IDX);
	const struct device *master = spif_dev(LOOP_MASTER_IDX);
	const struct device *sibling = spif_dev(LOOP_SIBLING_IDX);
	int ret;

	ret = linkedsemi_spi_filter_cold_reset(filter);
	if (ret != 0) {
		return ret;
	}

	ret = spif_flash_size_set(filter, MB(32));
	if (ret != 0) {
		return ret;
	}

	spif_filter_enable(filter, true);
	if (!spif_pinctrl_filter_mode_check(filter)) {
		return -EIO;
	}

	ret = spif_passthrough_analog_mux_enable(filter, true);
	if (ret != 0) {
		return ret;
	}
	if (!spif_pinctrl_passthrough_mode_check(filter)) {
		return -EIO;
	}

	/* 参考资产只显式放行 0x03；JEDEC 0x9F 不在固定表默认项里，cold_reset
	 * 后其 EN=0，需要显式 add，否则 filter 会把 0x9F 当非法命令。 */
	ret = spif_add_cmd(filter, 0x9f);
	if (ret != 0) {
		return ret;
	}
	ret = spif_add_cmd(filter, 0x03);
	if (ret != 0) {
		return ret;
	}

	/* 参考资产调用 configure_address_protection(DISABLE) 清掉 0x4000..0x8000
	 * 的 allow 位；驱动语义里 DISABLE = 标记为非法，会在 filter 日志里打
	 * ADDR_ERR（流量仍可透传）。本用例验证链路而非保护错误，故显式 ENABLE
	 * 该区间，保持 cold_reset 默认的全片放行语义。 */
	ret = spif_address_privilege_config(filter, FLAG_ADDR_PRIV_READ_SELECT,
					    FLAG_ADDR_PRIV_ENABLE, LOOP_READ_ADDR,
					    KB(16));
	if (ret != 0) {
		return ret;
	}
	ret = spif_address_privilege_config(filter, FLAG_ADDR_PRIV_WRITE_SELECT,
					    FLAG_ADDR_PRIV_ENABLE, LOOP_READ_ADDR,
					    KB(16));
	if (ret != 0) {
		return ret;
	}

	/* spif1 的相邻实例 spif2 必须先回 filter，否则 master 切换 -EBUSY */
	ret = spif_switch_to_filter(sibling);
	if (ret != 0) {
		return ret;
	}
	ret = spif_switch_to_master(master);
	if (ret != 0) {
		return ret;
	}
	if (!spif_pinctrl_master_mode_check(master)) {
		return -EIO;
	}

	return 0;
}

static void loopback_teardown(void)
{
	(void)spif_switch_to_filter(spif_dev(LOOP_MASTER_IDX));
	(void)spif_remove_cmd(spif_dev(LOOP_FILTER_IDX), 0x9f);
	(void)spif_remove_cmd(spif_dev(LOOP_FILTER_IDX), 0x03);
	(void)spif_switch_to_filter(spif_dev(LOOP_FILTER_IDX));
}

static int loopback_xfer(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len)
{
	const struct device *master = spif_dev(LOOP_MASTER_IDX);
	const struct device *spi = spif_spi_dev(master);
	const struct spi_config cfg = {
		.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
		.frequency = LOOP_FREQ,
		.cs = { .gpio = *(spif_spi_cs(master)) },
	};
	const struct spi_buf tx_buf = { .buf = (void *)tx, .len = tx_len };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf rx_buf = { .buf = rx, .len = rx_len };
	const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	if (!device_is_ready(spi)) {
		return -ENODEV;
	}

	memset(rx, 0, rx_len);
	return spi_transceive(spi, &cfg, &tx_set, &rx_set);
}

ZTEST(spif, loopback_jedec)
{
	static const uint8_t tx[] = { 0x9f };
	uint8_t rx[4] = { 0 };
	int ret;

	ret = loopback_setup();
	if (ret == 0) {
		ret = loopback_xfer(tx, sizeof(tx), rx, sizeof(rx));
	}
	loopback_teardown();

	ZT_INFO("loopback_jedec", "ret=%d,jedec=%02x%02x%02x", ret, rx[1], rx[2], rx[3]);
	zassert_ok(ret, "spif1 master JEDEC transceive failed (ret=%d)", ret);

	/* 空总线读回常是全 0 或全 FF；真实 NOR 的 JEDEC 不会两者都命中。 */
	zassert_false(rx[1] == 0x00 && rx[2] == 0x00 && rx[3] == 0x00,
		      "JEDEC read all zero (no flash / link down)");
	zassert_false(rx[1] == 0xff && rx[2] == 0xff && rx[3] == 0xff,
		      "JEDEC read all 0xff (no flash / link down)");
}

ZTEST(spif, loopback_read_addr)
{
	static const uint8_t tx[] = { 0x03, 0x00, 0x40, 0x00 }; /* 0x004000 */
	uint8_t rx[4 + LOOP_READ_LEN] = { 0 };
	int ret;

	ret = loopback_setup();
	if (ret == 0) {
		ret = loopback_xfer(tx, sizeof(tx), rx, sizeof(rx));
	}
	loopback_teardown();

	ZT_INFO("loopback_read_addr", "ret=%d,data=%02x%02x%02x%02x", ret, rx[4], rx[5],
		rx[6], rx[7]);
	zassert_ok(ret, "spif1 master 0x03 read transceive failed (ret=%d)", ret);
}

/*
 * 过滤矩阵：命令/地址 合法 × 合法/非法 四种组合。
 *
 * 驱动/HW 语义（用户 2026-10-29 明确）：
 *   命令合法 + 地址合法 → 无 ERROR
 *   命令合法 + 地址非法 → ADDR_ERR=1，CMD_ERR=0，ERROR_CMD=命令值，ERROR_ADDR=地址
 *   命令非法 + 地址合法 → CMD_ERR=1，ADDR_ERR=0，ERROR_CMD=命令值
 *   命令非法 + 地址非法 → CMD_ERR=1，ADDR_ERR=0（先查命令，命中即上报）
 *
 * 每项都从 loopback_setup() 的 cold_reset 开始，确保 SPIF_DMA_DATA 无上一项残留；
 * 结束后 teardown 恢复 master/filter 与命令表。
 */
static int loopback_filter_run(bool cmd_legal, bool addr_legal,
			       spif_dma_data_t *before, spif_dma_data_t *err)
{
	const struct device *filter = spif_dev(LOOP_FILTER_IDX);
	const uintptr_t base = spif_reg_base(LOOP_FILTER_IDX);
	static const uint8_t tx[] = { 0x03, 0x00, 0x40, 0x00 };
	uint8_t rx[4 + LOOP_READ_LEN] = { 0 };
	int ret;

	err->value = 0;
	before->value = 0;
	ret = loopback_setup();

	if (ret == 0 && !cmd_legal) {
		/* 只移 0x03：0x9F 仍在白名单，确保是命令过滤而不是链路断 */
		ret = spif_remove_cmd(filter, 0x03);
	}
	if (ret == 0 && !addr_legal) {
		/* cold_reset 默认全片 allow；清掉 0x4000..0x8000 的读 allow 位 */
		ret = spif_address_privilege_config(filter, FLAG_ADDR_PRIV_READ_SELECT,
						    FLAG_ADDR_PRIV_DISABLE, LOOP_READ_ADDR,
						    KB(16));
	}
	if (ret == 0) {
		spif_intr_clr_t clr = {
			.ERROR = 1,
			.ERROR_OVERFLOW = 1,
			.BITMAP = 1,
		};

		/* 读前快照：cold_reset 后 SPIF_DMA_DATA 必须为 0，排除陈旧值假 PASS */
		before->value = sys_read32(base + SPIF_DMA_DATA);
		sys_write32(clr.value, base + SPIF_INTR_CLR);
		ret = loopback_xfer(tx, sizeof(tx), rx, sizeof(rx));
		k_msleep(5);   /* 等 filter ISR 采样 SPIF_DMA_DATA */
		err->value = sys_read32(base + SPIF_DMA_DATA);
	}

	loopback_teardown();
	return ret;
}

ZTEST(spif, loopback_filter_legal_both)
{
	spif_dma_data_t before = { 0 };
	spif_dma_data_t err = { 0 };
	int ret = loopback_filter_run(true, true, &before, &err);

	ZT_INFO("loopback_filter_legal_both",
		"before=%#x,ret=%d,raw=%#x,cmd_err=%d,addr_err=%d,por_addr=%d,error_cmd=%#x,error_addr=%#x",
		before.value, ret, err.value, err.CMD_ERR, err.ADDR_ERR, err.POR_ADDR,
		err.ERROR_CMD, err.ERROR_ADDR << 11);
	zassert_equal(before.value, 0, "SPIF_DMA_DATA must be clean after cold_reset");
	zassert_ok(ret, "legal cmd + legal addr transceive failed (ret=%d)", ret);
	zassert_equal(err.CMD_ERR, 0, "legal cmd must not set CMD_ERR");
	zassert_equal(err.ADDR_ERR, 0, "legal addr must not set ADDR_ERR");
	zassert_equal(err.POR_ADDR, 0, "no POR_ADDR expected");
	zassert_equal(err.ERROR_CMD, 0, "no ERROR_CMD expected");
	zassert_equal(err.ERROR_ADDR, 0, "no ERROR_ADDR expected");
}

ZTEST(spif, loopback_filter_legal_cmd_bad_addr)
{
	spif_dma_data_t before = { 0 };
	spif_dma_data_t err = { 0 };
	int ret = loopback_filter_run(true, false, &before, &err);

	ZT_INFO("loopback_filter_legal_cmd_bad_addr",
		"before=%#x,ret=%d,raw=%#x,cmd_err=%d,addr_err=%d,por_addr=%d,error_cmd=%#x,error_addr=%#x",
		before.value, ret, err.value, err.CMD_ERR, err.ADDR_ERR, err.POR_ADDR,
		err.ERROR_CMD, err.ERROR_ADDR << 11);
	zassert_equal(before.value, 0, "SPIF_DMA_DATA must be clean after cold_reset");
	zassert_ok(ret, "legal cmd + bad addr transceive failed (ret=%d)", ret);
	zassert_equal(err.CMD_ERR, 0, "legal cmd must not set CMD_ERR");
	zassert_equal(err.ADDR_ERR, 1, "bad addr must set ADDR_ERR");
	zassert_equal(err.POR_ADDR, 1, "read-address rejection must set POR_ADDR");
	zassert_equal(err.ERROR_CMD, 0x03, "ERROR_CMD must hold the legal command");
	zassert_equal(err.ERROR_ADDR << 11, LOOP_READ_ADDR,
		      "ERROR_ADDR must hold the blocked address");
}

ZTEST(spif, loopback_filter_bad_cmd_legal_addr)
{
	spif_dma_data_t before = { 0 };
	spif_dma_data_t err = { 0 };
	int ret = loopback_filter_run(false, true, &before, &err);

	ZT_INFO("loopback_filter_bad_cmd_legal_addr",
		"before=%#x,ret=%d,raw=%#x,cmd_err=%d,addr_err=%d,por_addr=%d,error_cmd=%#x,error_addr=%#x",
		before.value, ret, err.value, err.CMD_ERR, err.ADDR_ERR, err.POR_ADDR,
		err.ERROR_CMD, err.ERROR_ADDR << 11);
	zassert_equal(before.value, 0, "SPIF_DMA_DATA must be clean after cold_reset");
	zassert_ok(ret, "bad cmd + legal addr transceive failed (ret=%d)", ret);
	zassert_equal(err.CMD_ERR, 1, "bad cmd must set CMD_ERR");
	zassert_equal(err.ADDR_ERR, 0, "legal addr must not set ADDR_ERR");
	zassert_equal(err.POR_ADDR, 0, "no POR_ADDR on command error");
	zassert_equal(err.ERROR_CMD, 0x03, "ERROR_CMD must hold the blocked command");
	zassert_equal(err.ERROR_ADDR, 0, "ERROR_ADDR must be 0 when only cmd fails");
}

ZTEST(spif, loopback_filter_bad_cmd_bad_addr)
{
	spif_dma_data_t before = { 0 };
	spif_dma_data_t err = { 0 };
	int ret = loopback_filter_run(false, false, &before, &err);

	ZT_INFO("loopback_filter_bad_cmd_bad_addr",
		"before=%#x,ret=%d,raw=%#x,cmd_err=%d,addr_err=%d,por_addr=%d,error_cmd=%#x,error_addr=%#x",
		before.value, ret, err.value, err.CMD_ERR, err.ADDR_ERR, err.POR_ADDR,
		err.ERROR_CMD, err.ERROR_ADDR << 11);
	zassert_equal(before.value, 0, "SPIF_DMA_DATA must be clean after cold_reset");
	zassert_ok(ret, "bad cmd + bad addr transceive failed (ret=%d)", ret);
	zassert_equal(err.CMD_ERR, 1, "bad cmd must set CMD_ERR");
	/* 命令先查，命中后不再上报地址错误 */
	zassert_equal(err.ADDR_ERR, 0, "addr must not be checked after bad cmd");
	zassert_equal(err.POR_ADDR, 0, "no POR_ADDR after command-first rejection");
	zassert_equal(err.ERROR_CMD, 0x03, "ERROR_CMD must hold the blocked command");
	zassert_equal(err.ERROR_ADDR, 0, "ERROR_ADDR must be 0 after command-first rejection");
}


