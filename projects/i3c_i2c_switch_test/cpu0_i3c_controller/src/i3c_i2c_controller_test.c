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
 *
 * ztest 原生框架版：功能顺序 ≠ ztest 链接序（SORT_BY_NAME 字典序），
 * 故整个 10 项流程由 dispatcher 线程按旧固件功能顺序原样执行（非致命
 * 失败累计 + 继续，与旧 test_main 语义逐点一致），每项结束后置
 * ctrl_item_done[id] 唤醒对应 ZTEST 测试项；'P'（收尾通知）由 test_main
 * 在 zt_run_all() 返回后下发（ctrl_send_summary，I1：ztest_run_all 无论
 * 断言成败都会返回，成败路径均下发——不能放 suite after 钩子：本树
 * after 是 per-test 钩子，链接序首项结束即触发 'P'，会与仍在跑的
 * switch_stress 并发访问总线，Task 8 审查 Critical 1 修正）。'E' 全部由
 * dispatcher 线程下发——zassert 失败的长跳转只发生在 ztest 测试线程，
 * 不会跳过任何 'E'。
 * 结果记账由 ztest_glue 按 ztest stats + 链接序自动完成（§4.5）。
 */

#include "i3c_i2c_controller_test.h"
#include "i3c_i2c_switch_test_protocol.h"
#include "test_results.h"

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
#include <zephyr/ztest.h>

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
 * （PINCTRL_DT_DEV_CONFIG_DECLARE，CONFIG_PINCTRL_NON_STATIC）。 */
PINCTRL_DT_DEV_CONFIG_DECLARE(DT_NODELABEL(i3c5));
static const struct pinctrl_dev_config *ctrl_pcfg =
	PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(i3c5));

/* 当前测试的失败累计：begin 时同 id 不清（STRESS 任一轮失败即败） */
static enum i3c_i2c_test_id ctrl_cur_id;
static enum i3c_i2c_test_id ctrl_last_id = I3C_I2C_TEST_COUNT;
static bool ctrl_failed;
static char ctrl_fail_reason[TR_REASON_LEN];

/* dispatcher → ztest 测试项的完成通知（按枚举 id 索引） */
static volatile bool ctrl_item_done[I3C_I2C_TEST_COUNT];
static volatile bool ctrl_item_failed[I3C_I2C_TEST_COUNT];
static char ctrl_item_reason[I3C_I2C_TEST_COUNT][TR_REASON_LEN];

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

/* ============ 测试 begin/end（'B'/'E' 命令，无 token 打印） ============ */

static void ctrl_test_begin(enum i3c_i2c_test_id id, bool via_bus)
{
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
}

/* 单项流程结束：快照本项结果 + 唤醒对应 ZTEST 测试（dispatcher 线程调用）。
 * STRESS 每轮 end 都会写 ctrl_failed（累计），但 done 只在整个流程
 * 结束后置位——测试项看到的是全轮累计结果（旧语义）。 */
static void ctrl_item_finish(enum i3c_i2c_test_id id)
{
	ctrl_item_failed[id] = ctrl_failed;
	strncpy(ctrl_item_reason[id], ctrl_fail_reason, TR_REASON_LEN - 1);
	ctrl_item_reason[id][TR_REASON_LEN - 1] = '\0';
	ctrl_item_done[id] = true;
}

/* ============ 切换辅助（pinmux apply + 模式跟踪） ============ */

static int ctrl_apply_pinctrl(bool to_i2c)
{
	uint32_t state_id = to_i2c ? PINCTRL_STATE_PINMUX_I2C
				   : PINCTRL_STATE_DEFAULT;
	int ret = pinctrl_apply_state(ctrl_pcfg, state_id);

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

/* ============ 各测试项 ============ */

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

/* ============ 功能流程（dispatcher 线程，旧 test_main 功能序原样执行） ============ */

static void ctrl_flow_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	ctrl_mode = MODE_I3C;
	target_desc = NULL;

	ctrl_test_pinctrl_states();
	ctrl_item_finish(TEST_PINCTRL_STATES);

	ctrl_test_i3c_baseline();
	ctrl_item_finish(TEST_I3C_BASELINE);

	ctrl_test_switch_i3c_to_i2c();
	ctrl_item_finish(TEST_SWITCH_I3C_TO_I2C);

	ctrl_test_i2c_rw(TEST_I2C_RW_1, 1);
	ctrl_item_finish(TEST_I2C_RW_1);

	ctrl_test_i2c_rw(TEST_I2C_RW_16, 16);
	ctrl_item_finish(TEST_I2C_RW_16);

	ctrl_test_i2c_rw(TEST_I2C_RW_17, 17);
	ctrl_item_finish(TEST_I2C_RW_17);

	ctrl_test_i2c_rw(TEST_I2C_RW_100, 100);
	ctrl_item_finish(TEST_I2C_RW_100);

	ctrl_test_i2c_nack_bad_addr();
	ctrl_item_finish(TEST_I2C_NACK_BAD_ADDR);

	ctrl_test_switch_i2c_to_i3c();
	ctrl_item_finish(TEST_SWITCH_I2C_TO_I3C);

	ctrl_test_switch_stress();
	ctrl_item_finish(TEST_SWITCH_STRESS);
}

