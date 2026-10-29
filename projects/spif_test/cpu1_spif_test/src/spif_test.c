/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * spif_test —— pfr_tpm SPI filter 驱动用例（drivers/pfr/spi/spi_filter.c）。
 *
 * 纯 ztest：每项自带前置（cold_reset），不依赖其他用例的执行顺序或残留状态。
 * 不同用例分派到不同 SPIF 实例，使实例级可变状态（`dev_data->flash_size` 等）
 * 互不干扰 —— 详见 COVERAGE.md「P4 生成说明」。
 *
 * 上游驱动缺陷（详见 tests/COVERAGE.md「上游缺陷观察」）：
 *   #1 `spif_add_cmd()` / `spif_add_cmd_with_dummy()` 的"命令在 **SW 固定表镜像**"
 *   分支曾用**过期 idx（-ENOSR = -63）**做寄存器写地址（spi_filter.c:884 / :938），
 *   造成表外寄存器写、且**目标槽 EN 保持 0**（功能完全失效）。
 *   **2026-09-18 已修**：写址改为固定表循环变量 `off`（`table_base + off * 4`）。
 *   `cmd_table_add_remove` 末尾的"固定表分支"段现已**直接调用** `spif_add_cmd()`
 *   并断言目标槽 `CMD` 回写、`EN==1`，作为该修复的**回归护栏**（修复前该段必 FAIL）。
 *   #20 `spif_add_cmd()` / `spif_add_cmd_with_dummy()` / `spif_add_general_cmd()` 的
 *   `spif_cmd_t` union **未清零**（spi_filter.c:881-883 等），DUMMY_CYCLE/reserved
 *   位为**栈垃圾** ⇒ 本工程一律**只断言 `CMD` 与 `EN` 两个字段**，不比对整字。
 *   该缺陷仍只记录不修改（属另一问题，不影响 CMD/EN 的确定性）。
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "ztest_glue.h"
#include "spif_test.h"

#include <spi_filter.h>
#include <reg_spi_filter.h>

LOG_MODULE_REGISTER(spif_test_case, LOG_LEVEL_INF);

/* pinctrl 状态编号由模块 dtsi 的 pinctrl-names 顺序决定（default/passthrough/master） */
#define SPIF_PINCTRL_STATE_MASTER 2

/* -------------------------------------------------------------------------- */
/* 设备 / 寄存器定位                                                           */
/* -------------------------------------------------------------------------- */

static const struct device *const spif_devs[SPIF_INST_NUM] = {
	DEVICE_DT_GET(DT_NODELABEL(spif1)),
	DEVICE_DT_GET(DT_NODELABEL(spif2)),
	DEVICE_DT_GET(DT_NODELABEL(spif3)),
	DEVICE_DT_GET(DT_NODELABEL(spif4)),
};

static const uintptr_t spif_bases[SPIF_INST_NUM] = {
	DT_REG_ADDR(DT_NODELABEL(spif1)),
	DT_REG_ADDR(DT_NODELABEL(spif2)),
	DT_REG_ADDR(DT_NODELABEL(spif3)),
	DT_REG_ADDR(DT_NODELABEL(spif4)),
};

const struct device *spif_dev(int idx)
{
	return (idx >= 0 && idx < SPIF_INST_NUM) ? spif_devs[idx] : NULL;
}

uintptr_t spif_reg_base(int idx)
{
	return (idx >= 0 && idx < SPIF_INST_NUM) ? spif_bases[idx] : 0;
}

static spif_cfg_t spif_read_cfg_at(int idx)
{
	spif_cfg_t cfg = { .value = sys_read32(spif_reg_base(idx) + SPIF_CFG) };

	return cfg;
}

/*
 * 固定表内是否已有该命令值。
 * 用途：general-cmd 用例选用的命令必须**不在**固定表内，否则
 * `spif_get_cmd_slot` 会先命中固定槽，测不到 general 槽的分配语义。
 */
static bool spif_cmd_in_fixed_table(const struct device *dev, uint8_t cmd)
{
	for (uint8_t i = 0; i < SPIF_FIXED_CMD_TABLE_NUM; i++) {
		uint8_t c = 0;

		spif_get_cmd_by_idx(dev, &c, i);
		if (c == cmd) {
			return true;
		}
	}
	return false;
}

/* 固定表 30 个槽位的 EN 位是否全为 0（cold_reset 的初始化语义） */
static int spif_fixed_enabled_count(const struct device *dev)
{
	uintptr_t base = 0;
	int n = 0;

	/* 由 dev 反查实例基址 */
	for (int i = 0; i < SPIF_INST_NUM; i++) {
		if (dev == spif_devs[i]) {
			base = spif_bases[i];
			break;
		}
	}
	for (uint8_t i = 0; i < SPIF_FIXED_CMD_TABLE_NUM; i++) {
		spif_cmd_t c = { .value = sys_read32(base + SPIF_CMD_BASE + i * 4) };

		if (c.EN) {
			n++;
		}
	}
	return n;
}

/* 清空 general 命令表（40 槽），幂等 */
static const uint8_t spif_general_pool[41] = {
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
	0xb0,   /* 第 41 个：用于验证 -ENOSR */
};

static void spif_general_pool_flush(const struct device *dev)
{
	for (int i = 0; i < (int)ARRAY_SIZE(spif_general_pool); i++) {
		(void)spif_remove_general_cmd(dev, spif_general_pool[i]);
	}
}

/* -------------------------------------------------------------------------- */
/* 用例                                                                        */
/* -------------------------------------------------------------------------- */

ZTEST(spif, device_ready)
{
	for (int i = 0; i < SPIF_INST_NUM; i++) {
		const struct device *dev = spif_dev(i);
		const struct gpio_dt_spec *cs;

		zassert_not_null(dev, "spif%d device pointer is NULL", i + 1);
		zassert_true(device_is_ready(dev), "spif%d not ready", i + 1);

		/* index 属性（DT index）→ 滤波实例编号 */
		zassert_equal(spif_get_ctrl_idx(dev), i, "spif%d ctrl idx", i + 1);

		/* 配对 SPI master（dtsi 的 spi 属性）与 CS GPIO */
		zassert_not_null(spif_spi_dev(dev), "spif%d spi dev NULL", i + 1);
		zassert_true(device_is_ready(spif_spi_dev(dev)),
			     "spif%d paired spi not ready", i + 1);
		cs = spif_spi_cs(dev);
		zassert_not_null(cs->port, "spif%d cs gpio port NULL", i + 1);
		zassert_true(gpio_is_ready_dt(cs), "spif%d cs gpio not ready", i + 1);
	}
}

