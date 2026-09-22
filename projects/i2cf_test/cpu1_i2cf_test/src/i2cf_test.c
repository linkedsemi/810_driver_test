/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * i2cf_test —— pfr_tpm I2C filter 驱动用例（drivers/pfr/i2c/i2c_filter.c）。
 *
 * 纯 ztest：每项自带前置（i2cf_reset_all），不依赖其他用例的执行顺序或残留状态
 * （COVERAGE 见 tests/COVERAGE.md；边界常量来源逐项标注）。
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

#include "ztest_glue.h"
#include "i2cf_test.h"

#include <i2c_filter.h>
#include <reg_i2c_filter.h>
#include <reg_sysc_app_awo.h>   /* SYSC_APP_AWO->IO_FUNC[][]：引脚复用矩阵（switch_master_filter 判据） */

LOG_MODULE_REGISTER(i2cf_test_case, LOG_LEVEL_INF);

/* 驱动导出但未在 i2c_filter.h 声明（非 static，可直接 extern 取用） */
extern int linkedsemi_i2c_filter_config_scl_hold_time(const struct device *dev,
						      uint16_t scl_hold_time);

/*
 * 引脚复用可观测（switch_master_filter 的判别性判据）
 *
 * 依据链（全部来自源码）：
 *   - pinctrl 写入路径 pinctrl_lsqsh.c:13-105 → ls_soc_gpio.c:951-981
 *     `pinmux_cfg_pin_func_alt()`：先 `per_func_disable_all(pin)`（清该 pad 的
 *     IO_FUNC[0..3] 位，:894-899），再 `per_func_enable(pin, func)`（:828-832）。
 *   - 实际寄存器写：`SYSC_APP_AWO->IO_FUNC[func][port >> 1] |= 1 << ((port%2)*16 + num)`
 *     （ls_soc_gpio.c:831）。
 *   - pinmux 字段编码（pinctrl_soc.h:23-33）：pin:8[0-7] / func:2[8-9] /
 *     alt:5[10-14] / func_valid:1[15]；func 数字值 = FUNC1(0)/FUNC2(1)/FUNC3(2)/FUNC4(3)。
 *
 * 关键事实（DT）：**i2c4.SCL 与 i2cf4.SCLO 是同一物理 pad PE02**，只是复用档位不同
 *   —— i2c4 的 pinctrl-0 = <&I2C4_scl_PE02 &I2C4_sda_PB00>（func1），
 *      i2cf4 的 pinctrl-0 = <&i2cf4_sclo_pe02 ...>（func2）。
 *   ⇒ `switch_to_master()`（i2c_filter.c:263-276，作用 i2c4 的 pcfg）与
 *     `switch_to_filter()`（:278-290，作用 i2cf4 的 pcfg）会让同一 bit 位**互斥地**
 *     在 0/1 之间翻转 —— 这是"管脚真的交出去了"的可证伪判据。
 *   （原实现只断言两个恒返回 0 的 API 返回值 ⇒ 不可能 FAIL。）
 */
#define PM_PIN(m)  ((m) & 0xffu)
#define PM_FUNC(m) (((m) >> 8) & 0x3u)
#define PM_PORT(m) (PM_PIN(m) >> 4)
#define PM_NUM(m)  (PM_PIN(m) & 0xfu)
#define PM_PAIR(m) (PM_PORT(m) >> 1)
#define PM_BIT(m)  (((PM_PORT(m) % 2) * 16) + PM_NUM(m))

/* 同一物理 pad PE02 在两个节点里的复用档位（取自 DT，不硬编码数值） */
#define I2C4_SCL_PINMUX  DT_PROP_BY_PHANDLE_IDX(DT_NODELABEL(i2c4), pinctrl_0, 0, pinmux)
#define I2CF4_SCL_PINMUX DT_PROP_BY_PHANDLE_IDX(DT_NODELABEL(i2cf4), pinctrl_0, 0, pinmux)

/* 该复用档位在 IO_FUNC 矩阵里对应的使能位（1 = 该 pad 复用给该外设） */
static uint32_t pad_func_en(uint32_t pinmux)
{
	mm_reg_t reg = (mm_reg_t)(uintptr_t)
		&SYSC_APP_AWO->IO_FUNC[PM_FUNC(pinmux)][PM_PAIR(pinmux)];

	return (sys_read32(reg) >> PM_BIT(pinmux)) & 0x1u;
}

/* 定义在下方"行为层用例"段；master_eeprom_rw 先用，故前向声明 */
static int i2cf_scan_slaves_bypass(const struct device *dev, uint8_t *out, int max);
static uint8_t i2cf_first_slave_bypass(const struct device *dev);

/* -------------------------------------------------------------------------- */
/* 设备 / 寄存器定位                                                           */
/* -------------------------------------------------------------------------- */

/*
 * ★ 2026-09-22：按确认的拓扑（i2c2 主机 → i2cf4 被测 → i2c3 从机），
 *   `i2cf1/2/3` 已**关掉**（纯主/从实例的配对滤波器会争引脚，见 app.overlay）。
 *   ⚠️ `DEVICE_DT_GET()` / `DT_REG_ADDR()` 对 **disabled 节点是编译期错误**，
 *   故必须条件化：未使能的槽位填 NULL/0，由调用方判空跳过（见下方各用例）。
 */
#define I2CF_DEV_OR_NULL(lbl)							\
	COND_CODE_1(DT_NODE_HAS_STATUS(DT_NODELABEL(lbl), okay),		\
		    (DEVICE_DT_GET(DT_NODELABEL(lbl))), (NULL))

#define I2CF_BASE_OR_ZERO(lbl)							\
	COND_CODE_1(DT_NODE_HAS_STATUS(DT_NODELABEL(lbl), okay),		\
		    (DT_REG_ADDR(DT_NODELABEL(lbl))), (0))

static const struct device *const i2cf_devs[I2CF_INST_NUM] = {
	I2CF_DEV_OR_NULL(i2cf1),
	I2CF_DEV_OR_NULL(i2cf2),
	I2CF_DEV_OR_NULL(i2cf3),
	I2CF_DEV_OR_NULL(i2cf4),      /* ★ 被测（idx 3） */
};

static const uintptr_t i2cf_bases[I2CF_INST_NUM] = {
	I2CF_BASE_OR_ZERO(i2cf1),
	I2CF_BASE_OR_ZERO(i2cf2),
	I2CF_BASE_OR_ZERO(i2cf3),
	I2CF_BASE_OR_ZERO(i2cf4),     /* ★ 被测（idx 3） */
};

const struct device *i2cf_dev(int idx)
{
	return (idx >= 0 && idx < I2CF_INST_NUM) ? i2cf_devs[idx] : NULL;
}

uintptr_t i2cf_reg_base(int idx)
{
	return (idx >= 0 && idx < I2CF_INST_NUM) ? i2cf_bases[idx] : 0;
}

/* -------------------------------------------------------------------------- */
/* 前置：把实例恢复到"复位 + 全通道关闭"的确定状态                             */
/* -------------------------------------------------------------------------- */

