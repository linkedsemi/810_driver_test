/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * I3C/I2C 模块切换测试 —— cpu2 controller 侧测试流程。
 *
 * 测试顺序（模式跟踪 ctrl_mode，各测试函数开头校验预期模式）：
 *   1. pinctrl_states     自主验证（双核各自验证，不经总线命令）
 *   2. i3c_baseline       I3C：DAA + 16B 写读校验
 *   3. switch_i3c_to_i2c  'S' 切换命令 + 双端延时 + I2C 探测
 *   4. i2c_rw_1/16/17/100 I2C：1/16/17/100 字节写读校验（17 跨 FIFO 边界）
 *   5. i2c_nack_bad_addr  I2C 负路径：访问 0x70 预期 NACK
 *   6. switch_i2c_to_i3c  'Y' 切回 + 重新 DAA + I3C 复验
 *   7. switch_stress      I3C<->I2C 循环切换 5 轮，每轮双模式基本读写
 *
 * 每项测试 begin/end 经命令通道通知 target 记账（'B' <id> / 'E' <id>），
 * 双核各自独立判定，CTRL+TGT 双 PASS 才算过（CLAUDE.md §6）。
 * pinctrl_states 例外：双方自主验证，不发 'B'/'E'。
 */

#include "i3c_i2c_controller_test.h"
#include "i3c_i2c_switch_test_protocol.h"
#include "test_results.h"
#include "ztest_glue.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(i3c_i2c_master_test_impl, LOG_LEVEL_INF);

/* ============ 全局状态 ============ */

enum ctrl_mode {
	MODE_I3C = 0,
	MODE_I2C = 1,
};

static enum ctrl_mode ctrl_mode = MODE_I3C;
static struct i3c_device_desc *target_desc;

/* pinctrl 配置（i3c5 节点双状态：default=I3C, pinmux_i2c=I2C）。
 * 应用侧引用驱动生成的 pinctrl 配置需显式 extern 声明
 * （PINCTRL_DT_DEV_CONFIG_DECLARE，CONFIG_PINCTRL_NON_STATIC）。
 * 纯 I2C case（CONFIG_I3C_I2C_PURE_I2C_TEST）禁用 i3c5，无 pinctrl 结构体。 */
#ifndef CONFIG_I3C_I2C_PURE_I2C_TEST
PINCTRL_DT_DEV_CONFIG_DECLARE(DT_NODELABEL(i3c5));
static const struct pinctrl_dev_config *ctrl_pcfg =
	PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(i3c5));
#endif

/* 当前测试的失败累计：begin 时同 id 不清（STRESS 任一轮失败即败） */
static enum i3c_i2c_test_id ctrl_cur_id;
static enum i3c_i2c_test_id ctrl_last_id = I3C_I2C_TEST_COUNT;
static bool ctrl_failed;
static char ctrl_fail_reason[TR_REASON_LEN];

/* I2C 读的最大长度（i2c_rw_100 用） */
#define I2C_RW_MAX 100U

/* ============ 命令通道（按当前模式选 I3C/I2C 帧） ============ */

static int ctrl_send_cmd(const uint8_t *buf, size_t len)
{
	if (ctrl_mode == MODE_I3C) {
		if (target_desc == NULL) {
			return -ENODEV;
		}
		return ctrl_i3c_write(target_desc, buf, len);
	}

	return i2c_write(ctrl_i2c_dev, buf, len, I2C_SWITCH_SLAVE_ADDR);
}

/* ============ 失败记账辅助 ============ */

static void ctrl_set_fail(const char *reason_fmt, ...)
{
	va_list ap;

	ctrl_failed = true;
	va_start(ap, reason_fmt);
	vsnprintk(ctrl_fail_reason, sizeof(ctrl_fail_reason), reason_fmt, ap);
	va_end(ap);
}

/* ============ 测试 begin/end（'B'/'E' 命令 + token + 结果记账） ============ */

static void ctrl_test_begin(enum i3c_i2c_test_id id, bool via_bus)
{
	I3C_I2C_CTRL_START_VAR(i3c_i2c_test_names[id]);

	ctrl_cur_id = id;
	if (id != ctrl_last_id) {
		/* 新测试清失败累计；同 id 多轮 begin（STRESS）不清 */
		ctrl_failed = false;
		ctrl_fail_reason[0] = '\0';
		ctrl_last_id = id;
	}

	if (via_bus) {
		const uint8_t buf[2] = { CMD_TGT_BEGIN, (uint8_t)id };

		if (ctrl_send_cmd(buf, 2) != 0) {
			ctrl_set_fail("send_begin");
		}
	}
}