ZTEST(spif, cold_reset_state)
{
	const struct device *dev = spif_dev(0);   /* spif1 */
	const uintptr_t base = spif_reg_base(0);
	spif_cfg_t cfg;
	spif_intr_t intr;

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");

	/* cold_reset 收尾的 SPIF_CFG 初值（spi_filter.c:1685-1693） */
	cfg = spif_read_cfg_at(0);
	zassert_equal(cfg.EN, 0, "CFG.EN must be 0 after cold_reset");
	zassert_equal(cfg.OPERATION_MODE, 0, "CFG.OPERATION_MODE (default = monitor)");
	zassert_equal(cfg.ADDR_3B_4B_SEL, 0, "CFG.ADDR_3B_4B_SEL (3-byte mode)");
	zassert_equal(cfg.TARGET_ADDR_MODE_SEL, 0, "CFG.TARGET_ADDR_MODE_SEL");
	zassert_equal(cfg.IP_LOCK, 1, "CFG.IP_LOCK (reg_unlock => 1, 命名反相)");
	zassert_equal(cfg.DMA_EN, 0, "CFG.DMA_EN");

	/* 目标地址与 bounding command range（spi_filter.c:1683-1684） */
	zassert_equal(sys_read32(base + SPIF_TARGET_ADDR), 0xffffffffu,
		      "SPIF_TARGET_ADDR after cold_reset");
	zassert_equal(sys_read32(base + SPIF_BCMD_RANGE), 0x7u,
		      "SPIF_BCMD_RANGE after cold_reset");

	/* 中断掩码：ERROR_OVERFLOW / ERROR 置位，TARGET_ADDR 清零（spi_filter.c:1702-1706） */
	intr.value = sys_read32(base + SPIF_INTR_MASK);
	zassert_equal(intr.ERROR_OVERFLOW, 1, "INTR_MASK.ERROR_OVERFLOW");
	zassert_equal(intr.ERROR, 1, "INTR_MASK.ERROR");
	zassert_equal(intr.TARGET_ADDR, 0, "INTR_MASK.TARGET_ADDR");

	/* 固定命令表 30 槽被写入 DT 默认命令值，且 EN 一律为 0（spi_filter.c:1676-1682） */
	zassert_equal(spif_fixed_enabled_count(dev), 0,
		      "cold_reset must leave all 30 fixed cmd slots disabled");

	{
		uint8_t cmd0 = 0;

		spif_get_cmd_by_idx(dev, &cmd0, 0);
		zassert_equal(cmd0, DT_PROP(DT_NODELABEL(spif1), cmd_page_program),
			      "fixed slot 0 must hold DT cmd-page-program");
		ZT_INFO("cold_reset_state", "cfg=%#x,target_addr=%#x,bcmd=%#x",
			cfg.value, sys_read32(base + SPIF_TARGET_ADDR),
			sys_read32(base + SPIF_BCMD_RANGE));
	}
}

ZTEST(spif, filter_enable_bit)
{
	const struct device *dev = spif_dev(0);
	spif_cfg_t cfg;

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");

	/* spif_enable / spif_filter_enable 都只改 SPIF_CFG.EN（spi_filter.c:1617 / :1171） */
	spif_enable(dev, true);
	cfg = spif_read_cfg_at(0);
	zassert_equal(cfg.EN, 1, "spif_enable(true) must set CFG.EN");

	spif_filter_enable(dev, false);
	cfg = spif_read_cfg_at(0);
	zassert_equal(cfg.EN, 0, "spif_filter_enable(false) must clear CFG.EN");

	spif_filter_enable(dev, true);
	cfg = spif_read_cfg_at(0);
	zassert_equal(cfg.EN, 1, "spif_filter_enable(true) must set CFG.EN");

	/* 复位回默认（cold_reset 终态 EN=0） */
	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "restore cold_reset");
}

ZTEST(spif, reg_lock_unlock)
{
	const struct device *dev = spif_dev(0);
	spif_cfg_t cfg;

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");

	/* ⚠️ 驱动 API 命名与其写入值反相（spi_filter.c:1187-1205）：
	 *   spif_reg_unlock() 写 IP_LOCK = 1
	 *   spif_reg_lock()   写 IP_LOCK = 0
	 * 本项断言的就是这对**实际寄存器语义**（命名反相已记录为上游观察项）。 */
	spif_reg_unlock(dev);
	cfg = spif_read_cfg_at(0);
	zassert_equal(cfg.IP_LOCK, 1, "spif_reg_unlock must write IP_LOCK=1");

	spif_reg_lock(dev);
	cfg = spif_read_cfg_at(0);
	zassert_equal(cfg.IP_LOCK, 0, "spif_reg_lock must write IP_LOCK=0");

	/* 恢复 cold_reset 终态（IP_LOCK=1） */
	spif_reg_unlock(dev);
	ZT_INFO("reg_lock_unlock", "unlock_ip_lock=%d,lock_ip_lock=%d", 1, 0);
}