/*
 * cold_reset 会清空命令表（clr_tbl=true），但**不清 WHITELIST_ADDRESS_* 与
 * SMBF_ADDRESS_ENABLE**。为了让每个用例独立于其他用例的残留，这里额外显式关闭
 * 全部 16 个通道（关掉 enable 位后，fill_bitmap 的"重址检查"才会放行同址）。
 */
static void i2cf_reset_all(const struct device *dev)
{
	zassert_ok(linkedsemi_i2c_filter_cold_reset(dev), "cold_reset failed");

	for (uint8_t i = 0; i < I2CF_CHANNEL_NUM; i++) {
		zassert_ok(linkedsemi_i2c_filter_enable_channel(dev, i, false),
			   "disable channel %u failed", i);
	}
}

static void fill_pattern(uint32_t bitmap[LINKEDSEMI_I2C_F_REMAP_SIZE_U32], uint8_t seed)
{
	for (int w = 0; w < LINKEDSEMI_I2C_F_REMAP_SIZE_U32; w++) {
		bitmap[w] = (uint32_t)seed * 0x01010101u + (uint32_t)w;
	}
}

/* -------------------------------------------------------------------------- */
/* 用例                                                                        */
/* -------------------------------------------------------------------------- */

ZTEST(i2cf, device_ready)
{
	/* ★ 2026-09-22：按拓扑只使能 i2cf4（见 app.overlay）⇒ 未使能槽位为 NULL，
	 *   **跳过不判失败**；同时要求**被测那一个必须在**。 */
	for (int i = 0; i < I2CF_INST_NUM; i++) {
		const struct device *d = i2cf_dev(i);

		if (d == NULL) {
			continue;          /* 该实例按拓扑未使能 */
		}
		zassert_true(device_is_ready(d), "i2cf%d not ready", i + 1);

		/* i2c 属性指向配对的 master；i2cf4 绑定 i2c4（板上同管脚复用） */
		const struct device *bound = i2cf_i2c_dev(d);

		zassert_not_null(bound, "i2cf%d bound i2c is NULL", i + 1);
		zassert_true(device_is_ready(bound), "i2cf%d bound i2c not ready", i + 1);
	}

	/* 被测那一个必须存在，否则本 case 无意义 */
	zassert_not_null(i2cf_dev(3), "i2cf4 (DUT) is not enabled");

	zassert_equal(i2cf_i2c_dev(i2cf_dev(3)), DEVICE_DT_GET(DT_NODELABEL(i2c4)),
		      "i2cf4 must bind i2c4");
}

ZTEST(i2cf, cold_reset_state)
{
	const struct device *dev = i2cf_dev(3);   /* i2cf4 */
	const uintptr_t base = i2cf_reg_base(3);

	i2cf_reset_all(dev);

	/* cold_reset 收尾写入（i2c_filter.c:342-346）：
	 *   SMBF_REG_ENABLE = 1
	 *   INTR_MSK = ADDRESS_BEYOND_WHITELIST | COMMAND_BEYOND_WHITELIST = 0x3
	 *   SCL_HOLD_TIME 取自 DT 的 scl-hold-time（dtsi 为 0xfa） */
	zassert_equal(sys_read32(base + SMBF_REG_ENABLE), 0x1,
		      "SMBF_REG_ENABLE after cold_reset");
	zassert_equal(sys_read32(base + INTR_MSK), 0x3,
		      "INTR_MSK after cold_reset (addr|cmd)");

	smbf_control0_reg_t ctl = { .value = sys_read32(base + SMBF_CONTROL0_REG) };
	const uint16_t dt_hold = (uint16_t)DT_PROP(DT_NODELABEL(i2cf4), scl_hold_time);

	zassert_equal(ctl.SCL_HOLD_TIME, dt_hold, "SCL_HOLD_TIME must equal DT %#x", dt_hold);
	ZT_INFO("cold_reset_state", "reg_enable=%#x,intr_msk=%#x,scl_hold=%#x",
		sys_read32(base + SMBF_REG_ENABLE), sys_read32(base + INTR_MSK),
		ctl.SCL_HOLD_TIME);
}

ZTEST(i2cf, enable_channel_boundary)
{
	const struct device *dev = i2cf_dev(3);
	const uintptr_t base = i2cf_reg_base(3);

	i2cf_reset_all(dev);

	/* 上界内（15）成功；越界（16 / 255）→ -EINVAL
	 * 边界来源：i2c_filter.c:114 `if (idx > (LINKEDSEMI_I2C_F_ADDR_NUM - 1))`，
	 * LINKEDSEMI_I2C_F_ADDR_NUM == 16（i2c_filter.h:10） */
	zassert_ok(linkedsemi_i2c_filter_enable_channel(dev, I2CF_CHANNEL_NUM - 1, true),
		   "idx 15 must be accepted");
	zassert_true(sys_read32(base + SMBF_ADDRESS_ENABLE) & BIT(I2CF_CHANNEL_NUM - 1),
		     "SMBF_ADDRESS_ENABLE bit15 not set");

	zassert_equal(linkedsemi_i2c_filter_enable_channel(dev, I2CF_CHANNEL_NUM, true),
		      -EINVAL, "idx 16 must be -EINVAL");
	zassert_equal(linkedsemi_i2c_filter_enable_channel(dev, 0xff, true),
		      -EINVAL, "idx 255 must be -EINVAL");

	/* 关通道要真的清位 */
	zassert_ok(linkedsemi_i2c_filter_enable_channel(dev, I2CF_CHANNEL_NUM - 1, false),
		   "idx 15 disable");
	zassert_false(sys_read32(base + SMBF_ADDRESS_ENABLE) & BIT(I2CF_CHANNEL_NUM - 1),
		      "SMBF_ADDRESS_ENABLE bit15 not cleared");
}

ZTEST(i2cf, fill_dump_roundtrip)
{
	uint32_t bitmap[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];
	uint32_t dump[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];
	const struct device *dev = i2cf_dev(3);
	const uintptr_t base = i2cf_reg_base(3);

	i2cf_reset_all(dev);

	/* 16 个通道逐个独立通道组（每 4 通道共用一个地址寄存器）roundtrip，
	 * 确保 (idx>>2) 分组偏移与 SMBF_ADDRESS_INDEX 选择都正确
	 * （i2c_filter.c:168-179 / :208-217） */
	for (uint8_t idx = 0; idx < I2CF_CHANNEL_NUM; idx++) {
		const uint8_t addr = 0x10 + idx;   /* 互不相同，避开重址拒绝 */
		uint8_t dump_addr = 0;

		fill_pattern(bitmap, idx + 1);
		zassert_ok(linkedsemi_i2c_filter_fill_bitmap(dev, idx, addr, bitmap),
			   "fill idx=%u failed", idx);

		memset(dump, 0, sizeof(dump));
		zassert_ok(linkedsemi_i2c_filter_dump_bitmap(dev, idx, &dump_addr, dump),
			   "dump idx=%u failed", idx);
		zassert_equal(dump_addr, addr, "idx=%u addr mismatch", idx);
		zassert_mem_equal(dump, bitmap, LINKEDSEMI_I2C_F_REMAP_SIZE_BYTE,
				  "idx=%u bitmap mismatch", idx);
	}

	/* fill_bitmap 自动置位通道 enable（i2c_filter.c:181-183） */
	zassert_equal(sys_read32(base + SMBF_ADDRESS_ENABLE), BIT_MASK(I2CF_CHANNEL_NUM),
		      "SMBF_ADDRESS_ENABLE must be all-set after filling 16 channels");
}