static void ctrl_test_end(enum i3c_i2c_test_id id, bool via_bus)
{
	if (via_bus) {
		const uint8_t buf[2] = { CMD_TGT_END, (uint8_t)id };

		if (ctrl_send_cmd(buf, 2) != 0) {
			ctrl_set_fail("send_end");
		}
	}

	if (ctrl_failed) {
		I3C_I2C_CTRL_FAIL_VAR(i3c_i2c_test_names[id], ctrl_fail_reason);
		tr_record(id, TR_FAIL, "%s", ctrl_fail_reason);
	} else {
		I3C_I2C_CTRL_PASS_VAR(i3c_i2c_test_names[id]);
		tr_record(id, TR_PASS, "");
	}
}

/* ============ 切换辅助（pinmux apply + 模式跟踪，纯 I2C 无切换） ============ */

#ifndef CONFIG_I3C_I2C_PURE_I2C_TEST
/* ==== 临时调试探针：快照 GPIOJ 全部 pad 配置 + 功能路由位 ====
 * [0]  PU1_PU0   @0x4006f2a4  APP_PMU IO_CFG[9]（GPIOJ：PJ10/11 = i3c5 引脚）
 * [1]  PD_PU2    @0x4006f2a8
 * [2]  IEN1_IEN0 @0x4006f2ac
 * [3]  DS1_DS0   @0x4006f2b0
 * [4]  AE_DS2    @0x4006f2b4
 * [5]  OD_FIR    @0x4006f2b8
 * [6]  SL_ST     @0x4006f2bc
 * [7]  IO_FUNC[1][4] @0x400610b0  FUNC1(I2C) 使能位 GPIOJ 高半区 bit26/27
 * [8]  IO_FUNC[2][4] @0x400610d0  FUNC2(I3C) 使能位 GPIOJ 高半区 bit26/27
 * [9]  FUNC_SEL[9][2] @0x4008fcd8  FUNC1 alt 码（PJ10=byte2, PJ11=byte3）
 * 采样点：ctrl_apply_pinctrl 进入(pre)/返回(post) 各一次 + boot 一次。
 * write 失败时另存 dbg_err（10 个 pad 字 + I3C EVR/SER 错误码）。
 * 调试完必须删除（探针纪律 §8）。 */
static volatile uint32_t dbg_snap[26][10];
static volatile int dbg_snap_idx;
/* dbg_err/dbg_err_valid 必须是全局符号：main.c 的 ctrl_i3c_write 在写失败时
 * 经 extern 引用它们另存 pad 字 + I3C EVR/SER 错误码（static 会链接失败）。 */
volatile uint32_t dbg_err[12];
volatile int dbg_err_valid;

static void dbg_snapshot_regs(void)
{
	if (dbg_snap_idx < 26) {
		volatile uint32_t *d = dbg_snap[dbg_snap_idx];

		d[0] = *(volatile uint32_t *)0x4006f2a4;
		d[1] = *(volatile uint32_t *)0x4006f2a8;
		d[2] = *(volatile uint32_t *)0x4006f2ac;
		d[3] = *(volatile uint32_t *)0x4006f2b0;
		d[4] = *(volatile uint32_t *)0x4006f2b4;
		d[5] = *(volatile uint32_t *)0x4006f2b8;
		d[6] = *(volatile uint32_t *)0x4006f2bc;
		d[7] = *(volatile uint32_t *)0x400610b0;
		d[8] = *(volatile uint32_t *)0x400610d0;
		d[9] = *(volatile uint32_t *)0x4008fcd8;
		dbg_snap_idx++;
	}
}

/* test_main 用的公开入口 */
void dbg_snapshot_regs_pub(void) { dbg_snapshot_regs(); }

static int ctrl_apply_pinctrl(bool to_i2c)
{
	uint32_t state_id = to_i2c ? PINCTRL_STATE_PINMUX_I2C
				   : PINCTRL_STATE_DEFAULT;
	int ret;

	dbg_snapshot_regs();	/* 探针：apply 前 */
	ret = pinctrl_apply_state(ctrl_pcfg, state_id);
	dbg_snapshot_regs();	/* 探针：apply 后 */

	if (ret == 0) {
		ctrl_mode = to_i2c ? MODE_I2C : MODE_I3C;
	}
	return ret;
}