ZTEST(spif, sck_threshold_config)
{
	const struct device *dev = spif_dev(0);
	const uintptr_t base = spif_reg_base(0);
	spif_sck_set_t sck;
	spif_sck_fqc_hi_t hi;
	spif_sck_fqc_lo_t lo;
	spif_intr_t intr;

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");

	/* SCK_DIV 为 3 bit 字段（reg_spi_filter.h:170-177）→ 上界 7 */
	spif_clk_check_config(dev, 5, 2, 100, true);

	sck.value = sys_read32(base + SPIF_SCK_SET);
	zassert_equal(sck.SCK_DIV, 5, "SPIF_SCK_SET.SCK_DIV");

	hi.value = sys_read32(base + SPIF_SCK_FQC_HI);
	zassert_equal(hi.SCK_FQC_HI, 2, "SPIF_SCK_FQC_HI");

	lo.value = sys_read32(base + SPIF_SCK_FQC_LO);
	zassert_equal(lo.SCK_FQC_LO, 100, "SPIF_SCK_FQC_LO");

	intr.value = sys_read32(base + SPIF_INTR_MASK);
	zassert_equal(intr.SCK_CHECK, 1, "INTR_MASK.SCK_CHECK (enable_intr=true)");

	spif_clk_check_config(dev, 0, 2, BIT(12) - 1, false);
	intr.value = sys_read32(base + SPIF_INTR_MASK);
	zassert_equal(intr.SCK_CHECK, 0, "INTR_MASK.SCK_CHECK (enable_intr=false)");

	ZT_INFO("sck_threshold_config", "sck_div=5,hi=2,lo=100");

	/* 恢复 cold_reset 默认 */
	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "restore cold_reset");
}

ZTEST(spif, addr_mode_config)
{
	const struct device *dev = spif_dev(3);   /* spif4 */
	spif_cfg_t cfg;

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");

	/* ADDR_3B_4B_SEL 是配置位（reg_spi_filter.h:172）；
	 * 注意 spif_addr_mode_peek() 返回的是 **ADDR_3B_4B_FLAG**（状态位，另一 bit），
	 * 故本项直接回读 SEL 位，不用 peek()。 */
	spif_4byte_mode_config(dev);
	cfg = spif_read_cfg_at(3);
	zassert_equal(cfg.ADDR_3B_4B_SEL, 1, "4byte mode must set ADDR_3B_4B_SEL");

	spif_3byte_mode_config(dev);
	cfg = spif_read_cfg_at(3);
	zassert_equal(cfg.ADDR_3B_4B_SEL, 0, "3byte mode must clear ADDR_3B_4B_SEL");

	ZT_INFO("addr_mode_config", "sel_4b=1,sel_3b=0");
}

ZTEST(spif, cmd_idx_accessors)
{
	const struct device *dev = spif_dev(1);   /* spif2 */
	uint8_t cmd = 0;
	uint8_t dummy = 0;

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");

	/* 固定表槽位按 idx 直接读写 CMD / DUMMY_CYCLE（spi_filter.c:960 / :1011 / :1041 / :1059） */
	spif_set_cmd_by_idx(dev, 0x77, 5);
	spif_get_cmd_by_idx(dev, &cmd, 5);
	zassert_equal(cmd, 0x77, "set/get cmd by idx roundtrip");

	/* 槽 5 = SPIF_READ_CMD(0x03)：SPI NOR 的普通 READ 没有 dummy 相，硬件
	 * 该槽 DUMMY_CYCLE[12:8] 实测恒 0（写 9 后原值仍 0x77）。这里只保留诊断，
	 * 不断言该字段；DUMMY 读写语义改用槽 6 = FAST_READ（有 dummy 相）验证。 */
	spif_set_dummy_by_idx(dev, 9, 5);
	{
		spif_cmd_t raw = { .value = sys_read32(spif_reg_base(1) + SPIF_CMD_BASE + 5 * 4) };

		spif_get_dummy_by_idx(dev, &dummy, 5);
		ZT_INFO("cmd_idx_accessors.slot5_read",
			"raw=%#x,cmd=%#x,raw_dummy=%#x,get_dummy=%#x (READ has no dummy phase)",
			raw.value, raw.CMD, raw.DUMMY_CYCLE, dummy);
	}

	/* 槽 6 = SPIF_READ_FAST_CMD(0x0b)：DT 默认 cmd-fast-read-dummy=8，硬件实现该字段。 */
	spif_set_dummy_by_idx(dev, 9, 6);
	{
		spif_cmd_t raw = { .value = sys_read32(spif_reg_base(1) + SPIF_CMD_BASE + 6 * 4) };

		spif_get_dummy_by_idx(dev, &dummy, 6);
		ZT_INFO("cmd_idx_accessors.slot6_fast_read",
			"raw=%#x,cmd=%#x,raw_dummy=%#x,get_dummy=%#x",
			raw.value, raw.CMD, raw.DUMMY_CYCLE, dummy);
		/* 报告按测试名匹配 INFO；给本用例一条与测试同名的汇总行 */
		ZT_INFO("cmd_idx_accessors", "slot5_cmd=%#x,slot5_dummy=0,slot6_raw=%#x,slot6_dummy=%#x",
			cmd, raw.value, dummy);
	}
	zassert_equal(dummy, 9, "set/get dummy by idx roundtrip (slot 6 FAST_READ)");

	/* replace_fixed_* 是同一组槽位的语义化包装 */
	spif_replace_fixed_cmd(dev, 0x99, 7);
	spif_get_cmd_by_idx(dev, &cmd, 7);
	zassert_equal(cmd, 0x99, "replace_fixed_cmd");

	spif_replace_fixed_dummy(dev, 3, 6);
	spif_get_dummy_by_idx(dev, &dummy, 6);
	zassert_equal(dummy, 3, "replace_fixed_dummy (slot 6 FAST_READ)");

	/* DUMMY_CYCLE 5 bit（reg_spi_filter.h:158-167）→ 掩码后回读；槽 6 支持该字段 */
	spif_set_dummy_by_idx(dev, 0xff, 6);
	spif_get_dummy_by_idx(dev, &dummy, 6);
	zassert_equal(dummy, 0xff & 0x1f, "dummy cycle is a 5-bit field");

	/* 复位回 DT 默认（cold_reset 用 dev_data->fixed_cmd_tab 重写固定表） */
	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "restore cold_reset");
}

