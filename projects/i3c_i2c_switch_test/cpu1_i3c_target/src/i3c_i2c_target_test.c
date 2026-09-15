/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * I3C/I2C 模块切换测试 —— cpu1 target 侧。
 *
 * 同一硬件模块（I3C6）以两个驱动视图存在：i3c6（I3C target）+ i2c6（I2C slave），
 * 引脚复用由 i3c6 节点的 pinctrl 双状态决定，本固件按 controller 命令经
 * pinctrl_apply_state() 在 "default"(I3C) 与 "pinmux_i2c"(I2C) 间切换。
 *
 * I3C 与 I2C 两种模式的 target 回调共用同一套逐字节解析状态机：
 *   - 命令通道：'B' <id> 开始记账 / 'E' <id> 结账 / 'W' <len> <data> 写 pattern 校验
 *   - 切换通道：'S' 切 I2C / 'Y' 切回 I3C（经延时后应用，避总线竞态）
 *   - 收尾通道：'P' 打印 SUMMARY
 *
 * ISR 回调（write_received 等）只解析记账 + 塞事件队列，所有 token 打印
 * 都在 dispatcher 线程上下文（CLAUDE.md §4.2 ISR 禁 LOG）。
 */

#include "i3c_i2c_target_test.h"
#include "i3c_i2c_switch_test_protocol.h"
#include "test_results.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <reg_i3c.h>
#include <zephyr/devicetree.h>

LOG_MODULE_REGISTER(i3c_i2c_target_test_impl, LOG_LEVEL_INF);

/* tgt_apply_switch 切回 I3C 时重注册用（target_test_init 中赋值） */
struct i3c_target_config *tgt_i3c_cfg_ptr;

/* ============ 常量 ============ */

/* I3C 读长度：controller 固定读 16 字节（≤ I3C TX FIFO 深度），
 * I3C 侧读回调据此判 END0（最后字节）；I2C 侧长度由 controller 的 STOP 决定。 */
#define TGT_TX_LEN 16U

/* 事件队列：ISR 只塞队列（K_NO_WAIT），dispatcher 线程消费并打印 */
enum tgt_evt_type {
	TGT_EVT_BEGIN,		/* 'B' <id>：开始记账 */
	TGT_EVT_END,		/* 'E' <id>：结账 */
	TGT_EVT_SWITCH,		/* 'S'/'Y'：切换 pinmux（id=1 切 I2C，0 切回 I3C） */
	TGT_EVT_NACK_WINDOW,	/* 'N'：临时置 SLVNACK 制造广播 NACK 窗口（复现用） */
	TGT_EVT_SUMMARY,	/* 'P'：打印 SUMMARY */
};

struct tgt_event {
	enum tgt_evt_type type;
	uint8_t id;
};

K_MSGQ_DEFINE(tgt_event_msgq, sizeof(struct tgt_event), 8, 4);

/* ============ 共享状态（ISR 记账，dispatcher 消费） ============ */

/* 纯 I2C case（CONFIG_I3C_I2C_PURE_I2C_TEST）禁用 i3c6，无 pinctrl 结构体 */
#ifndef CONFIG_I3C_I2C_PURE_I2C_TEST
PINCTRL_DT_DEV_CONFIG_DECLARE(DT_NODELABEL(i3c6));
static const struct pinctrl_dev_config *tgt_pcfg =
	PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(i3c6));
#endif

/* 读序列索引：每次写匹配复位 0，read 回调逐字节推进 */
static volatile uint16_t tgt_tx_idx;

/* 逐字节解析状态机 */
enum parse_state {
	PS_OPCODE,		/* 等 opcode */
	PS_CMD_BEGIN,		/* 已收 'B'，等 test id */
	PS_CMD_END,		/* 已收 'E'，等 test id */
	PS_WRITE_LEN,		/* 已收 'W'，等长度 */
	PS_WRITE_DATA,		/* 已收 'W' <len>，收数据并校验 */
};

static volatile enum parse_state tgt_ps;
static volatile uint8_t tgt_wr_len;
static volatile uint16_t tgt_wr_idx;

/* 写 pattern 内容校验：期望字节为递增序列 0,1,2,... */
static volatile bool tgt_data_mismatch;

/* 当前测试 id 与失败累计：同 id 多轮 begin 不清失败（STRESS 任一轮失败即败） */
static volatile uint8_t tgt_cur_id;
static volatile uint8_t tgt_last_id;
static volatile bool tgt_failed;
static char tgt_fail_reason[TR_REASON_LEN];