ZTEST(i2cf, addr_7bit_field)
{
	uint32_t bitmap[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];
	uint32_t dump[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];
	const struct device *dev = i2cf_dev(3);
	uint8_t dump_addr = 0;

	/* 白名单地址字段只有 7 bit（reg_i2c_filter.h:57 `WHITELIST_ADDRESS : 7`）：
	 * 0x7f 是最大可回读值；0x80 的最高位落在 reserve0，回读被截断为 0x00。
	 * 该边界是驱动层字段位宽语义（非用例缩容）。 */
	i2cf_reset_all(dev);

	fill_pattern(bitmap, 0x11);
	zassert_ok(linkedsemi_i2c_filter_fill_bitmap(dev, 0, 0x7f, bitmap), "fill 0x7f");
	zassert_ok(linkedsemi_i2c_filter_dump_bitmap(dev, 0, &dump_addr, dump), "dump 0x7f");
	zassert_equal(dump_addr, 0x7f, "7-bit max must round-trip");

	fill_pattern(bitmap, 0x22);
	zassert_ok(linkedsemi_i2c_filter_fill_bitmap(dev, 1, 0x80, bitmap), "fill 0x80");
	dump_addr = 0xff;
	zassert_ok(linkedsemi_i2c_filter_dump_bitmap(dev, 1, &dump_addr, dump), "dump 0x80");
	zassert_equal(dump_addr, 0x00, "bit7 must be truncated by the 7-bit field");

	ZT_INFO("addr_7bit_field", "max_ok=%#x,ovf_readback=%#x", 0x7f, dump_addr);
}

ZTEST(i2cf, invalid_args)
{
	uint32_t bitmap[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];
	const struct device *dev = i2cf_dev(3);
	uint8_t addr = 0;

	i2cf_reset_all(dev);
	fill_pattern(bitmap, 0x33);

	/* idx 越界（i2c_filter.c:142 / :198） */
	zassert_equal(linkedsemi_i2c_filter_fill_bitmap(dev, I2CF_CHANNEL_NUM, 0x50, bitmap),
		      -EINVAL, "fill idx 16 must be -EINVAL");
	zassert_equal(linkedsemi_i2c_filter_dump_bitmap(dev, I2CF_CHANNEL_NUM, &addr, bitmap),
		      -EINVAL, "dump idx 16 must be -EINVAL");

	/* NULL bitmap（i2c_filter.c:145-148 / :201-204） */
	zassert_equal(linkedsemi_i2c_filter_fill_bitmap(dev, 0, 0x50, NULL),
		      -EINVAL, "fill NULL bitmap must be -EINVAL");
	zassert_equal(linkedsemi_i2c_filter_dump_bitmap(dev, 0, &addr, NULL),
		      -EINVAL, "dump NULL bitmap must be -EINVAL");
}

ZTEST(i2cf, duplicate_addr_rejected)
{
	uint32_t bitmap[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];
	const struct device *dev = i2cf_dev(3);

	i2cf_reset_all(dev);
	fill_pattern(bitmap, 0x44);

	/* fill 会顺带 enable 该通道（i2c_filter.c:181-183） */
	zassert_ok(linkedsemi_i2c_filter_fill_bitmap(dev, 0, 0x50, bitmap), "fill ch0");

	/* 另一路已使能通道用同址 → 拒绝（i2c_filter.c:157-164） */
	zassert_equal(linkedsemi_i2c_filter_fill_bitmap(dev, 1, 0x50, bitmap), -EINVAL,
		      "duplicate addr on enabled channel must be -EINVAL");

	/* 判据是对方的 **enable 位**：关掉 ch0 后同址放行 */
	zassert_ok(linkedsemi_i2c_filter_enable_channel(dev, 0, false), "disable ch0");
	zassert_ok(linkedsemi_i2c_filter_fill_bitmap(dev, 1, 0x50, bitmap),
		   "same addr must be allowed once the other channel is disabled");

	/* 同址填回自己（it_idx == idx）始终放行 */
	zassert_ok(linkedsemi_i2c_filter_fill_bitmap(dev, 1, 0x50, bitmap),
		   "re-filling own channel must be allowed");
}

ZTEST(i2cf, en_clears_table)
{
	uint32_t bitmap[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];
	uint32_t dump[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];
	const struct device *dev = i2cf_dev(3);
	uint8_t dump_addr = 0;

	i2cf_reset_all(dev);

	/* 16 通道全填非零 pattern */
	for (uint8_t idx = 0; idx < I2CF_CHANNEL_NUM; idx++) {
		fill_pattern(bitmap, idx + 1);
		zassert_ok(linkedsemi_i2c_filter_fill_bitmap(dev, idx, 0x20 + idx, bitmap),
			   "fill idx=%u", idx);
	}

	/* clr_tbl=true → 遍历 16 通道把命令表清零（i2c_filter.c:242-249） */
	zassert_ok(linkedsemi_i2c_filter_en(dev, false, false, true), "en clr_tbl");

	for (uint8_t idx = 0; idx < I2CF_CHANNEL_NUM; idx++) {
		memset(dump, 0xff, sizeof(dump));
		zassert_ok(linkedsemi_i2c_filter_dump_bitmap(dev, idx, &dump_addr, dump),
			   "dump idx=%u", idx);
		for (int w = 0; w < LINKEDSEMI_I2C_F_REMAP_SIZE_U32; w++) {
			zassert_equal(dump[w], 0u,
				      "idx=%u word=%d not cleared by clr_tbl", idx, w);
		}
	}
}

ZTEST(i2cf, filter_set_bits)
{
	const struct device *dev = i2cf_dev(3);
	const uintptr_t base = i2cf_reg_base(3);
	smbf_set_t s;

	i2cf_reset_all(dev);

	/* SMBUS_FILTER_SET 是负逻辑（i2c_filter.c:231-236）：
	 *   BLOCK_DISABLE  = filter_en ? 0 : 1
	 *   FILTER_DISABLE = wlist_en  ? 0 : 1
	 * 四种组合逐一回读。 */
	static const struct {
		bool filter_en, wlist_en;
		uint8_t block_disable, filter_disable;
	} cases[] = {
		{ true,  true,  0, 0 },
		{ true,  false, 0, 1 },
		{ false, true,  1, 0 },
		{ false, false, 1, 1 },
	};

	for (int i = 0; i < (int)ARRAY_SIZE(cases); i++) {
		zassert_ok(linkedsemi_i2c_filter_en(dev, cases[i].filter_en,
						    cases[i].wlist_en, false),
			   "en(%d,%d)", cases[i].filter_en, cases[i].wlist_en);
		s.value = sys_read32(base + SMBUS_FILTER_SET);
		zassert_equal(s.BLOCK_DISABLE, cases[i].block_disable,
			      "BLOCK_DISABLE for filter_en=%d", cases[i].filter_en);
		zassert_equal(s.FILTER_DISABLE, cases[i].filter_disable,
			      "FILTER_DISABLE for wlist_en=%d", cases[i].wlist_en);
	}

	/* MASTER_WRITE_MODE 恒为 0（驱动未使用该位） */
	zassert_equal(s.MASTER_WRITE_MODE, 0, "MASTER_WRITE_MODE must stay 0");
}