ZTEST(spif, cmd_table_add_remove)
{
	const struct device *dev = spif_dev(1);
	const uint8_t cmd = 0xb0;

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");
	spif_general_pool_flush(dev);

	/* 走 general API 而非 spif_add_cmd：后者在"命令已存在于固定表"分支用过期
	 * idx（-ENOSR）作写地址，会写表外寄存器（spi_filter.c:884）。本项显式断言
	 * 选用的命令不在固定表内，确保不触发该缺陷。 */
	zassert_false(spif_cmd_in_fixed_table(dev, cmd),
		      "cmd %#x must not be in the fixed table (defect avoidance)", cmd);

	/* 固定表占位（fixed_cmd_tab）与槽位搜索都走 get_cmd_slot（spi_filter.c:356） */
	zassert_ok(spif_add_general_cmd(dev, cmd), "add general cmd %#x", cmd);
	zassert_true(spif_get_cmd_slot(dev, cmd, 0) >= 0, "cmd %#x must be present", cmd);

	zassert_ok(spif_remove_general_cmd(dev, cmd), "remove general cmd");
	zassert_equal(spif_get_general_cmd_slot(dev, cmd, 0), -ENOSR,
		      "cmd %#x must be gone after remove", cmd);

	/* 移除不存在的命令 → -EINVAL（spi_filter.c:1099-1102） */
	zassert_equal(spif_remove_general_cmd(dev, cmd), -EINVAL,
		      "removing an absent cmd must be -EINVAL");

	/* ---- 固定表分支：`spif_add_cmd()` 的 HW/SW 表不一致路径 ----
	 *
	 * 背景（缺陷 #1，2026-09-18 已修）：`spif_add_cmd()` 的第二个循环
	 * （"命令在 **SW 固定表镜像** `fixed_cmd_tab[]` 里"分支，spi_filter.c:879-889）
	 * 修复前用**过期 idx = -ENOSR** 算写地址，把 EN 写到表外寄存器、**目标槽
	 * EN 保持 0**（功能完全失效）。修后写址改为固定表循环变量 `off`
	 * （`table_base + off * 4`），该分支才真正能把目标槽 EN 置 1。
	 * 本段即钉住修复后的正确行为 —— 修复前这一段必 FAIL，故是**真断言**。
	 *
	 * 触发前置 = HW 命令表与 SW 镜像不一致。构造：`spif_set_cmd_by_idx()`
	 * 只改 HW 的 CMD、**不动** SW 镜像（spi_filter.c:964-981），把它写到某个
	 * 固定槽即可制造不一致。
	 *
	 * 只断言 CMD 与 EN：该分支的 `spif_cmd` union **未清零**
	 * （spi_filter.c:881-883，缺陷 #20），DUMMY_CYCLE/reserved 位是栈垃圾，
	 * 断言整个 32 位字会假 FAIL。
	 */
	{
		const uint8_t fixed_cmd = 0x03;   /* DT 默认 cmd-read */
		const uintptr_t base = spif_reg_base(1);
		spif_cmd_t slot;
		int fixed_idx = -1;

		zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset (fixed-table phase)");

		/* 定位 CMD==0x03 的固定槽（cold_reset 后 CMD = DT 默认、EN = 0） */
		for (uint8_t i = 0; i < SPIF_FIXED_CMD_TABLE_NUM; i++) {
			slot.value = sys_read32(base + SPIF_CMD_BASE + i * 4);
			if (slot.CMD == fixed_cmd) {
				fixed_idx = i;
				break;
			}
		}
		zassert_true(fixed_idx >= 0,
			     "cmd %#x must exist in the fixed table after cold_reset", fixed_cmd);
		zassert_equal(slot.EN, 0,
			      "fixed slot %d EN must be 0 right after cold_reset", fixed_idx);

		/* 自检：该 CMD 在 HW 表中必须**唯一**，否则下面的负断言无意义 */
		{
			int again = spif_get_cmd_slot(dev, fixed_cmd, (uint32_t)fixed_idx + 1);

			zassert_equal(again, -ENOSR,
				      "cmd %#x must appear only once in the fixed table (dup at %d)",
				      fixed_cmd, again);
		}

		/* 制造 HW/SW 不一致：改 HW 槽的 CMD，SW 镜像 fixed_cmd_tab[] 不变 */
		spif_set_cmd_by_idx(dev, 0x5a, (uint8_t)fixed_idx);
		zassert_equal(spif_get_cmd_slot(dev, fixed_cmd, 0), -ENOSR,
			      "hw table must no longer contain %#x after desync", fixed_cmd);

		/* 走第二个循环：SW 镜像命中 ⇒ 目标槽应被写回 CMD=0x03 且 EN=1 */
		zassert_ok(spif_add_cmd(dev, fixed_cmd), "spif_add_cmd(%#x)", fixed_cmd);
		slot.value = sys_read32(base + SPIF_CMD_BASE + fixed_idx * 4);
		zassert_equal(slot.CMD, fixed_cmd,
			      "slot %d CMD must be restored to %#x", fixed_idx, fixed_cmd);
		zassert_equal(slot.EN, 1,
			      "slot %d EN must be 1 after spif_add_cmd — defect #1 "
			      "(stale idx write address) regression guard", fixed_idx);

		ZT_INFO("cmd_table_add_remove.fixed_path",
			"slot=%d,cmd=%#x,en=%d", fixed_idx, slot.CMD, (int)slot.EN);

		/* 复原：清该槽 EN（保留 CMD），避免污染同实例的后续用例 */
		zassert_ok(spif_remove_cmd(dev, fixed_cmd), "restore: clear fixed slot EN");
	}
}

ZTEST(spif, cmd_table_full_boundary)
{
	const struct device *dev = spif_dev(1);
	int added = 0;

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");
	spif_general_pool_flush(dev);

	/* general 表容量 = SPIF_GENERAL_CMD_TABLE_NUM = 40（spi_filter.h:18）：
	 * 第 40 个成功，第 41 个 → -ENOSR（spi_filter.c:838-841） */
	for (int i = 0; i < SPIF_GENERAL_CMD_TABLE_NUM; i++) {
		zassert_ok(spif_add_general_cmd(dev, spif_general_pool[i]),
			   "add #%d (%#x) must succeed", i, spif_general_pool[i]);
		added++;
	}
	zassert_equal(added, SPIF_GENERAL_CMD_TABLE_NUM, "added count");

	zassert_equal(spif_add_general_cmd(dev, spif_general_pool[SPIF_GENERAL_CMD_TABLE_NUM]),
		      -ENOSR, "the 41st add must be -ENOSR");

	ZT_INFO("cmd_table_full_boundary", "added=%d,capacity=%d", added,
		SPIF_GENERAL_CMD_TABLE_NUM);

	/* 清空，保证其他用例的槽位可用 */
	spif_general_pool_flush(dev);
	for (int i = 0; i < SPIF_GENERAL_CMD_TABLE_NUM; i++) {
		zassert_equal(spif_get_general_cmd_slot(dev, spif_general_pool[i], 0), -ENOSR,
			      "cleanup: cmd #%d must be gone", i);
	}
}