/* ============ 逐字节解析（ISR 上下文：只记账 + 塞队列） ============ */

static volatile bool tgt_evt_overflow;	/* 事件队列满（不应发生，协议单帧 ≤ 4 事件） */
static volatile bool tgt_done;		/* SUMMARY 后置 true，dispatcher 退出 */

static void tgt_queue_event(enum tgt_evt_type type, uint8_t id)
{
	struct tgt_event ev = { .type = type, .id = id };

	if (k_msgq_put(&tgt_event_msgq, &ev, K_NO_WAIT) != 0) {
		tgt_evt_overflow = true;
	}
}

static void tgt_parse_byte(uint8_t byte)
{
	switch (tgt_ps) {
	case PS_OPCODE:
		switch (byte) {
		case CMD_TGT_BEGIN:
			tgt_ps = PS_CMD_BEGIN;
			break;
		case CMD_TGT_END:
			tgt_ps = PS_CMD_END;
			break;
		case CMD_SWITCH_TO_I2C:
		case CMD_SWITCH_TO_I3C:
			tgt_queue_event(TGT_EVT_SWITCH,
					 (byte == CMD_SWITCH_TO_I2C) ? 1U : 0U);
			break;
		case CMD_NACK_WINDOW:
			tgt_queue_event(TGT_EVT_NACK_WINDOW, 0U);
			break;
		case CMD_WRITE_PATTERN:
			tgt_ps = PS_WRITE_LEN;
			break;
		case CMD_PRINT_SUMMARY:
			tgt_queue_event(TGT_EVT_SUMMARY, 0U);
			break;
		default:
			break;	/* 未知 opcode 忽略，保持等 opcode */
		}
		break;
	case PS_CMD_BEGIN:
		tgt_queue_event(TGT_EVT_BEGIN, byte);
		tgt_ps = PS_OPCODE;
		break;
	case PS_CMD_END:
		tgt_queue_event(TGT_EVT_END, byte);
		tgt_ps = PS_OPCODE;
		break;
	case PS_WRITE_LEN:
		tgt_wr_len = byte;
		tgt_wr_idx = 0;
		tgt_ps = (byte == 0) ? PS_OPCODE : PS_WRITE_DATA;
		break;
	case PS_WRITE_DATA:
		if (byte != (uint8_t)tgt_wr_idx) {
			tgt_data_mismatch = true;
		}
		if (++tgt_wr_idx >= tgt_wr_len) {
			tgt_ps = PS_OPCODE;
		}
		break;
	}
}

static void tgt_reset_rx(void)
{
	tgt_ps = PS_OPCODE;
	tgt_wr_len = 0;
	tgt_wr_idx = 0;
}

/* ============ I3C6 target 回调（纯 I2C case 无 I3C 侧） ============ */

#ifndef CONFIG_I3C_I2C_PURE_I2C_TEST
/* I3C 读字节：tx_idx < TGT_TX_LEN 时供数据，最后字节返回 1（驱动标 END0）；
 * 已预填 FIFO（16B 整满）时返回 0 字节并声明数据结束（样板同语义）。 */
static int tgt_i3c_read_byte(uint8_t *val)
{
	if (tgt_tx_idx < TGT_TX_LEN) {
		*val = tgt_tx_buf[tgt_tx_idx++];
		if (tgt_tx_idx >= TGT_TX_LEN) {
			return 1;
		}
		return 0;
	}

	*val = 0;
	return 1;
}

/* FIFO 预填：装 16 字节（= FIFO 深度，末字节带 END0 由硬件发完）。
 * 预填时机（三个）：
 *   1) write_requested_cb（写匹配）：restart 读请求时 FIFO 已满；
 *   2) stop_cb（每笔传输结束）：驱动 ISR 的 STOP 分支 FLUSHTB 清空 TX
 *      FIFO，必须重填，否则下一笔独立读传输（START+addr+R）开始时
 *      FIFO 空，read_requested_cb 会被调且只能提供 END0 → 只发 1 字节，
 *      controller 读欠载报错（实测 i3c_baseline read:-5）；
 *   3) init：总线空闲时恢复 FIFO 常备数据。
 * 驱动按 FIFO 现存 TXCOUNT 续写，重复预填幂等（样板同语义）。 */
static void tgt_i3c_prefill_tx(void)
{
	int pre = i3c_target_tx_write(tgt_i3c_dev, tgt_tx_buf, TGT_TX_LEN, 0);

	if (pre > 0) {
		tgt_tx_idx = (uint16_t)pre;
	}
}