/* 发切换命令（当前模式帧）→ 延时（> target 侧 TGT_SWITCH_DELAY_MS）→ 本侧切换 */
static bool ctrl_switch_to(bool to_i2c)
{
	uint8_t cmd = to_i2c ? CMD_SWITCH_TO_I2C : CMD_SWITCH_TO_I3C;

	if (ctrl_send_cmd(&cmd, 1) != 0) {
		ctrl_set_fail("send_switch");
		return false;
	}

	k_sleep(K_MSEC(CTRL_SWITCH_DELAY_MS));

	if (ctrl_apply_pinctrl(to_i2c) != 0) {
		ctrl_set_fail("pinctrl_apply_state(%s)",
			      to_i2c ? "i2c" : "i3c");
		return false;
	}
	return true;
}
#endif

/* ============ 基本读写（按当前模式） ============ */

static void ctrl_basic_rw(void)
{
	uint8_t wbuf[2 + I2C_RW_MAX];
	uint8_t rbuf[I2C_RW_MAX];
	size_t n = 16;
	int ret;

	wbuf[0] = CMD_WRITE_PATTERN;
	wbuf[1] = (uint8_t)n;
	for (size_t i = 0; i < n; i++) {
		wbuf[2 + i] = (uint8_t)i;
	}

	if (ctrl_mode == MODE_I3C) {
		if (target_desc == NULL) {
			ctrl_set_fail("no_target");
			return;
		}
		ret = ctrl_i3c_write(target_desc, wbuf, 2 + n);
	} else {
		ret = i2c_write(ctrl_i2c_dev, wbuf, 2 + n, I2C_SWITCH_SLAVE_ADDR);
	}
	if (ret != 0) {
		ctrl_set_fail("write:%d", ret);
		return;
	}

	if (ctrl_mode == MODE_I3C) {
		if (target_desc == NULL) {
			ctrl_set_fail("no_target");
			return;
		}
		ret = ctrl_i3c_read(target_desc, rbuf, n);
	} else {
		ret = i2c_read(ctrl_i2c_dev, rbuf, n, I2C_SWITCH_SLAVE_ADDR);
	}
	if (ret != 0) {
		ctrl_set_fail("read:%d", ret);
		return;
	}

	for (size_t i = 0; i < n; i++) {
		uint8_t expect = (uint8_t)(TGT_READ_PATTERN_BASE + i);

		if (rbuf[i] != expect) {
			ctrl_set_fail("byte[%u]:0x%02X!=0x%02X", (unsigned)i,
				      rbuf[i], expect);
			return;
		}
	}
}

/* ============ 各测试项（纯 I2C case 无 I3C/切换项） ============ */

#ifndef CONFIG_I3C_I2C_PURE_I2C_TEST
static void ctrl_test_pinctrl_states(void)
{
	ctrl_test_begin(TEST_PINCTRL_STATES, false);

	bool ok = (ctrl_pcfg->state_cnt == 2) &&
		  (ctrl_pcfg->states[0].id == PINCTRL_STATE_DEFAULT) &&
		  (ctrl_pcfg->states[1].id == PINCTRL_STATE_PINMUX_I2C);

	if (!ok) {
		ctrl_set_fail("states:%u[%u,%u]", ctrl_pcfg->state_cnt,
			      ctrl_pcfg->states[0].id, ctrl_pcfg->states[1].id);
	}

	ctrl_test_end(TEST_PINCTRL_STATES, false);
}

static void ctrl_test_i3c_baseline(void)
{
	/* 先 DAA 再发 'B'：'B' 经 I3C 帧发送需要 target_desc 已就绪
	 * （ctrl_send_cmd 在 target_desc==NULL 时返回 -ENODEV → send_begin） */
	if (ctrl_mode != MODE_I3C) {
		ctrl_test_begin(TEST_I3C_BASELINE, true);
		ctrl_set_fail("mode:%d", ctrl_mode);
		ctrl_test_end(TEST_I3C_BASELINE, true);
		return;
	}

	target_desc = ctrl_wait_for_target();
	if (target_desc == NULL) {
		ctrl_test_begin(TEST_I3C_BASELINE, false);
		ctrl_set_fail("daa_timeout");
		ctrl_test_end(TEST_I3C_BASELINE, false);
		return;
	}

	ctrl_test_begin(TEST_I3C_BASELINE, true);
	ctrl_basic_rw();
	ctrl_test_end(TEST_I3C_BASELINE, true);
}

static void ctrl_test_switch_i3c_to_i2c(void)
{
	ctrl_test_begin(TEST_SWITCH_I3C_TO_I2C, true);

	if (ctrl_mode != MODE_I3C) {
		ctrl_set_fail("mode:%d", ctrl_mode);
	} else if (ctrl_switch_to(true)) {
		ctrl_basic_rw();	/* I2C 探测：回连验证 */
	}

	ctrl_test_end(TEST_SWITCH_I3C_TO_I2C, true);
}