/*
 * 回读「地址有效表」(ADDR_VALID_EN) 的正确路径。
 *
 * ⚠️ 该表**不能由 CPU 直接 sys_read32 回读**（直读恒 0）：
 *   - 写窗 0x800-0xfff / 读窗 0x1000-0x17ff 对 CPU 只写不读，驱动自身
 *     也从不直读它们（这正是原先直读断言恒失败的原因）；
 *   - `CONFIG_SPI_FILTER_ADDR_WHITELIST_BUF=y` 时驱动读 SW 镜像
 *     （spi_filter.c:454-469），非 BUF 时走**间接 SRAM 读端口**
 *     （:471-502：写 SPIF_READ_SRAM_ADDR → 置 SPIF_READ_ADDR_REQ → 轮询
 *      直到 CS 空闲 → 读 SPIF_READ_SRAM_DATA）。
 *   - 间接端口读的是 HW 表本身，与 BUF 配置无关：驱动的两条写入路径
 *     （`spif_memset_*_addr_whitelist()` 与 `spif_address_privilege_config_op()`）
 *     都同时写 HW 寄存器窗（`sys_write32(..., priv_table_base + reg_off*4)`）。
 * 故本辅助复刻驱动的间接端口时序，供用例回读表内容。
 * `reg_off` = 表内寄存器序号（0..SPIF_ADDR_PRIV_REG_NUN-1），与驱动一致。
 * 返回 0 成功；-EIO = 端口轮询超时（CS 未空闲）。
 */
static int spif_whitelist_peek(uintptr_t base, enum addr_priv_rw_select rw_select,
			       uint32_t reg_off, uint32_t *data)
{
	spif_read_addr_req_t req = { 0 };
	k_timepoint_t timeout;

	if (rw_select == FLAG_ADDR_PRIV_READ_SELECT) {
		req.READ_RADDR_REQ = 1;
	} else {
		req.READ_WADDR_REQ = 1;
	}

	sys_write32(reg_off, base + SPIF_READ_SRAM_ADDR);
	sys_write32(req.value, base + SPIF_READ_ADDR_REQ);

	timeout = sys_timepoint_calc(K_MSEC(100));
	while (sys_read32(base + SPIF_READ_ADDR_REQ) != 0U) {
		if (sys_timepoint_expired(timeout)) {
			return -EIO;
		}
	}

	*data = sys_read32(base + SPIF_READ_SRAM_DATA);

	return 0;
}

ZTEST(spif, addr_whitelist_fill)
{
	const struct device *dev = spif_dev(1);
	const uintptr_t base = spif_reg_base(1);
	/* 表共 SPIF_ADDR_PRIV_REG_NUN (512) 个 32-bit 寄存器：
	 * 每个寄存器 32 bit × 16KB = 512KB 地址空间 ⇒ 整片 MB(256) */
	const uint32_t reg_0 = 0;
	const uint32_t reg_last = SPIF_ADDR_PRIV_REG_NUN - 1;
	uint32_t w0, wl, r0, rl;

/* 经间接端口取「写/读两个窗口」的首、尾寄存器 */
#define SPIF_PEEK_ALL()								\
	do {									\
		zassert_ok(spif_whitelist_peek(base, FLAG_ADDR_PRIV_WRITE_SELECT,	\
					       reg_0, &w0), "peek write[0]");	\
		zassert_ok(spif_whitelist_peek(base, FLAG_ADDR_PRIV_WRITE_SELECT,	\
					       reg_last, &wl), "peek write[last]"); \
		zassert_ok(spif_whitelist_peek(base, FLAG_ADDR_PRIV_READ_SELECT,	\
					       reg_0, &r0), "peek read[0]");	\
		zassert_ok(spif_whitelist_peek(base, FLAG_ADDR_PRIV_READ_SELECT,	\
					       reg_last, &rl), "peek read[last]"); \
	} while (0)

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");

	/* num=0 → 读/写白名单整片清 0（spi_filter.c:756-796） */
	spif_memset_addr_whitelist(dev, 0);
	SPIF_PEEK_ALL();
	zassert_equal(w0, 0u, "write win [0]");
	zassert_equal(wl, 0u, "write win last");
	zassert_equal(r0, 0u, "read win [0]");
	zassert_equal(rl, 0u, "read win last");

	/* num=1 → 整片置 0xffffffff */
	spif_memset_addr_whitelist(dev, 1);
	SPIF_PEEK_ALL();
	zassert_equal(w0, 0xffffffffu, "write win [0] all-1");
	zassert_equal(wl, 0xffffffffu, "write win last all-1");
	zassert_equal(r0, 0xffffffffu, "read win [0] all-1");
	zassert_equal(rl, 0xffffffffu, "read win last all-1");

	/* num 非法（非 0/1）只打 DEV_ERR、无返回值，**不改寄存器**（spi_filter.c:802-806）；
	 * 这里断言"保持不变"以固化该行为（非法入参静默忽略属驱动既有语义）。 */
	spif_memset_addr_whitelist(dev, 2);
	SPIF_PEEK_ALL();
	zassert_equal(w0, 0xffffffffu, "invalid num must not modify registers");

	ZT_INFO("addr_whitelist_fill", "reg_nun=%u,reg_last=%u", SPIF_ADDR_PRIV_REG_NUN,
		reg_last);
#undef SPIF_PEEK_ALL
}