static int tgt_i3c_write_requested_cb(struct i3c_target_config *config)
{
	ARG_UNUSED(config);

	tgt_reset_rx();
	tgt_tx_idx = 0;
	tgt_i3c_prefill_tx();
	return 0;
}

static int tgt_i3c_write_received_cb(struct i3c_target_config *config, uint8_t val)
{
	ARG_UNUSED(config);

	tgt_parse_byte(val);
	return 0;
}

static int tgt_i3c_read_requested_cb(struct i3c_target_config *config, uint8_t *val)
{
	ARG_UNUSED(config);

	return tgt_i3c_read_byte(val);
}

static int tgt_i3c_read_processed_cb(struct i3c_target_config *config, uint8_t *val)
{
	ARG_UNUSED(config);

	return tgt_i3c_read_byte(val);
}

static int tgt_i3c_stop_cb(struct i3c_target_config *config)
{
	ARG_UNUSED(config);

	/* 驱动 ISR 的 STOP 分支已 FLUSHTB 清空 TX FIFO，必须重填，
	 * 使下一笔独立读传输（START+addr+R）开始时 FIFO 已满（见
	 * tgt_i3c_prefill_tx 注释）；FIFO 有数据时驱动读分支不调
	 * read_requested_cb，硬件直接发预填数据。 */
	tgt_reset_rx();
	tgt_i3c_prefill_tx();
	return 0;
}
#endif /* !CONFIG_I3C_I2C_PURE_I2C_TEST */

/* ============ I2C6 slave 回调（逐字节，与 I3C 回调共用解析状态机） ============ */

static int tgt_i2c_read_byte(uint8_t *val)
{
	/* i2c 无 END0 概念：读长度由 controller 的 STOP 决定，tx_buf[256] 足够 */
	*val = tgt_tx_buf[tgt_tx_idx++];
	return 0;
}

static int tgt_i2c_write_requested_cb(struct i2c_target_config *config)
{
	ARG_UNUSED(config);

	tgt_reset_rx();
	tgt_tx_idx = 0;
	return 0;
}

static int tgt_i2c_write_received_cb(struct i2c_target_config *config, uint8_t val)
{
	ARG_UNUSED(config);

	tgt_parse_byte(val);
	return 0;	/* 不 NACK：数据内容由解析状态机校验 */
}

static int tgt_i2c_read_requested_cb(struct i2c_target_config *config, uint8_t *val)
{
	ARG_UNUSED(config);

	return tgt_i2c_read_byte(val);
}

static int tgt_i2c_read_processed_cb(struct i2c_target_config *config, uint8_t *val)
{
	ARG_UNUSED(config);

	return tgt_i2c_read_byte(val);
}

static int tgt_i2c_stop_cb(struct i2c_target_config *config)
{
	ARG_UNUSED(config);

	tgt_reset_rx();
	return 0;
}

/* ============ dispatcher 线程：事件处理（token 打印唯一位置） ============ */

static void tgt_handle_begin(uint8_t id)
{
	if (id >= I3C_I2C_TEST_COUNT) {
		return;
	}

	I3C_I2C_TGT_START_VAR(i3c_i2c_test_names[id]);

	tgt_cur_id = id;
	tgt_tx_idx = 0;
	tgt_data_mismatch = false;
	if (id != tgt_last_id) {
		/* 新测试清失败累计；同 id 多轮 begin（STRESS）不清 */
		tgt_failed = false;
		tgt_fail_reason[0] = '\0';
		tgt_last_id = id;
	}
}

static void tgt_handle_end(uint8_t id)
{
	if (id >= I3C_I2C_TEST_COUNT) {
		return;
	}

	if (id != tgt_cur_id) {
		I3C_I2C_TGT_FAIL_VAR(i3c_i2c_test_names[id], "end_id_mismatch");
		tr_record(id, TR_FAIL, "end_id_mismatch");
		return;
	}

	if (tgt_data_mismatch) {
		tgt_failed = true;
		snprintk(tgt_fail_reason, sizeof(tgt_fail_reason),
			 "write_data_mismatch");
	}

	if (tgt_failed) {
		I3C_I2C_TGT_FAIL_VAR(i3c_i2c_test_names[id], tgt_fail_reason);
		tr_record(id, TR_FAIL, "%s", tgt_fail_reason);
	} else {
		I3C_I2C_TGT_PASS_VAR(i3c_i2c_test_names[id]);
		tr_record(id, TR_PASS, "");
	}
}

