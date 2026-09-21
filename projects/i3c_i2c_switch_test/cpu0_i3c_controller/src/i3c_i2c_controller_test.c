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
#include "rlog.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/drivers/i3c/ccc.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <reg_i3c.h>
#include <zephyr/devicetree.h>

LOG_MODULE_REGISTER(i3c_i2c_master_test_impl, LOG_LEVEL_INF);

/* ============ 全局状态 ============ */

/* PA05 探针标记：switch_i2c_to_i3c 的 read 前拉高、read 后拉低，
 * 示波器用 PA05 边沿定位 read 帧（I3C PJ10/PJ11 通道同时捕获）。 */
static const struct device *probe_gpio = DEVICE_DT_GET(DT_NODELABEL(gpioa));
#define PROBE_PIN 5
#define PROBE_HIGH() gpio_pin_set(probe_gpio, PROBE_PIN, 1)
#define PROBE_LOW()  gpio_pin_set(probe_gpio, PROBE_PIN, 0)
#define PROBE_INIT() gpio_pin_configure(probe_gpio, PROBE_PIN, GPIO_OUTPUT_LOW)

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
	/* 单出口（§6.9.3）：先把已到的从核行打出去再打本核 token ——
	 * drain 同时给从核的 mbox FIFO 腾空间（从核发不进去会丢帧）。 */
	(void)rlog_relay_drain();
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

	/* 从核本项的 START/PASS 行就在这几毫秒内到（'E' 之后从核才结账）——
	 * 给它一点时间并顺手 drain，避免攒到 FIFO 满丢帧。 */
	k_sleep(K_MSEC(2));
	(void)rlog_relay_drain();

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

static void ctrl_i3c_purge(void);

static int ctrl_apply_pinctrl(bool to_i2c)
{
	uint32_t state_id = to_i2c ? PINCTRL_STATE_PINMUX_I2C
				   : PINCTRL_STATE_DEFAULT;
	int ret;

	ret = pinctrl_apply_state(ctrl_pcfg, state_id);

	if (ret == 0) {
		ctrl_mode = to_i2c ? MODE_I2C : MODE_I3C;
	}
	if (ret == 0 && !to_i2c) {
		ctrl_i3c_purge();
	}
	return ret;
}

/* 发切换命令（当前模式帧）→ 延时（> target 侧 TGT_SWITCH_DELAY_MS）→ 本侧切换 */

/* 控制器侧帧外清洁：复刻驱动 ls_i3c_xfer_reset 序列。pinmux 往返后 i3c5
 * 控制器 FIFO/状态残留会污染下一笔私有传输（实测切回后首笔 read 头部
 * 出现确定性垃圾字节，RX FIFO 却为空 → 帧级污染）；i3c_transfer 成功
 * 路径驱动不做帧外清理，CCC 路径才有（"先清 FIFO 再发起请求"），故
 * 切回 I3C 后、首笔私有传输前在本侧补一次。 */
static void ctrl_i3c_purge(void)
{
	I3C_TypeDef *base = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c5));
	int i;

	/* 等总线空闲（BUSY 清零）：活动帧期间 flush 会打断帧 */
	for (i = 0; (base->EVR & I3C_EVR_BUSY_MASK) != 0U && i < 10000; i++) {
	}

	/* 清帧完成/错误/接收终止残留标志（写 CEVR 清位，EVR 跨时钟域
	 * 同步滞后，清一次足够：此处帧早已结束） */
	base->CEVR = I3C_CEVR_CFCF_MASK | I3C_CEVR_CERRF_MASK |
		     I3C_IER_RXTGTENDIE_MASK;

	/* 排空 RX FIFO 残留字节（RXCOUNT + RXFNEF 双条件兜底） */
	for (i = 0; i < 64; i++) {
		uint32_t rxc = (base->SDATACTRL & I3C_SDATACTRL_RXCOUNT_MASK) >>
			       I3C_SDATACTRL_RXCOUNT_POS;

		if (rxc == 0U && (base->EVR & I3C_EVR_RXFNEF_MASK) == 0U) {
			break;
		}
		(void)base->RDR;
	}
	/* RX flush 请求（CFGR 写 1 触发，异步清空需 settle） */
	base->CFGR |= I3C_CFGR_RXFLUSH_MASK;
	k_busy_wait(5);

	/* TX FIFO flush 至空（TXFEF） */
	for (i = 0; i < 100; i++) {
		if ((base->EVR & I3C_EVR_TXFEF_MASK) != 0U) {
			break;
		}
		base->CFGR |= I3C_CFGR_TXFLUSH_MASK;
	}
	k_busy_wait(5);
	/* 控制 FIFO flush 至空（CFEF） */
	for (i = 0; i < 100; i++) {
		if ((base->EVR & I3C_EVR_CFEF_MASK) != 0U) {
			break;
		}
		base->CFGR |= I3C_CFGR_CFLUSH_MASK;
	}
	k_busy_wait(5);
	/* 状态 FIFO flush（SFLUSH） */
	base->CFGR |= I3C_CFGR_SFLUSH_MASK;
	k_busy_wait(5);
}

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
		/* 守卫：state_cnt < 2 时 states[1] 越界（原实现无条件打印
		 * states[0]/states[1]，属内存安全缺陷）。`ok` 的短路求值本身
		 * 已安全，越界只出在失败分支的打印。 */
		if (ctrl_pcfg->state_cnt >= 2) {
			ctrl_set_fail("states:%u[%u,%u]", ctrl_pcfg->state_cnt,
				      ctrl_pcfg->states[0].id,
				      ctrl_pcfg->states[1].id);
		} else {
			ctrl_set_fail("state_cnt:%u (<2)", ctrl_pcfg->state_cnt);
		}
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