ZTEST(spif, addr_privilege_guard)
{
	const struct device *dev = spif_dev(0);   /* spif1：本工程唯一不设 flash_size 的实例 */
	uint32_t orig_size;

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");

	/* 前置：flash_size 必须为 0，`spif_address_privilege_config()` 才会把
	 * `spif_address_privilege_config_op()` 的返回值透传出来（spi_filter.c:744-747）；
	 * flash_size != 0 时该函数恒返回 0（缺陷 #3，见 COVERAGE.md）。 */
	orig_size = spif_flash_size_get(dev);
	zassert_equal(orig_size, 0u,
		      "spif1 flash_size must stay 0 for this guard test (got %#x)", orig_size);

	/*
	 * ⚠️ 以下守卫分支在驱动内 `goto end` 后**未配对地**执行 release_spif_device()
	 *    （acquire 在 :685，守卫 :653-656 在其之前）—— 缺陷见 COVERAGE.md「上游缺陷观察」#1。
	 *
	 * 本项为何安全：`sem_spif` 以 `k_sem_init(..., 1, 1)` 初始化（limit=1，:1783），
	 * 而 Zephyr 的 `z_impl_k_sem_give()` 在 count == limit 时**不增加计数**
	 * （`sem->count += (sem->count != sem->limit) ? 1U : 0U;`）—— 故无等待者时
	 * 多一次 give 等价于空操作。本用例单线程、无其他 `sem_spif` 等待者，故不会
	 * 破坏互斥。若将来加入并发用例，必须先把该缺陷按实机行为核清。
	 */

	/* 非法地址：addr >= MB(256)（spi_filter.c:653-656），±1 边界 */
	zassert_equal(spif_address_privilege_config(dev, FLAG_ADDR_PRIV_READ_SELECT,
						    FLAG_ADDR_PRIV_ENABLE, MB(256), KB(16)),
		      -EINVAL, "addr == MB(256) must be -EINVAL");
	zassert_equal(spif_address_privilege_config(dev, FLAG_ADDR_PRIV_WRITE_SELECT,
						    FLAG_ADDR_PRIV_ENABLE, MB(256) + KB(16), KB(16)),
		      -EINVAL, "addr > MB(256) must be -EINVAL");

	/* 零长度：len == 0 → -EINVAL */
	zassert_equal(spif_address_privilege_config(dev, FLAG_ADDR_PRIV_READ_SELECT,
						    FLAG_ADDR_PRIV_ENABLE, 0, 0),
		      -EINVAL, "len == 0 must be -EINVAL");

	/* 上界内（MB(256) - KB(16)）应被接受 */
	zassert_ok(spif_address_privilege_config(dev, FLAG_ADDR_PRIV_READ_SELECT,
						  FLAG_ADDR_PRIV_ENABLE,
						  MB(256) - KB(16), KB(16)),
		   "addr just below MB(256) must be accepted");

	ZT_INFO("addr_privilege_guard", "guard_boundary=%#x,len_min=%u", MB(256), KB(16));

	/* 复位白名单镜像与寄存器 */
	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "restore cold_reset");
}

ZTEST(spif, addr_privilege_valid_region)
{
	const struct device *dev = spif_dev(0);
	const uintptr_t base = spif_reg_base(0);
	/* addr = MB(1)：reg_off = MB(1)/KB(512) = 2，bit_off = 0 */
	const uint32_t reg_off = MB(1) / KB(512);
	uint32_t v;

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");

	/* 起点归零，便于按位断言（cold_reset 默认把整片置 1） */
	spif_memset_addr_whitelist(dev, 0);

	/* 16 KB 对齐、addr < MB(256)、len != 0 → 走镜像路径写入（spi_filter.c:690-712） */
	zassert_ok(spif_address_privilege_config(dev, FLAG_ADDR_PRIV_READ_SELECT,
						  FLAG_ADDR_PRIV_ENABLE, MB(1), KB(16)),
		   "enable 16KB-aligned region");

	/* 回读走间接 SRAM 端口：该表对 CPU 直读恒 0（见 spif_whitelist_peek 注释） */
	zassert_ok(spif_whitelist_peek(base, FLAG_ADDR_PRIV_READ_SELECT, reg_off, &v),
		   "peek READ reg_off=%u", reg_off);
	zassert_equal(v, 0x1u, "READ addr-valid bit for MB(1)..+16KB must be set (got %#x)", v);

	/* 关闭同一区间 → 位被清 */
	zassert_ok(spif_address_privilege_config(dev, FLAG_ADDR_PRIV_READ_SELECT,
						  FLAG_ADDR_PRIV_DISABLE, MB(1), KB(16)),
		   "disable the same region");
	zassert_ok(spif_whitelist_peek(base, FLAG_ADDR_PRIV_READ_SELECT, reg_off, &v),
		   "peek READ reg_off=%u after disable", reg_off);
	zassert_equal(v, 0u, "READ addr-valid bit must be cleared (got %#x)", v);

	/* 写方向是独立寄存器窗 */
	zassert_ok(spif_address_privilege_config(dev, FLAG_ADDR_PRIV_WRITE_SELECT,
						  FLAG_ADDR_PRIV_ENABLE, MB(1), KB(16)),
		   "enable write region");
	zassert_ok(spif_whitelist_peek(base, FLAG_ADDR_PRIV_WRITE_SELECT, reg_off, &v),
		   "peek WRITE reg_off=%u", reg_off);
	zassert_equal(v, 0x1u, "WRITE addr-valid bit must be set (got %#x)", v);

	ZT_INFO("addr_privilege_valid_region", "reg_off=%u,len=%u", reg_off, KB(16));

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "restore cold_reset");
}