static void ctrl_test_i2c_rw(enum i3c_i2c_test_id id, size_t n)
{
	uint8_t wbuf[2 + I2C_RW_MAX];
	uint8_t rbuf[I2C_RW_MAX];
	int ret;

	ctrl_test_begin(id, true);

	if (ctrl_mode != MODE_I2C) {
		ctrl_set_fail("mode:%d", ctrl_mode);
	} else {
		wbuf[0] = CMD_WRITE_PATTERN;
		wbuf[1] = (uint8_t)n;
		for (size_t i = 0; i < n; i++) {
			wbuf[2 + i] = (uint8_t)i;
		}

		ret = i2c_write(ctrl_i2c_dev, wbuf, 2 + n, I2C_SWITCH_SLAVE_ADDR);
		if (ret != 0) {
			ctrl_set_fail("i2c_write:%d", ret);
		} else {
			ret = i2c_read(ctrl_i2c_dev, rbuf, n, I2C_SWITCH_SLAVE_ADDR);
			if (ret != 0) {
				ctrl_set_fail("i2c_read:%d", ret);
			} else {
				for (size_t i = 0; i < n; i++) {
					uint8_t expect =
						(uint8_t)(TGT_READ_PATTERN_BASE + i);

					if (rbuf[i] != expect) {
						ctrl_set_fail(
							"byte[%u]:0x%02X!=0x%02X",
							(unsigned)i, rbuf[i],
							expect);
						break;
					}
				}
			}
		}
	}

	ctrl_test_end(id, true);
}

static void ctrl_test_i2c_nack_bad_addr(void)
{
	uint8_t dummy = 0x00;
	struct i2c_msg msg = {
		.buf = &dummy,
		.len = 1,
		.flags = I2C_MSG_WRITE | I2C_MSG_STOP,
	};
	int ret;

	ctrl_test_begin(TEST_I2C_NACK_BAD_ADDR, true);

	if (ctrl_mode != MODE_I2C) {
		ctrl_set_fail("mode:%d", ctrl_mode);
	} else {
		/* 0x70 无设备：i2c_transfer 应返回错误（NACK） */
		ret = i2c_transfer(ctrl_i2c_dev, &msg, 1, 0x70);
		if (ret == 0) {
			ctrl_set_fail("expected_nack");
		}
	}

	ctrl_test_end(TEST_I2C_NACK_BAD_ADDR, true);
}

static void ctrl_test_switch_i2c_to_i3c(void)
{
	ctrl_test_begin(TEST_SWITCH_I2C_TO_I3C, true);

	if (ctrl_mode != MODE_I2C) {
		ctrl_set_fail("mode:%d", ctrl_mode);
	} else if (ctrl_switch_to(false)) {
		target_desc = ctrl_wait_for_target();	/* 重新 DAA */
		if (target_desc == NULL) {
			ctrl_set_fail("daa_timeout");
		} else {
			ctrl_basic_rw();	/* I3C 复验 */
		}
	}

	ctrl_test_end(TEST_SWITCH_I2C_TO_I3C, true);
}

static void ctrl_test_switch_stress(void)
{
	for (int round = 0; round < SWITCH_STRESS_ROUNDS; round++) {
		/* 每轮：I3C → I2C → I3C，各一次 16B 写读；任一轮失败即 FAIL */
		if (ctrl_mode != MODE_I3C) {
			ctrl_set_fail("round%d_mode:%d", round, ctrl_mode);
			(void)ctrl_apply_pinctrl(false);	/* 恢复 I3C 继续 */
		} else {
			ctrl_test_begin(TEST_SWITCH_STRESS, true);
			ctrl_basic_rw();
			if (ctrl_switch_to(true)) {
				ctrl_test_begin(TEST_SWITCH_STRESS, true);
				ctrl_basic_rw();
				if (ctrl_switch_to(false)) {
					target_desc = ctrl_wait_for_target();
					ctrl_test_begin(TEST_SWITCH_STRESS, true);
					ctrl_basic_rw();
				}
			}
			ctrl_test_end(TEST_SWITCH_STRESS, true);
		}
	}
}
#endif /* !CONFIG_I3C_I2C_PURE_I2C_TEST */

#ifdef CONFIG_I3C_I2C_PURE_I2C_TEST
/* ============ 纯 I2C 波形测试（i2c_wave） ============ */

/* 波形观察轮数与轮间间隔：每轮一次 16B 写+读传输（400K 下 ~0.3ms 波形
 * 爆发），轮间间隔便于示波器/逻辑分析仪抓拍；总时长 ~10s。 */