/* ============ 广播 NACK 恢复测试 ============
 *
 * 验证广播 CCC NACK 后，后续 CCC 传输恢复正常（驱动修复回归验证）：
 *   1. target 切 I2C（不响应 I3C 广播），第一帧广播 RSTACT whole → NACK；
 *   2. target 自动切回 I3C；
 *   3. 后两帧广播 RSTACT peripheral / RSTDAA → 正常。 */
static void ctrl_test_broadcast_nack_recovery(void)
{
	I3C_TypeDef *base = (I3C_TypeDef *)0x40094000;	/* i3c5 基地址 */
	int r1, r2, r3;
	uint8_t cmd;

	ctrl_test_begin(TEST_BROADCAST_NACK_RECOVERY, true);

	/* 1. 发 'N' 让 target 临时切 I2C，制造第一帧广播 CE2 NACK */
	cmd = CMD_NACK_WINDOW;
	if (ctrl_send_cmd(&cmd, 1) != 0) {
		ctrl_set_fail("send_N");
	}
	/* target 收 'N' 后经 dispatcher(~1ms) → TGT_SWITCH_DELAY_MS(5ms) → pinctrl 切 I2C；
	 * NACK_WINDOW_MS(2ms) 后自动切回 I3C。
	 * 目标窗口：~6ms~8ms（dispatcher 开销可变）。
	 * controller 等 6ms：RSTACT 到达时 target 大概率在 I2C 窗口内。
	 * 若仍 FAIL（dispatcher 偏慢），可适当增大到 7ms。 */
	k_sleep(K_MSEC(6));	/* 等 target 进入 I2C NACK 窗口 */

	/* 2. 第一帧广播 RSTACT whole → 预期 NACK */
	r1 = i3c_ccc_do_rstact_all(ctrl_i3c_dev, I3C_CCC_RSTACT_RESET_WHOLE_TARGET);
	if (r1 == 0) {
		ctrl_set_fail("rstact_whole_should_nack");
	}

	k_sleep(K_MSEC(3));	/* 等 target 自动切回 I3C */

	/* 3. 第二帧广播 RSTACT peripheral → 预期正常 */
	r2 = i3c_ccc_do_rstact_all(ctrl_i3c_dev, I3C_CCC_RSTACT_PERIPHERAL_ONLY);
	if (r2 != 0) {
		ctrl_set_fail("rstact_peripheral:%d", r2);
	}

	/* 4. 第三帧广播 RSTDAA → 预期正常 */
	r3 = i3c_ccc_do_rstdaa_all(ctrl_i3c_dev);
	if (r3 != 0) {
		ctrl_set_fail("rstdaa:%d", r3);
	}

	/* 所有帧结束后总线应空闲，BUSY(bit12) 应为 0 */
	if (base->EVR & I3C_EVR_BUSY_MASK) {
		ctrl_set_fail("busy_bit_not_cleared");
	}

	ctrl_test_end(TEST_BROADCAST_NACK_RECOVERY, true);
}

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

	/* 单出口（§6.9.3）：本核是**主核** ⇒ 收下从核经 mbox 送来的 token 行，
	 * 原样打进本核串口。接收回调在 ISR 只入环形缓冲，主线程按点 drain。 */
	rlog_primary_init();

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

	/* PA05 探针初始化：switch_i2c_to_i3c 的 read 前拉高、read 后拉低，
	 * 示波器用 PA05 边沿定位 read 帧。 */
	PROBE_INIT();

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
	ctrl_test_broadcast_nack_recovery();
#endif

	/* broadcast_nack_recovery 里 RSTDAA 清了 target 硬件动态地址（0x32），
	 * 但 controller 侧 dev list 的 desc->dynamic_addr 不会同步清 → find 会
	 * 误判"已就绪"。显式 i3c_do_daa 强制重新 ENTDAA 恢复 target 地址，
	 * 否则下面 'P' 命令发不到 target → target 不打 EXECUTION → TGT=None
	 * 假 FAIL。 */
	(void)i3c_do_daa(ctrl_i3c_dev);
	target_desc = i3c_dev_list_i3c_addr_find(ctrl_i3c_dev, I3C_SWITCH_TARGET_ADDR);

	/* 通知 target 打印总结（此时处于 I3C 模式，'P' 走 i3c 帧）。
	 * 单出口（§6.9.3）：从核的 token 只出现在本核串口流里 ⇒ 必须等它把
	 * SUMMARY + DONE 送齐（期间边等边转发打印），再打本核收尾。
	 * fail-closed：没等到 DONE，或接收侧有丢帧 ⇒ 判失败 —— 绝不把
	 * "没收到从核结果"当成"通过"。 */
	(void)ctrl_send_cmd((const uint8_t[]){ CMD_PRINT_SUMMARY }, 1);

	if (!rlog_wait_secondary_done(RLOG_TX_TIMEOUT_MS)) {
		ctrl_set_fail(rlog_secondary_silent() ? "SECONDARY_SILENT"
						      : "SECONDARY_INCOMPLETE");
	} else if (rlog_lost() != 0U) {
		ctrl_set_fail("SECONDARY_INCOMPLETE(lost=%u)", (unsigned)rlog_lost());
	}

	ctrl_print_summary();
	printk("PROJECT EXECUTION %s\n", ctrl_failed ? "FAILED" : "SUCCESSFUL");

	/* test_main 不得返回（CLAUDE.md §4.1）——返回会进 ztest 强 main() 的
	 * `TC_END_REPORT`，在本核串口再补 `====` + `PROJECT EXECUTION SUCCESSFUL`，
	 * 使"完成标记是整条流最后一个"（§6.9.3 硬约束）不再成立、并触发 R6 的
	 * 相反标记并存。 */
	k_sleep(K_FOREVER);
}