ZTEST(spif, flash_size_boundary)
{
	const struct device *dev = spif_dev(3);   /* spif4：独立实例，改动不影响其他用例 */

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");

	/* 守卫：size == 0，或 size 不能整除 MB(256)（spi_filter.c:185-188，
	 * IS_ALIGNED(MB(256), size) 即 (MB(256) % size) == 0） */
	zassert_equal(spif_flash_size_set(dev, 0), -EINVAL, "size 0 must be -EINVAL");
	zassert_equal(spif_flash_size_set(dev, 3), -EINVAL, "size 3 (non-divisor) must be -EINVAL");
	zassert_equal(spif_flash_size_set(dev, MB(256) + 1), -EINVAL,
		      "size > MB(256) must be -EINVAL");

	/* 合法上界：size == MB(256) */
	zassert_ok(spif_flash_size_set(dev, MB(256)), "size MB(256) must be accepted");
	zassert_equal(spif_flash_size_get(dev), MB(256), "flash_size readback MB(256)");

	/* 合法下界（仍须整除）：64 KB */
	zassert_ok(spif_flash_size_set(dev, KB(64)), "size KB(64) must be accepted");
	zassert_equal(spif_flash_size_get(dev), KB(64), "flash_size readback KB(64)");

	ZT_INFO("flash_size_boundary", "max=%#x,min_ok=%#x", MB(256), KB(64));
}

ZTEST(spif, pinctrl_state_switch)
{
	const struct device *dev = spif_dev(2);   /* spif3 */
	const struct device *sibling = spif_dev(3);

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");

	/* 先确保相邻滤波实例（spif4）不在 master 态，
	 * 否则 spif_switch_to_master_handle() 会以 -EBUSY 拒绝（spi_filter.c:1518-1527） */
	zassert_ok(spif_switch_to_filter(sibling), "sibling back to filter");

	zassert_ok(spif_switch_to_filter(dev), "switch to filter");
	zassert_true(spif_pinctrl_filter_mode_check(dev),
		     "spif3 must report filter-mode pinctrl");

	zassert_ok(spif_switch_to_master(dev), "switch to master");
	zassert_true(spif_pinctrl_master_mode_check(dev),
		     "spif3 must report master-mode pinctrl");

	/* spif_switch_to_filter 会顺带 spif_enable(true)（spi_filter.c:1484-1485） */
	zassert_ok(spif_switch_to_filter(dev), "switch back to filter");
	zassert_true(spif_pinctrl_filter_mode_check(dev), "spif3 back in filter mode");
	zassert_equal(spif_read_cfg_at(2).EN, 1, "switch_to_filter must enable the filter");
}

ZTEST(spif, master_read_jedec_id)
{
	/*
	 * 外部: spif3 在 master 模式下经其配对 SPI master（dtsi `spi = <&spi4>`）的
	 *       CS0(GPIOI 0) 直接驱动总线上挂的 NOR flash；即滤波器的 in/out 管脚
	 *       与 spi4 主控管脚在板级连到同一颗 flash。需要该外部器件在位。
	 * 判据：SPI 事务成功（不假设具体厂商 ID —— 实测值经 ZT_INFO 上报，
	 *       型号/容量基准待 P6 实测确认）。
	 */
	const struct device *dev = spif_dev(2);   /* spif3 */
	const struct device *spi = spif_spi_dev(dev);
	const struct device *sibling = spif_dev(3);

	static const uint8_t tx_buffer[] = { 0x9f, 0x00, 0x00, 0x00 };
	static uint8_t rx_buffer[4];
	const struct spi_config spi_cfg = {
		.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
		.frequency = MHZ(1),
		.cs = { .gpio = *(spif_spi_cs(dev)) },
	};
	const struct spi_buf tx_buf = { .buf = (void *)tx_buffer, .len = sizeof(tx_buffer) };
	const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf rx_buf = { .buf = rx_buffer, .len = sizeof(rx_buffer) };
	const struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
	int ret;

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");
	zassert_true(device_is_ready(spi), "paired SPI master not ready");

	zassert_ok(spif_switch_to_filter(sibling), "sibling back to filter");
	zassert_ok(spif_switch_to_master(dev), "switch spif3 to master");

	memset(rx_buffer, 0, sizeof(rx_buffer));
	ret = spi_transceive(spi, &spi_cfg, &tx, &rx);
	zassert_ok(ret, "JEDEC-ID transceive failed (ret=%d); 外部 flash 在位?", ret);

	ZT_INFO("master_read_jedec_id", "jedec=%02x%02x%02x", rx_buffer[1], rx_buffer[2],
		rx_buffer[3]);

	zassert_ok(spif_switch_to_filter(dev), "switch spif3 back to filter");
}

/* -------------------------------------------------------------------------- */
/* P4.7 新增：外部参考资产 spi_monitor_filter 的对照覆盖                       */
/*                                                                             */
/* 参考资产（用户 2026-09-17 提供，出处见 tests/COVERAGE.md「参考资产出处」）：*/
/*   `spi_monitor_filter` 在 spif3(filter) + spif1(master) 上走：              */
/*     cold_reset → flash_size=MB(32) → filter_enable → 判 filter 态          */
/*     → passthrough_analog_mux_enable → 判 passthrough 态                     */
/*     → switch_to_master(spif1) → 判 master 态                                */
/*     → 裸 spi_transceive(0x9f) / (0x03 + 24bit addr) → 读 flash3_0 JEDEC     */
/* 其可确定性断言的部分落入以下三项；依赖监视日志 / flash_size 镜像 /           */
/* `spif_add_cmd` 的部分按判据类型跳过（见 COVERAGE.md 跳过项）。              */
/* -------------------------------------------------------------------------- */