ZTEST(i2cf, scl_hold_time_roundtrip)
{
	const struct device *dev = i2cf_dev(3);
	const uintptr_t base = i2cf_reg_base(3);
	const uint16_t dt_hold = (uint16_t)DT_PROP(DT_NODELABEL(i2cf4), scl_hold_time);

	i2cf_reset_all(dev);

	/* SCL_HOLD_TIME 是 16 bit 字段（reg_i2c_filter.h:67） */
	static const uint16_t values[] = { 0x0000, 0x0001, 0x00ff, 0xffff, 0x1234 };

	for (int i = 0; i < (int)ARRAY_SIZE(values); i++) {
		smbf_control0_reg_t ctl;

		zassert_ok(linkedsemi_i2c_filter_config_scl_hold_time(dev, values[i]),
			   "set scl_hold_time=%#x", values[i]);
		ctl.value = sys_read32(base + SMBF_CONTROL0_REG);
		zassert_equal(ctl.SCL_HOLD_TIME, values[i],
			      "scl_hold_time %#x not round-tripped", values[i]);
	}

	/* 复位回 DT 值，避免影响后续用例的总线时序 */
	zassert_ok(linkedsemi_i2c_filter_config_scl_hold_time(dev, dt_hold), "restore");
}

ZTEST(i2cf, switch_master_filter)
{
	const struct device *dev = i2cf_dev(3);

	i2cf_reset_all(dev);

	/* ---- 前置态：cold_reset 收尾即 `pinctrl_apply_state(i2cf4, DEFAULT)`
	 *      （i2c_filter.c:331-336）⇒ PE02 应复用给 i2cf4（func2），未给 i2c4（func1）。*/
	zassert_equal(pad_func_en(I2CF4_SCL_PINMUX), 1u,
		      "PE02 must be muxed to i2cf4 after cold_reset（filter 态）");
	zassert_equal(pad_func_en(I2C4_SCL_PINMUX), 0u,
		      "PE02 must NOT be muxed to i2c4 after cold_reset");

	/* ---- → master：驱动作用 i2c4 的 pcfg ⇒ func1 置、func2 清 ---- */
	zassert_ok(linkedsemi_i2c_filter_switch_to_master(dev), "to master");
	zassert_equal(pad_func_en(I2C4_SCL_PINMUX), 1u,
		      "PE02 must be muxed to i2c4 after switch_to_master");
	zassert_equal(pad_func_en(I2CF4_SCL_PINMUX), 0u,
		      "PE02 must no longer be muxed to i2cf4 after switch_to_master"
		      "（两档必须互斥 —— 驱动先 per_func_disable_all 再使能新档）");

	/* 重复同向切换幂等：复用位不变 */
	zassert_ok(linkedsemi_i2c_filter_switch_to_master(dev), "to master (repeat)");
	zassert_equal(pad_func_en(I2C4_SCL_PINMUX), 1u,
		      "repeat switch_to_master must keep PE02 on i2c4");
	zassert_equal(pad_func_en(I2CF4_SCL_PINMUX), 0u,
		      "repeat switch_to_master must keep PE02 off i2cf4");

	/* ---- → filter：驱动作用 i2cf4 的 pcfg ⇒ func2 置、func1 清 ---- */
	zassert_ok(linkedsemi_i2c_filter_switch_to_filter(dev), "to filter");
	zassert_equal(pad_func_en(I2CF4_SCL_PINMUX), 1u,
		      "PE02 must be muxed back to i2cf4 after switch_to_filter");
	zassert_equal(pad_func_en(I2C4_SCL_PINMUX), 0u,
		      "PE02 must no longer be muxed to i2c4 after switch_to_filter");

	zassert_ok(linkedsemi_i2c_filter_switch_to_filter(dev), "to filter (repeat)");
	zassert_equal(pad_func_en(I2CF4_SCL_PINMUX), 1u,
		      "repeat switch_to_filter must keep PE02 on i2cf4");

	/* 再走一轮完整往返，确认可反复切换（驱动正确时必过；pinctrl 句柄用错 /
	 * 切了别的 pad / 只改返回值不改复用位 ⇒ 必不过） */
	zassert_ok(linkedsemi_i2c_filter_switch_to_master(dev), "to master again");
	zassert_equal(pad_func_en(I2C4_SCL_PINMUX), 1u, "PE02 back on i2c4");
	zassert_ok(linkedsemi_i2c_filter_switch_to_filter(dev), "to filter again");
	zassert_equal(pad_func_en(I2CF4_SCL_PINMUX), 1u, "PE02 back on i2cf4");

	ZT_INFO("switch_master_filter", "PE02_func_i2c4=%u,PE02_func_i2cf4=%u",
		pad_func_en(I2C4_SCL_PINMUX), pad_func_en(I2CF4_SCL_PINMUX));
}

/*
 * 白盒观测视图：驱动 `struct linkedsemi_i2c_filter_data`
 *   = { struct k_mutex lock; i2c_filter_callback_t cb; void *user_data; }
 * （i2c_filter.c:44-48）。该结构**未**在 i2c_filter.h 导出、也无任何 getter，
 * 而驱动 `register_callback` 是**纯指针落地且恒返回 0**（:69-80）——
 * 只看返回值判不出任何东西（恒真断言）。
 * 这里按**同一布局**复刻视图后直接读 `dev->data` 的字段，使"回调真的落地"可证伪。
 * 布局若漂移，读到的值不会等于我们注册的函数指针 ⇒ 本项 **FAIL**（响亮，
 * 不会假 PASS），届时同步该视图即可。
 */
struct i2cf_data_view {
	struct k_mutex lock;
	i2c_filter_callback_t cb;
	void *user_data;
};

static void i2cf_precise_cb(const struct device *dev, uint32_t callback_idx,
			    void *user_data, void *drv_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(callback_idx);
	ARG_UNUSED(user_data);
	ARG_UNUSED(drv_data);
}