#ifndef CONFIG_I3C_I2C_PURE_I2C_TEST

static void tgt_apply_switch(bool to_i2c)
{
	uint32_t state_id = to_i2c ? PINCTRL_STATE_PINMUX_I2C
				   : PINCTRL_STATE_DEFAULT;
	int ret = pinctrl_apply_state(tgt_pcfg, state_id);

	if (ret != 0) {
		tgt_failed = true;
		snprintk(tgt_fail_reason, sizeof(tgt_fail_reason),
			 "pinctrl_apply_state(%s):%d", to_i2c ? "i2c" : "i3c", ret);
	} else if (!to_i2c) {
		/* 切回 I3C：SLVENA 复位清硬件状态 + FLUSHTB 清 TX FIFO 残留，
		 * 再重注册回调 + 重预填 16B pattern（恢复正式工作状态）。 */
		I3C_TypeDef *base = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));

		base->SCONFIG &= ~I3C_SCONFIG_SLVENA_MASK;
		k_busy_wait(10);
		base->SCONFIG |= I3C_SCONFIG_SLVENA_MASK;
		k_busy_wait(10);

		i3c_target_unregister(tgt_i3c_dev, tgt_i3c_cfg_ptr);
		base->SDATACTRL |= I3C_SDATACTRL_FLUSHTB_MASK;
		i3c_target_register(tgt_i3c_dev, tgt_i3c_cfg_ptr);
		for (int i = 0; i < (int)sizeof(tgt_tx_buf); i++) {
			tgt_tx_buf[i] = (uint8_t)(TGT_READ_PATTERN_BASE + i);
		}
		tgt_tx_idx = 0;
		tgt_i3c_prefill_tx();
	}
}

/* NACK 窗口：切 I2C pinmux 让 target 不响应 I3C 广播（CE2），延时
 * NACK_WINDOW_MS 后自动切回 I3C（不产生 I2C 帧，无需 'Y' 命令）。 */
static void tgt_enter_nack_window(void)
{
	tgt_apply_switch(true);		/* 切 I2C，不响应 I3C 广播 */
	k_sleep(K_MSEC(NACK_WINDOW_MS));
	tgt_apply_switch(false);	/* 自动切回 I3C */
}
#endif

static void tgt_print_summary(void)
{
	uint8_t passed = 0;

	I3C_I2C_SUMMARY_BEGIN(TGT);
	for (int i = 0; i < I3C_I2C_TEST_COUNT; i++) {
		switch (test_results[i]) {
		case TR_PASS:
			I3C_I2C_SUMMARY_ITEM(TGT, i3c_i2c_test_names[i], "PASS");
			passed++;
			break;
		case TR_FAIL:
			I3C_I2C_SUMMARY_ITEM_REASON(TGT, i3c_i2c_test_names[i],
						     "FAIL", test_reasons[i]);
			break;
		default:
			I3C_I2C_SUMMARY_ITEM(TGT, i3c_i2c_test_names[i], "NOT_RUN");
			break;
		}
	}
	I3C_I2C_SUMMARY_END(TGT, passed, I3C_I2C_TEST_COUNT);
	I3C_I2C_TGT_DONE_VAR(passed == I3C_I2C_TEST_COUNT ? "PASS" : "FAIL");
}

static void dispatcher_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct tgt_event ev;
		int ret = k_msgq_get(&tgt_event_msgq, &ev, K_FOREVER);

		if (ret != 0) {
			continue;
		}

		switch (ev.type) {
		case TGT_EVT_BEGIN:
			tgt_handle_begin(ev.id);
			break;
		case TGT_EVT_END:
			tgt_handle_end(ev.id);
			break;
		case TGT_EVT_SWITCH:
#ifndef CONFIG_I3C_I2C_PURE_I2C_TEST
			/* 延时再切：保证 controller 已停止总线操作（协议时序） */
			k_sleep(K_MSEC(TGT_SWITCH_DELAY_MS));
			tgt_apply_switch(ev.id != 0);
#else
			/* 纯 I2C case 无切换命令（'S'/'Y' 不应到达） */
			break;
#endif
			break;
		case TGT_EVT_NACK_WINDOW:
#ifndef CONFIG_I3C_I2C_PURE_I2C_TEST
			tgt_enter_nack_window();
#else
			break;
#endif
			break;
		case TGT_EVT_SUMMARY:
			tgt_print_summary();
			tgt_done = true;
			break;
		}
		if (tgt_done) {
			break;	/* SUMMARY 已打印，dispatcher 退出，test_main 继续 */
		}
	}
}