#define PURE_I2C_ROUNDS 100
#define PURE_I2C_ROUND_DELAY_MS 100

static void ctrl_test_i2c_wave(void)
{
	uint8_t wbuf[2 + 16];
	uint8_t rbuf[16];

	ctrl_test_begin(TEST_I2C_WAVE, true);

	for (int round = 0; round < PURE_I2C_ROUNDS; round++) {
		wbuf[0] = CMD_WRITE_PATTERN;
		wbuf[1] = 16;
		for (size_t i = 0; i < 16; i++) {
			wbuf[2 + i] = (uint8_t)i;
		}

		if (i2c_write(ctrl_i2c_dev, wbuf, sizeof(wbuf),
			      I2C_TGT_SLAVE_ADDR) != 0) {
			ctrl_set_fail("write:round%d", round);
			break;
		}
		if (i2c_read(ctrl_i2c_dev, rbuf, sizeof(rbuf),
			     I2C_TGT_SLAVE_ADDR) != 0) {
			ctrl_set_fail("read:round%d", round);
			break;
		}
		for (size_t i = 0; i < 16; i++) {
			if (rbuf[i] != (uint8_t)(TGT_READ_PATTERN_BASE + i)) {
				ctrl_set_fail("byte[%u]:0x%02X!=0x%02X",
					      (unsigned)i, rbuf[i],
					      TGT_READ_PATTERN_BASE + i);
				break;
			}
		}
		if (ctrl_failed) {
			break;
		}

		k_sleep(K_MSEC(PURE_I2C_ROUND_DELAY_MS));
	}

	I3C_I2C_INFO_CORE(CTRL, i3c_i2c_test_names[TEST_I2C_WAVE],
			  "rounds=100,delay_ms=100");
	ctrl_test_end(TEST_I2C_WAVE, true);
}
#endif /* CONFIG_I3C_I2C_PURE_I2C_TEST */

/* ============ SUMMARY ============ */

static void ctrl_print_summary(void)
{
	uint8_t passed = 0;

	I3C_I2C_SUMMARY_BEGIN(CTRL);
	for (int i = 0; i < I3C_I2C_TEST_COUNT; i++) {
		switch (test_results[i]) {
		case TR_PASS:
			I3C_I2C_SUMMARY_ITEM(CTRL, i3c_i2c_test_names[i], "PASS");
			passed++;
			break;
		case TR_FAIL:
			I3C_I2C_SUMMARY_ITEM_REASON(CTRL, i3c_i2c_test_names[i],
						     "FAIL", test_reasons[i]);
			break;
		default:
			I3C_I2C_SUMMARY_ITEM(CTRL, i3c_i2c_test_names[i], "NOT_RUN");
			break;
		}
	}
	I3C_I2C_SUMMARY_END(CTRL, passed, I3C_I2C_TEST_COUNT);
}

/* ============ test_main ============ */

void test_main(void)
{
	zt_init("CTRL");

	LOG_INF("[CPU2 I3C_I2C_CTRL] controller test started");

	tr_init(I3C_I2C_TEST_COUNT);

#ifdef CONFIG_I3C_I2C_PURE_I2C_TEST
	/* 纯 I2C 波形 case：无 I3C 阶段，初始即 I2C 模式 */
	ctrl_mode = MODE_I2C;
	target_desc = NULL;

	ctrl_test_i2c_wave();
#else
	ctrl_mode = MODE_I3C;
	target_desc = NULL;

	/* 探针：boot 初始状态（i3c5 init 已把引脚配成 I3C） */
	dbg_snapshot_regs_pub();

	ctrl_test_pinctrl_states();
	ctrl_test_i3c_baseline();
	ctrl_test_switch_i3c_to_i2c();
	ctrl_test_i2c_rw(TEST_I2C_RW_1, 1);
	ctrl_test_i2c_rw(TEST_I2C_RW_16, 16);
	ctrl_test_i2c_rw(TEST_I2C_RW_17, 17);
	ctrl_test_i2c_rw(TEST_I2C_RW_100, 100);
	ctrl_test_i2c_nack_bad_addr();
	ctrl_test_switch_i2c_to_i3c();
	ctrl_test_switch_stress();
#endif

	/* 通知 target 打印总结（此时处于 I3C 模式，'P' 走 i3c 帧），
	 * 稍等让 target 的总结先出完再打印自己的 */
	(void)ctrl_send_cmd((const uint8_t[]){ CMD_PRINT_SUMMARY }, 1);
	k_sleep(K_MSEC(5));

	ctrl_print_summary();
	printk("PROJECT EXECUTION %s\n", ctrl_failed ? "FAILED" : "SUCCESSFUL");
}