ZTEST(i2cf, callback_register)
{
	const struct device *dev = i2cf_dev(3);
	struct i2cf_data_view *view = (struct i2cf_data_view *)dev->data;

	/* 先清空，保证随后"读到非 NULL"确由本项注册造成 */
	view->cb = NULL;
	view->user_data = NULL;

	/* 注册一个非 NULL 回调 + 非 NULL user_data：两者都必须真的落到 dev->data。
	 * 驱动正确时必过；回调字段被丢弃 / 写错位置 / 只改返回值时必不过。 */
	zassert_ok(linkedsemi_i2c_filter_register_callback(dev, 0, i2cf_precise_cb,
							   (void *)dev),
		   "register callback");
	zassert_equal_ptr(view->cb, i2cf_precise_cb,
			  "cb field did not land in dev->data (got %p)", view->cb);
	zassert_equal_ptr(view->user_data, dev,
			  "user_data field did not land in dev->data (got %p)", view->user_data);

	/* 覆盖注册 NULL：必须清掉旧值（驱动 :76-77 无条件赋值） */
	zassert_ok(linkedsemi_i2c_filter_register_callback(dev, 0, NULL, NULL),
		   "register NULL callback");
	zassert_is_null(view->cb, "cb must be cleared by a NULL registration");
	zassert_is_null(view->user_data, "user_data must be cleared by a NULL registration");

	/* 4 个实例都能注册（驱动无 idx 上界约束，:69-80 恒接受）：
	 * 逐个实例验证回调字段真的落在**各自的** dev->data 上 */
	for (int i = 0; i < I2CF_INST_NUM; i++) {
		const struct device *d = i2cf_dev(i);

		if (d == NULL) {
			continue;      /* 该实例按拓扑未使能（见 app.overlay） */
		}
		struct i2cf_data_view *v = (struct i2cf_data_view *)d->data;

		zassert_ok(linkedsemi_i2c_filter_register_callback(d, 0, i2cf_precise_cb,
								   (void *)d),
			   "register on i2cf%d", i + 1);
		zassert_equal_ptr(v->cb, i2cf_precise_cb,
				  "i2cf%d cb field did not land", i + 1);
		zassert_equal_ptr(v->user_data, d,
				  "i2cf%d user_data field did not land", i + 1);
	}
}

/* ---- 并发：k_mutex 保护 fill/dump 的 idx→寄存器 选择序列 ---- */

struct conc_arg {
	const struct device *dev;
	uint8_t first;
	uint8_t count;
	int err;
};

static void conc_worker(void *p1, void *p2, void *p3)
{
	struct conc_arg *a = (struct conc_arg *)p1;
	uint32_t bm[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];
	uint32_t dp[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];
	int rc = 0;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* 多轮往返：fill 写 SMBF_ADDRESS_INDEX 后连续写 8 个命令字，
	 * 若 mutex 失效，另一线程会在中途改掉 INDEX → 命令字落到别的通道 */
	for (int round = 0; round < 4; round++) {
		for (uint8_t i = 0; i < a->count; i++) {
			const uint8_t idx = a->first + i;
			const uint8_t addr = 0x30 + idx;
			uint8_t dump_addr = 0;

			fill_pattern(bm, (uint8_t)(idx + 1 + round));
			rc |= linkedsemi_i2c_filter_fill_bitmap(a->dev, idx, addr, bm);

			memset(dp, 0, sizeof(dp));
			rc |= linkedsemi_i2c_filter_dump_bitmap(a->dev, idx, &dump_addr, dp);

			if (dump_addr != addr ||
			    memcmp(bm, dp, LINKEDSEMI_I2C_F_REMAP_SIZE_BYTE) != 0) {
				rc |= 0x4000;
			}
		}
	}

	a->err = rc;
}