/* ============ 公共 init / dispatcher 入口 ============ */

#ifndef CONFIG_I3C_I2C_PURE_I2C_TEST
/* pinctrl_states：双核各自自主验证（不经总线命令），验证 IO 区分机制静态配置 */
static void tgt_self_check_pinctrl_states(void)
{
	I3C_I2C_TGT_START("pinctrl_states");

	bool ok = (tgt_pcfg->state_cnt == 2) &&
		  (tgt_pcfg->states[0].id == PINCTRL_STATE_DEFAULT) &&
		  (tgt_pcfg->states[1].id == PINCTRL_STATE_PINMUX_I2C);

	if (ok) {
		I3C_I2C_TGT_PASS("pinctrl_states");
		tr_record(TEST_PINCTRL_STATES, TR_PASS, "");
	} else {
		snprintk(tgt_fail_reason, sizeof(tgt_fail_reason),
			 "states:%u[%u,%u]", tgt_pcfg->state_cnt,
			 tgt_pcfg->states[0].id, tgt_pcfg->states[1].id);
		I3C_I2C_TGT_FAIL_VAR("pinctrl_states", tgt_fail_reason);
		tr_record(TEST_PINCTRL_STATES, TR_FAIL, "%s", tgt_fail_reason);
	}
}
#endif

void target_test_init(void)
{
	/* 标准 Zephyr i2c_target_callbacks：不设 buf_read_requested（NULL），
	 * 驱动按函数指针非 NULL 判断走逐字节回调路径（i2c_ls.c）。 */
	static struct i2c_target_callbacks i2c_cb = {
		.write_requested = tgt_i2c_write_requested_cb,
		.write_received = tgt_i2c_write_received_cb,
		.read_requested = tgt_i2c_read_requested_cb,
		.read_processed = tgt_i2c_read_processed_cb,
		.stop = tgt_i2c_stop_cb,
	};
	static struct i2c_target_config i2c_cfg = {
		.address = I2C_TGT_SLAVE_ADDR,
		.callbacks = &i2c_cb,
	};

	tgt_reset_rx();
	tgt_failed = false;
	tgt_fail_reason[0] = '\0';

	tr_init(I3C_I2C_TEST_COUNT);

#ifndef CONFIG_I3C_I2C_PURE_I2C_TEST
	static struct i3c_target_callbacks i3c_cb = {
		.write_requested_cb = tgt_i3c_write_requested_cb,
		.write_received_cb = tgt_i3c_write_received_cb,
		.read_requested_cb = tgt_i3c_read_requested_cb,
		.read_processed_cb = tgt_i3c_read_processed_cb,
		.stop_cb = tgt_i3c_stop_cb,
	};
	static struct i3c_target_config i3c_cfg = {
		.address = I3C_SWITCH_TARGET_ADDR,
		.callbacks = &i3c_cb,
	};
	/* 供 tgt_apply_switch 重注册用 */
	tgt_i3c_cfg_ptr = &i3c_cfg;

	int ret = i3c_target_register(tgt_i3c_dev, &i3c_cfg);
	if (ret != 0) {
		LOG_ERR("i3c_target_register failed: %d", ret);
	}

	/* init 预填：总线空闲时恢复 FIFO 常备数据（与 stop_cb 预填同语义） */
	tgt_i3c_prefill_tx();

	ret = i2c_target_register(tgt_i2c_dev, &i2c_cfg);
	if (ret != 0) {
		LOG_ERR("i2c_target_register failed: %d", ret);
	}
#else
	int ret = i2c_target_register(tgt_i2c_dev, &i2c_cfg);
	if (ret != 0) {
		LOG_ERR("i2c_target_register failed: %d", ret);
	}
#endif
}

void target_dispatcher_run(void)
{
	static K_THREAD_STACK_DEFINE(dispatcher_stack, 4096);
	static struct k_thread dispatcher_thread;

#ifndef CONFIG_I3C_I2C_PURE_I2C_TEST
	tgt_self_check_pinctrl_states();
#endif

	k_thread_create(&dispatcher_thread, dispatcher_stack,
			K_THREAD_STACK_SIZEOF(dispatcher_stack),
			dispatcher_entry, NULL, NULL, NULL,
			7, 0, K_NO_WAIT);
	/* 同步 join：test_main 不得返回（CLAUDE.md §4.1） */
	k_thread_join(&dispatcher_thread, K_FOREVER);
}