void ctrl_flow_start(void)
{
	/* 流程含 i2c/i3c 驱动调用链 + 局部 100B 缓冲（旧固件在 10KB main
	 * 栈上跑），dispatcher 线程独立给 8KB 静态栈 */
	static K_THREAD_STACK_DEFINE(flow_stack, 8192);
	static struct k_thread flow_thread;

	k_thread_create(&flow_thread, flow_stack, K_THREAD_STACK_SIZEOF(flow_stack),
			ctrl_flow_entry, NULL, NULL, NULL,
			7, 0, K_NO_WAIT);
}

/* ============ 完成通知 'P'（test_main 在 zt_run_all() 后调用） ============ */

/* 收尾通知 'P'：ztest_run_all 返回 = 10 个 ZTEST 快照全部齐（每项快照 =
 * dispatcher 完成对应功能项，等快照的测试体全部结束），与旧固件"功能流程
 * 全部完成才发 'P'"时序等价（样板 i3c_dualcore_test 同款模式，Task 8 审查
 * Critical 1 指定修法 b）。不 zassert、失败忽略（target 侧判定结构自带兜底）。
 * ★ 不能放 suite after 钩子：本树 ztest 的 after 是 per-test 钩子（每项测试
 * 后调用），链接序首项结束即触发 'P'，此时 switch_stress 仍在跑——并发访问
 * 总线 + target 端 switch_stress 每轮伪失败（no_end_received）。 */
void ctrl_send_summary(void)
{
	const uint8_t cmd = CMD_PRINT_SUMMARY;

	(void)ctrl_send_cmd(&cmd, 1);
	/* 发 'P' 后留 20ms 让 target 先完成收尾（旧固件同款时序） */
	k_sleep(K_MSEC(20));
}

/* ============ ztest 套件（10 项，与 target 同名双 PASS 判定） ============ */

ZTEST_SUITE(switch_ctrl, NULL, NULL, NULL, NULL, NULL);

/* 单项测试体公共实现：等 dispatcher 完成本项流程后按快照判定。
 * 断言失败即测试失败（zassert 打印 Assertion failed 详情，harness 归因
 * 进报告 reason 列，§13.5）。 */
static void ctrl_test_item(enum i3c_i2c_test_id id)
{
	while (!ctrl_item_done[id]) {
		k_sleep(K_MSEC(20));
	}

	if (ctrl_item_failed[id]) {
		zassert_true(false, "%s", ctrl_item_reason[id]);
	}
}

ZTEST(switch_ctrl, pinctrl_states)
{
	ctrl_test_item(TEST_PINCTRL_STATES);
}

ZTEST(switch_ctrl, i3c_baseline)
{
	ctrl_test_item(TEST_I3C_BASELINE);
}

ZTEST(switch_ctrl, switch_i3c_to_i2c)
{
	ctrl_test_item(TEST_SWITCH_I3C_TO_I2C);
}

ZTEST(switch_ctrl, i2c_rw_1)
{
	ctrl_test_item(TEST_I2C_RW_1);
}

ZTEST(switch_ctrl, i2c_rw_16)
{
	ctrl_test_item(TEST_I2C_RW_16);
}

ZTEST(switch_ctrl, i2c_rw_17)
{
	ctrl_test_item(TEST_I2C_RW_17);
}

ZTEST(switch_ctrl, i2c_rw_100)
{
	ctrl_test_item(TEST_I2C_RW_100);
}

ZTEST(switch_ctrl, switch_i2c_to_i3c)
{
	ctrl_test_item(TEST_SWITCH_I2C_TO_I3C);
}

ZTEST(switch_ctrl, i2c_nack_bad_addr)
{
	ctrl_test_item(TEST_I2C_NACK_BAD_ADDR);
}

ZTEST(switch_ctrl, switch_stress)
{
	ctrl_test_item(TEST_SWITCH_STRESS);
}