ZTEST(i2cf, concurrent_fill)
{
	const struct device *dev = i2cf_dev(3);
	static K_THREAD_STACK_DEFINE(stk_a, 2048);
	static K_THREAD_STACK_DEFINE(stk_b, 2048);
	struct k_thread ta, tb;
	struct conc_arg aa = { .dev = dev, .first = 0, .count = 8, .err = -1 };
	struct conc_arg ab = { .dev = dev, .first = 8, .count = 8, .err = -1 };

	i2cf_reset_all(dev);

	k_thread_create(&ta, stk_a, K_THREAD_STACK_SIZEOF(stk_a), conc_worker,
			&aa, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	k_thread_create(&tb, stk_b, K_THREAD_STACK_SIZEOF(stk_b), conc_worker,
			&ab, NULL, NULL, K_PRIO_PREEMPT(9), 0, K_NO_WAIT);

	k_thread_join(&ta, K_FOREVER);
	k_thread_join(&tb, K_FOREVER);

	zassert_equal(aa.err, 0, "thread A (idx 0..7) rc=%d", aa.err);
	zassert_equal(ab.err, 0, "thread B (idx 8..15) rc=%d", ab.err);

	ZT_INFO("concurrent_fill", "threads=2,rounds=4,channels=16");
}

/* ---- 外部路径：i2c4 master 模式访问总线上的外接 EEPROM ---- */

ZTEST(i2cf, master_eeprom_rw)
{
	/*
	 * 外部: I2C 总线 PE02(SCL)/PB00(SDA)（板上 I2C4 ↔ I2CF4 SCLO/SDAO 同管脚复用）
	 *       上挂片内 target（@0x33，见 app.overlay）。
	 * 破坏性：改写 EEPROM 偏移 0x00..0x0f —— 用例内先备份、后恢复。
	 */
	const struct device *dev = i2cf_dev(3);
	const struct device *master = i2cf_i2c_dev(dev);
	uint8_t orig[16];
	uint8_t pattern[16];
	uint8_t readback[16];
	uint8_t slave = 0;
	int ret;

	i2cf_reset_all(dev);

	zassert_true(device_is_ready(master), "i2c4 master not ready");

	/* 依赖外部从设备（用户已降级为可选）——扫描不到就 skip，不算失败。 */
	slave = i2cf_first_slave_bypass(dev);
	if (slave == 0) {
		ztest_test_skip();
	}

	zassert_ok(linkedsemi_i2c_filter_switch_to_master(dev), "switch to master");

	/* 1) 备份原内容 */
	ret = i2c_burst_read(master, slave, 0x00, orig, sizeof(orig));
	zassert_ok(ret, "eeprom read(backup) ret=%d (接线/地址 0x%02x 待确认)",
		   ret, slave);

	/* 2) 写 pattern 后回读比对 */
	for (uint8_t i = 0; i < sizeof(pattern); i++) {
		pattern[i] = (uint8_t)(0xA5u ^ i);
	}
	zassert_ok(i2c_burst_write(master, slave, 0x00, pattern, sizeof(pattern)),
		   "eeprom write");
	zassert_ok(i2c_burst_read(master, slave, 0x00, readback, sizeof(readback)),
		   "eeprom read");
	zassert_mem_equal(readback, pattern, sizeof(pattern), "eeprom data mismatch");

	ZT_INFO("master_eeprom_rw", "addr=%#x,len=%u", slave, sizeof(pattern));

	/* 3) 恢复原内容并把管脚交回滤波器 */
	zassert_ok(i2c_burst_write(master, slave, 0x00, orig, sizeof(orig)),
		   "eeprom restore");
	zassert_ok(linkedsemi_i2c_filter_switch_to_filter(dev), "switch back to filter");
}

/* ========================================================================== */
/* 行为层用例（上游 master 侧）                                                */
/*                                                                            */
/* 拓扑（推导依据见 tests/COVERAGE.md「拓扑推导」）：                          */
/*   i2c3 --跳线--> i2cf4.SCLI/SDAI  [滤波器门控]  i2cf4.SCLO/SDAO --> EEPROM  */
/*   （i2c4 与 SCLO/SDAO 同引脚，是旁路 master，不参与本组用例）               */
/* ========================================================================== */

/*
 * 上游 master：取自 overlay 的 alias `i2cfupstream`。
 *
 * ⚠️ **谁被跳线到 i2cf4 的 SCLI/SDAI 完全由外部接线决定，仓库里查不到**
 *    （DT 只固定 i2cf4.i2c = i2c4，那是 O 侧的旁路控制器，不可能是被门控的上游）。
 *    当前 alias 取值见 app.overlay 文件头注释；换实例只改 overlay 一行，
 *    本文件无需改动。
 */
static const struct device *i2cf_upstream(void)
{
	return DEVICE_DT_GET(DT_ALIAS(i2cfupstream));
}

/*
 * ISR 观测：回调在**中断上下文**执行 —— 只置标志，绝不打日志/不拿锁
 * （SPEC_REFERENCE §6.1.2）。
 */
static volatile bool isr_fired;
static volatile uint32_t isr_count;

static void i2cf_isr_cb(const struct device *dev, uint32_t callback_idx,
			void *user_data, void *drv_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(callback_idx);
	ARG_UNUSED(user_data);
	ARG_UNUSED(drv_data);

	isr_fired = true;
	isr_count++;
}

/* 256 位命令位图全置 1 = 该地址下所有命令码均放行 */
static void bitmap_all_on(uint32_t bm[LINKEDSEMI_I2C_F_REMAP_SIZE_U32])
{
	for (int i = 0; i < LINKEDSEMI_I2C_F_REMAP_SIZE_U32; i++) {
		bm[i] = 0xffffffffu;
	}
}

/*
 * 经**旁路通路**（i2c4 master 模式直驱 SCLO/SDAO 侧）扫描下游从设备：
 * 在标准 7-bit 段 [I2CF_SCAN_ADDR_MIN, I2CF_SCAN_ADDR_MAX] 上逐地址探测，
 * 把应答的地址写入 out[]，返回应答个数（≤ max）。
 *
 * 旁路通路**不经滤波器**，故结果与白名单配置无关，可作为"下游挂了什么器件"
 * 的独立清单 —— 也用来把"滤波器拦住了"与"根本没有从设备应答"区分开。
 *
 * ⚠️ 用户裁定（2026-09-17）：外接 EEPROM 优先级降为**最低**。返回 0（无任何
 *    应答）是**合法结果**，任何用例**不得**因此判失败 —— 依赖从设备的用例改走
 *    `ztest_test_skip()`。
 */
static int i2cf_scan_slaves_bypass(const struct device *dev, uint8_t *out, int max)
{
	const struct device *down = i2cf_i2c_dev(dev);
	uint8_t v = 0;
	int n = 0;

	zassert_ok(linkedsemi_i2c_filter_switch_to_master(dev), "scan: switch to master");

	for (uint16_t addr = I2CF_SCAN_ADDR_MIN; addr <= I2CF_SCAN_ADDR_MAX; addr++) {
		if (i2c_read(down, &v, 1, (uint8_t)addr) == 0) {
			if (n < max) {
				out[n++] = (uint8_t)addr;
			} else {
				break;   /* 结果超上限，保底截断（标准段内不会发生） */
			}
		}
	}

	zassert_ok(linkedsemi_i2c_filter_switch_to_filter(dev), "scan: restore filter");
	return n;
}

/* 扫描并返回第一个应答地址；无应答返回 0（= 下游无从设备） */
static uint8_t i2cf_first_slave_bypass(const struct device *dev)
{
	uint8_t found[I2CF_SCAN_MAX];
	int n = i2cf_scan_slaves_bypass(dev, found, I2CF_SCAN_MAX);

	return (n > 0) ? found[0] : 0;
}

/* filter 模式：拦违规访问 + 白名单生效（BLOCK_DISABLE=0, FILTER_DISABLE=0） */
static void i2cf_filter_on(const struct device *dev)
{
	zassert_ok(linkedsemi_i2c_filter_en(dev, true, true, false), "filter on failed");
}

/*
 * 监视/透传模式：不拦任何访问、白名单不生效
 * （BLOCK_DISABLE=1, FILTER_DISABLE=1 —— 见 i2c_filter.c:231-236 的负逻辑映射）。
 * 用作"自证"对照：同一笔访问在 filter off 下成功 ⇒ 之前的失败确由 filter 造成，
 * 而非接线/器件问题。
 */
static void i2cf_filter_off(const struct device *dev)
{
	zassert_ok(linkedsemi_i2c_filter_en(dev, false, false, false), "filter off failed");
}

/* 清 pending 中断，并把 ISR 观测归零（在每笔事务前调用，保证归因唯一） */
static void i2cf_isr_arm(const struct device *dev, int inst)
{
	uintptr_t base = i2cf_reg_base(inst);

	sys_write32(0x3, base + INTR_CLR);   /* ADDRESS|COMMAND beyond whitelist */
	isr_fired = false;
	isr_count = 0;
}

/* 给 ISR 一点时间（中断异步于 i2c 事务返回） */
static bool i2cf_isr_wait(int ms)
{
	for (int i = 0; i < ms / 5 && !isr_fired; i++) {
		k_msleep(5);
	}
	return isr_fired;
}

ZTEST(i2cf, isr_addr_beyond_whitelist)
{
	/*
	 * 外部: **仅需上游 master**（alias `i2cfupstream`）跳线到
	 *       i2cf4.SCLI/SDAI(PE03/PB02)。**不需要下游从设备** ——
	 *       本项判据全部来自滤波器自身与该笔被拦访问，零外部器件依赖。
	 *
	 * 场景：白名单**为空**（一路通道都不使能，`i2cf_reset_all` 的结果）
	 *       ⇒ 任何地址访问都属 ADDRESS_BEYOND_WHITELIST
	 *       ⇒ 滤波器产生中断（ISR 路径实触发）。
	 *
	 * 判据：① 回调被调用（ISR 真的进了驱动 `:102-104`）；
	 *       ② `SMBF_NONWHITELIST` 被锁存了**非零**信息（滤波器确实判定了一次违规）；
	 *       ③ 主机侧看到失败（滤波器不转发）。
	 *
	 * 说明：本项**不做**"关 filter 后同址应成功"的自证对照 —— 那需要下游有
	 *       从设备应答（见 `filter_blocks_nonwhitelist`）；本项用"滤波器自身
	 *       锁存的违规信息"替代，同样能把"被拦"与"跳线缺失"区分开。
	 */
	const struct device *dev = i2cf_dev(3);            /* i2cf4 */
	const uintptr_t base = i2cf_reg_base(3);
	const struct device *up = i2cf_upstream();
	/* 白名单为空 ⇒ 任意地址皆越界；**取片内 target 的地址**（链路里的真实从设备）。
	 * ★ 2026-09-22：**不得用 0x50/0x51 这类 EEPROM 惯例地址** —— 用户板上确实挂了
	 *   这类器件，会撞址（详见 app.overlay 的说明）。0x33 为两侧实测空闲地址。 */
	const uint8_t target = 0x33;
	uint8_t val = 0;
	int ret_blocked;
	smbf_nonwhitelist_t nw;

	zassert_true(device_is_ready(up), "upstream master not ready");

	i2cf_reset_all(dev);                               /* 全通道关闭 = 空白名单 */
	zassert_ok(linkedsemi_i2c_filter_register_callback(dev, 0, i2cf_isr_cb, NULL),
		   "register ISR callback");
	zassert_ok(linkedsemi_i2c_filter_switch_to_filter(dev), "switch to filter");
	i2cf_filter_on(dev);
	i2cf_isr_arm(dev, 3);

	/* 上游 master 访问未授权地址 —— 期望被拦并触发中断 */
	ret_blocked = i2c_read(up, &val, 1, target);

	/* ① ISR 是否触发 */
	zassert_true(i2cf_isr_wait(100),
		     "filter ISR not fired: upstream access to %#x did not reach the "
		     "filter? (ret=%d, 检查上游 master → i2cf4 SCLI/SDAI(PE03/PB02) 跳线)",
		     target, ret_blocked);
	zassert_true(isr_count >= 1, "ISR count=%u", (unsigned)isr_count);

	/* ② 滤波器锁存了违规信息（字段语义未在寄存器头文档化 ⇒ 只断言"非零"，
	 *    逐字段值经 ZT_INFO 上报，不做断言） */
	nw.value = sys_read32(base + SMBF_NONWHITELIST);
	zassert_not_equal(nw.value, 0u,
			  "filter must latch non-whitelist info after a violation, got 0 "
			  "(ISR 触发了但未锁存 ⇒ 疑为伪中断；检查 INTR_CLR 与时序)");

	/* ③ 主机侧必须看到失败（滤波器不转发 ⇒ 无从 ACK） */
	zassert_true(ret_blocked < 0,
		     "upstream access to non-whitelisted %#x must fail, got %d",
		     target, ret_blocked);

	ZT_INFO("isr_addr_beyond_whitelist",
		"target=%#x,blocked_ret=%d,isr_count=%u,nw=%#x,nw_addr=%#x,nw_cmd=%#x",
		target, ret_blocked, (unsigned)isr_count, nw.value,
		nw.ERROR_ADDRESS, nw.ERROR_COMMAND);

	/* 收尾：恢复监视态 + 管脚交回滤波器 */
	i2cf_filter_off(dev);
	zassert_ok(linkedsemi_i2c_filter_switch_to_filter(dev), "restore filter mode");
}

ZTEST(i2cf, filter_blocks_nonwhitelist)
{
	/*
	 * 外部: 同 isr_addr_beyond_whitelist。
	 *
	 * 场景：白名单**非空但只授权另一地址**（0x2A）⇒ 访问从设备的 0x33
	 *       属"黑名单"（未授权）⇒ 被拦住 + 主机 NACK。
	 * 判据：① 0x33 访问失败；② 自证：关 filter 后同一访问成功。
	 * 与 `isr_addr_beyond_whitelist` 的区别：那项验 ISR 路径（空白名单），
	 * 本项验"白名单存在时的选择性拦截"语义。
	 */
	const struct device *dev = i2cf_dev(3);
	uint32_t bm[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];
	const struct device *up = i2cf_upstream();
	uint8_t slave = 0;
	uint8_t authorized = 0;   /* 另一候选地址：被授权，但（在本链路假设下）无器件 */
	uint8_t val = 0;
	int ret_blocked, ret_free;

	zassert_true(device_is_ready(up), "upstream master i2c3 not ready");

	i2cf_reset_all(dev);

	/* 依赖外部从设备（用户已降级为可选）——本项需 ACK 源才能把"被拦"与
	 * "无器件"区分开；扫描不到就 skip，不算失败。 */
	slave = i2cf_first_slave_bypass(dev);
	if (slave == 0) {
		ztest_test_skip();
	}
	/* 授权"另一个"地址（取扫描段内与 slave 不同的固定值），使 slave 成为未授权地址。
	 * ★ 2026-09-22：**避开 EEPROM 惯例地址（0x50~0x57）** —— 本值只写进白名单表、
	 *   **不过总线**，故无需关心是否有器件占用；避开惯例地址纯粹是为了不与板上
	 *   真实器件混同、便于阅读。 */
	authorized = (slave == 0x2A) ? 0x2B : 0x2A;

	zassert_ok(linkedsemi_i2c_filter_switch_to_filter(dev), "switch to filter");

	/* 白名单：通道 0 = authorized（另一候选），全部命令码放行
	 * ⇒ slave 地址成为"未授权地址" */
	bitmap_all_on(bm);
	zassert_ok(linkedsemi_i2c_filter_fill_bitmap(dev, 0, authorized, bm),
		   "fill whitelist for %#x", authorized);
	i2cf_filter_on(dev);

	ret_blocked = i2c_read(up, &val, 1, slave);
	zassert_true(ret_blocked < 0,
		     "non-whitelisted %#x must be blocked (whitelist=%#x), got %d",
		     slave, authorized, ret_blocked);

	/* 自证：filter off ⇒ 拦截消失 */
	i2cf_filter_off(dev);
	k_msleep(5);
	ret_free = i2c_read(up, &val, 1, slave);
	zassert_ok(ret_free,
		   "with filter OFF %#x must be reachable, got %d "
		   "(EEPROM 未在位 / 跳线缺失; 地址未经实测)",
		   slave, ret_free);

	ZT_INFO("filter_blocks_nonwhitelist",
		"whitelist=%#x,target=%#x,blocked_ret=%d,free_ret=%d",
		authorized, slave, ret_blocked, ret_free);

	i2cf_filter_off(dev);
	zassert_ok(linkedsemi_i2c_filter_switch_to_filter(dev), "restore filter mode");
}

ZTEST(i2cf, filter_passthrough_whitelist)
{
	/*
	 * 外部: 同 isr_addr_beyond_whitelist（需外接 EEPROM 在位）。
	 *
	 * 场景：白名单授权 EEPROM 地址 + 全命令码 ⇒ 上游 master 的读写**透传**到
	 *       EEPROM ⇒ 数据逐字节可比对。这是"放行"方向的正对照。
	 * 判据：上游经滤波器读回的数据 == 预先经旁路通路（i2c4 master 模式）写入的 pattern。
	 *
	 * 破坏性受控：写前备份偏移 0x00..0x0f，用例末尾恢复。
	 * ⚠️ 从设备地址 0x33 = 片内 target（见 app.overlay）；
	 *    偏移与容量同样未实测 —— 失败信息里带实际读回字节以便归因。
	 */
	const struct device *dev = i2cf_dev(3);
	const struct device *down = i2cf_i2c_dev(dev);     /* i2c4：旁路 master */
	const struct device *up = i2cf_upstream();         /* = i2c2：上游主机（见 app.overlay alias） */
	uint32_t bm[LINKEDSEMI_I2C_F_REMAP_SIZE_U32];
	uint8_t orig[8];
	uint8_t pattern[8];
	uint8_t readback[8];
	uint8_t slave = 0;
	int ret;

	zassert_true(device_is_ready(down), "paired bypass master i2c4 not ready");
	zassert_true(device_is_ready(up), "upstream master i2c3 not ready");

	i2cf_reset_all(dev);
	bitmap_all_on(bm);

	/* 0) 依赖外部从设备（用户已降级为可选）——本项需从设备提供已知数据；
	 *    扫描不到就 skip，不算失败。 */
	slave = i2cf_first_slave_bypass(dev);
	if (slave == 0) {
		ztest_test_skip();
	}

	/* 1) 备份原内容（旁路通路，不经滤波器） */
	zassert_ok(linkedsemi_i2c_filter_switch_to_master(dev), "switch to master (bypass)");
	ret = i2c_burst_read(down, slave, 0x00, orig, sizeof(orig));
	zassert_ok(ret,
		   "从设备@%#x 备份读失败 (ret=%d) —— 片内 target 未注册/未就位"
		   "（地址 0x33 = overlay 的 eeprom@33，见 app.overlay）",
		   slave, ret);

	/* 2) 写入已知 pattern（旁路通路；EEPROM 写周期需等待） */
	for (uint8_t i = 0; i < sizeof(pattern); i++) {
		pattern[i] = (uint8_t)(0x5Au + i);
	}
	zassert_ok(i2c_burst_write(down, slave, 0x00, pattern, sizeof(pattern)),
		   "EEPROM pattern write failed");
	k_msleep(20);   /* EEPROM 内部写周期（典型 5 ms，留裕量） */

	/* 3) 切回滤波器 + 授权 EEPROM 地址（全命令码） */
	zassert_ok(linkedsemi_i2c_filter_switch_to_filter(dev), "switch to filter");
	zassert_ok(linkedsemi_i2c_filter_fill_bitmap(dev, 0, slave, bm),
		   "authorize %#x in whitelist", slave);
	i2cf_filter_on(dev);

	/* 4) 上游 master 经滤波器读回，与 pattern 比对 */
	memset(readback, 0, sizeof(readback));
	ret = i2c_burst_read(up, slave, 0x00, readback, sizeof(readback));
	zassert_ok(ret,
		   "upstream passthrough read failed (ret=%d): 白名单已授权 %#x，"
		   "检查 SCLI/SDAI 跳线与 filter 透传通路",
		   ret, slave);
	zassert_mem_equal(readback, pattern, sizeof(pattern),
			  "passthrough data mismatch: got %02x %02x %02x %02x %02x %02x %02x %02x",
			  readback[0], readback[1], readback[2], readback[3],
			  readback[4], readback[5], readback[6], readback[7]);

	ZT_INFO("filter_passthrough_whitelist",
		"addr=%#x,len=%u,rd=%02x%02x%02x%02x",
		slave, (uint32_t)sizeof(readback),
		readback[0], readback[1], readback[2], readback[3]);

	/* 5) 恢复原内容（旁路通路）并交回滤波器 */
	i2cf_filter_off(dev);
	zassert_ok(linkedsemi_i2c_filter_switch_to_master(dev), "switch to master (restore)");
	zassert_ok(i2c_burst_write(down, slave, 0x00, orig, sizeof(orig)),
		   "EEPROM restore failed —— 偏移 0x00..0x0f 可能已被改写");
	k_msleep(20);
	zassert_ok(linkedsemi_i2c_filter_switch_to_filter(dev), "restore filter mode");
}

ZTEST(i2cf, slave_addr_scan)
{
	/*
	 * 外部: 下游侧（SCLO/SDAO，与 i2c4 同引脚）总线上的从设备清单。
	 *       走**旁路通路**（i2c4 master 模式直驱），不经滤波器。
	 *
	 * ⚠ **本项是"观测项"，无判别力（2026-09-18 F1 波如实标注）** ⚠
	 *
	 * 用户裁定（2026-09-17）：外接 EEPROM 优先级降为最低，**零应答是合法结果**
	 * ⇒ 不得断言"必须发现至少 1 个地址"。
	 *
	 * 原实现的两条断言（`found[i]` 在扫描区间内 / 两两不重复）是**恒真**的：
	 *   - 范围：`found[]` 由 `i2cf_scan_slaves_bypass()` 的循环变量 `addr` 赋入
	 *     （本文件 :584-591），而循环界就是 `[I2CF_SCAN_ADDR_MIN, MAX]` ⇒ 必在范围内；
	 *   - 不重复：每个地址只 `i2c_read` 探测一次、成功才追加 ⇒ 必不重复。
	 * 两条都只是"扫描实现自洽"，任何驱动缺陷都不会让它们失败 —— 属**假断言**，
	 * 本轮已删除（保留假断言比没有更糟：给人虚假的安全感）。
	 *
	 * **为何无法给出判别性断言**：本项的"结果"完全由**外部接线/器件是否存在**
	 * 决定，平台内无任何可注入的对照量；零应答又已被裁定为合法 ⇒ 不存在一个
	 * "驱动正确时必过、典型失效时必不过"的判据。故本项**只保留**：
	 *   ① 前置真断言：旁路 master 就绪（真的会 FAIL）；
	 *   ② ZT_INFO 上报完整应答清单，供 P6 实机对照实际接线（人工判读）。
	 *
	 * **改名风险（未改名，留父会话裁定）**：`slave_addr_scan` 同时登记在
	 * `tests/run_test.py:70` 与 `tests/scripts/dump_results.py:61` 的 `TEST_NAMES`
	 * 里；本轮那两个文件**不在写域**（另一 agent 正在重写）⇒ 改名会立即造成
	 * 名字表失配（KNOWN_ISSUES #59 的"张冠李戴"）。故此处**保留原名**，
	 * 仅以本注释 + `tests/COVERAGE.md` 如实标注其无判别力。
	 */
	const struct device *dev = i2cf_dev(3);            /* i2cf4 */
	const struct device *down = i2cf_i2c_dev(dev);     /* i2c4：旁路 master */
	uint8_t found[I2CF_SCAN_MAX];
	char list[I2CF_SCAN_MAX * 3 + 2];
	int n, pos = 0;

	/* ① 前置：旁路 master 必须就绪（这是本项唯一的真判据） */
	zassert_true(device_is_ready(down), "bypass master i2c4 not ready");

	i2cf_reset_all(dev);
	n = i2cf_scan_slaves_bypass(dev, found, I2CF_SCAN_MAX);

	/* ② 观测：把应答清单上报（人工判读，不做断言） */
	for (int i = 0; i < n; i++) {
		pos += snprintf(list + pos, sizeof(list) - (size_t)pos, "%02x ", found[i]);
	}
	if (pos == 0) {
		(void)snprintf(list, sizeof(list), "(none)");
	}

	ZT_INFO("slave_addr_scan", "range=%02x..%02x,count=%d,addrs=%s",
		I2CF_SCAN_ADDR_MIN, I2CF_SCAN_ADDR_MAX, n, list);

	/* 收尾：把管脚交回滤波器。返回值恒 0（i2c_filter.c:278-290），**非判据**，
	 * 仅作为流程步骤保留。 */
	zassert_ok(linkedsemi_i2c_filter_switch_to_filter(dev), "restore filter mode");
}

ZTEST_SUITE(i2cf, NULL, NULL, NULL, NULL, NULL);