ZTEST(spif, passthrough_mode_switch)
{
	const struct device *dev = spif_dev(1);   /* spif2：pinctrl 未被其他用例长期占用 */

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");

	/* 诊断先于断言（ztest 断言失败即 abort 用例线程，其后代码不执行）：
	 * default 与 passthrough 两态 pinmux 完全相同、仅差 `analog` 属性
	 * （dtsi pinctrl-0 / pinctrl-1），故两态判据必须互斥 —— 复位后
	 * 应为 filter=1,passthrough=0。 */
	ZT_INFO("passthrough_mode_switch", "after_cold_reset filter=%d,passthrough=%d",
		(int)spif_pinctrl_filter_mode_check(dev),
		(int)spif_pinctrl_passthrough_mode_check(dev));

	/* cold_reset 走 pinctrl-apply(DEFAULT)，故复位后应停在 filter 态 */
	zassert_true(spif_pinctrl_filter_mode_check(dev),
		     "spif2 must be in filter mode right after cold_reset");
	zassert_false(spif_pinctrl_passthrough_mode_check(dev),
		      "spif2 must not report passthrough after cold_reset");

	/* 切 passthrough。⚠️ 驱动忽略 enable 参数、恒应用 passthrough 状态
	 * （spi_filter.c:1552-1561，上游缺陷 #7）—— 本项只固化状态机行为，
	 * 不对 enable=false 的语义下断言。 */
	zassert_ok(spif_passthrough_analog_mux_enable(dev, true), "passthrough enable");
	zassert_true(spif_pinctrl_passthrough_mode_check(dev),
		     "spif2 must report passthrough pinctrl");
	zassert_false(spif_pinctrl_filter_mode_check(dev),
		      "filter/passthrough pinctrl states must be mutually exclusive");

	/* 切回 filter 态（default 管脚表） */
	zassert_ok(spif_switch_to_filter(dev), "switch back to filter");
	zassert_true(spif_pinctrl_filter_mode_check(dev), "spif2 back in filter mode");
	zassert_false(spif_pinctrl_passthrough_mode_check(dev), "passthrough must be cleared");

	ZT_INFO("passthrough_mode_switch", "default->passthrough->default ok");
}

ZTEST(spif, bus_pairing)
{
	/*
	 * 每个 SPIF 的配对 SPI master 与 CS 必须与 DT 声明一致：
	 *   spif1 → spi3 (CS gpioi[8])，spif2 → spi3 (CS gpiog[2])
	 *   spif3 → spi4 (CS gpioi[0])，spif4 → spi4 (CS gpioq[0])
	 * 这是参考资产走 `spif_spi_dev()` + `spif_spi_cs()` 裸事务路径所依赖的
	 * 静态契约（`device_ready` 只验非 NULL/ready，不验实例身份）。
	 * 零外部条件。
	 */
	const struct device *expected_spi[SPIF_INST_NUM] = {
		DEVICE_DT_GET(DT_PHANDLE(DT_NODELABEL(spif1), spi)),
		DEVICE_DT_GET(DT_PHANDLE(DT_NODELABEL(spif2), spi)),
		DEVICE_DT_GET(DT_PHANDLE(DT_NODELABEL(spif3), spi)),
		DEVICE_DT_GET(DT_PHANDLE(DT_NODELABEL(spif4), spi)),
	};
	const struct gpio_dt_spec expected_cs[SPIF_INST_NUM] = {
		GPIO_DT_SPEC_GET(DT_NODELABEL(spif1), cs_gpios),
		GPIO_DT_SPEC_GET(DT_NODELABEL(spif2), cs_gpios),
		GPIO_DT_SPEC_GET(DT_NODELABEL(spif3), cs_gpios),
		GPIO_DT_SPEC_GET(DT_NODELABEL(spif4), cs_gpios),
	};

	for (int i = 0; i < SPIF_INST_NUM; i++) {
		const struct device *dev = spif_dev(i);
		const struct gpio_dt_spec *cs = spif_spi_cs(dev);

		zassert_true(device_is_ready(expected_spi[i]),
			     "spif%d paired spi (dtsi phandle) not ready", i + 1);
		zassert_true(spif_spi_dev(dev) == expected_spi[i],
			     "spif%d paired SPI master does not match dtsi spi phandle", i + 1);
		zassert_true(cs->port == expected_cs[i].port && cs->pin == expected_cs[i].pin,
			     "spif%d CS does not match dtsi cs-gpios (pin=%u)", i + 1, cs->pin);
	}

	ZT_INFO("bus_pairing", "spif1/2->spi3, spif3/4->spi4 ok");
}

ZTEST(spif, master_read_with_addr)
{
	/*
	 * 外部: 参考资产 spi_monitor_filter 以 `spifiltermaster = &spif1`
	 *       （配对 spi3，CS = GPIOI 8）发裸 `0x03 + 24bit 地址` 读命令
	 *       （其 `spi_monitor_send_read_addr()`）；本项复刻该命令形态。
	 *       需要 spi3 总线上挂的 NOR flash 在位 —— 与 `master_read_jedec_id`
	 *       同属「master 模式直驱总线」外部路径（COVERAGE.md 顶部）。
	 * 判据：SPI 事务成功（读回数据经 ZT_INFO 上报，不预设器件值）。
	 */
	const struct device *dev = spif_dev(0);   /* spif1：参考资产的 spifiltermaster */
	const struct device *sibling = spif_dev(1);
	const struct device *spi = spif_spi_dev(dev);
	/* 0x03 = READ，后接 3 字节地址 0x000040 */
	static const uint8_t tx_buffer[] = { 0x03, 0x00, 0x00, 0x40 };
	static uint8_t rx_buffer[8];
	const struct spi_config spi_cfg = {
		.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
		.frequency = MHZ(1),
		.cs = { .gpio = *(spif_spi_cs(dev)) },
	};
	const struct spi_buf tx_buf = { .buf = (void *)tx_buffer, .len = sizeof(tx_buffer) };
	const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf rx_buf = { .buf = rx_buffer, .len = sizeof(rx_buffer) };
	const struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };
	int ret;

	zassert_ok(linkedsemi_spi_filter_cold_reset(dev), "cold_reset failed");
	zassert_true(device_is_ready(spi), "paired SPI master (spi3) not ready");

	/* spif1 的相邻实例是 spif2（驱动内 spif_spi_check_dev_name_arr） */
	zassert_ok(spif_switch_to_filter(sibling), "sibling (spif2) back to filter");
	zassert_ok(spif_switch_to_master(dev), "switch spif1 to master");

	memset(rx_buffer, 0, sizeof(rx_buffer));
	ret = spi_transceive(spi, &spi_cfg, &tx, &rx);
	zassert_ok(ret, "0x03 READ transceive failed (ret=%d); 外部 flash 在位?", ret);

	ZT_INFO("master_read_with_addr", "read=%02x%02x%02x%02x", rx_buffer[4], rx_buffer[5],
		rx_buffer[6], rx_buffer[7]);

	zassert_ok(spif_switch_to_filter(dev), "switch spif1 back to filter");
}

ZTEST_SUITE(spif, NULL, NULL, NULL, NULL, NULL);
