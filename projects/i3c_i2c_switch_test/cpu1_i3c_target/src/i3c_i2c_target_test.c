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
 *   - 收尾通道：'P' 通知判定收尾
 *
 * ISR 回调（write_received 等）只解析记账 + 塞事件队列（CLAUDE.md §4.2
 * ISR 禁 LOG），事件处理都在 dispatcher 线程上下文。
 *
 * ztest 原生框架版：
 *   - dispatcher 线程处理 BEGIN/END/SWITCH/SUMMARY 事件，END 时把该 id 的
 *     结账结果写入 tgt_item_* 快照（按总线枚举 id 索引）并置完成标记；
 *   - 10 个 ZTEST 测试项等各自的完成标记（STRESS 等满 SWITCH_STRESS_ROUNDS
 *     轮结账；'P' 可提前唤醒并按其时快照判定），断言快照结果；
 *   - 每项结账同时按 **ztest 链接下标** 写 test_results（GDB 兜底数据源）：
 *     下标由 ztest_unit_test 段按测试名运行时解析（tgt_link_index），
 *     与 ztest_glue 的链接序记账口径一致（§10.27）；
 *   - pinctrl_states 无总线命令：自检在 dispatcher 启动前完成（旧固件同序），
 *     结果同样写入快照 + 链接下标记账。
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
#include <zephyr/ztest.h>
#include <zephyr/ztest_test.h>

LOG_MODULE_REGISTER(i3c_i2c_target_test_impl, LOG_LEVEL_INF);

/* ============ 常量 ============ */

/* I3C 读长度：controller 固定读 16 字节（≤ I3C TX FIFO 深度），
 * I3C 侧读回调据此判 END0（最后字节）；I2C 侧长度由 controller 的 STOP 决定。 */
#define TGT_TX_LEN 16U

/* 事件队列：ISR 只塞队列（K_NO_WAIT），dispatcher 线程消费 */
enum tgt_evt_type {
	TGT_EVT_BEGIN,		/* 'B' <id>：开始记账 */
	TGT_EVT_END,		/* 'E' <id>：结账 */
	TGT_EVT_SWITCH,		/* 'S'/'Y'：切换 pinmux（id=1 切 I2C，0 切回 I3C） */
	TGT_EVT_SUMMARY,	/* 'P'：判定收尾 */
};

struct tgt_event {
	enum tgt_evt_type type;
	uint8_t id;
};

K_MSGQ_DEFINE(tgt_event_msgq, sizeof(struct tgt_event), 8, 4);

/* ============ 共享状态（ISR 记账，dispatcher 消费） ============ */

PINCTRL_DT_DEV_CONFIG_DECLARE(DT_NODELABEL(i3c6));
static const struct pinctrl_dev_config *tgt_pcfg =
	PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(i3c6));

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

/* dispatcher → ztest 测试项的结账快照（按总线枚举 id 索引）：
 * done = 该 id 已可判定（普通项首笔 'E'，STRESS 满 SWITCH_STRESS_ROUNDS 笔） */
static volatile bool tgt_item_done[I3C_I2C_TEST_COUNT];
static volatile bool tgt_item_failed[I3C_I2C_TEST_COUNT];
static char tgt_item_reason[I3C_I2C_TEST_COUNT][TR_REASON_LEN];
static volatile uint8_t tgt_end_count[I3C_I2C_TEST_COUNT];

/* 'P' 到达标记：controller 套件 after 钩子下发的收尾通知（I1） */
static volatile bool tgt_summary_evt;

/* ============ 链接序下标解析（GDB 兜底记账口径，§10.27） ============ */

/* ztest_unit_test 段链接序（SORT_BY_NAME）≠ 总线枚举 id 序。dispatcher
 * 结账记账必须写链接下标槽位（ztest_glue 同口径），否则套件未正常收尾时
 * GDB 兜底读出的结果张冠李戴。运行时按测试名查段内偏移，避免固件内
 * 硬编码第二张顺序表（增删测试项后自动跟随，构建后仍须 nm 复核）。 */
static int tgt_link_index(const char *name)
{
	for (struct ztest_unit_test *t = _ztest_unit_test_list_start;
	     t < _ztest_unit_test_list_end; t++) {
		if (t->name != NULL && strcmp(t->name, name) == 0) {
			return (int)(t - _ztest_unit_test_list_start);
		}
	}
	return -1;
}

static void tgt_record_at_link(uint8_t id, bool passed, const char *reason)
{
	int idx = tgt_link_index(i3c_i2c_test_names[id]);

	if (idx >= 0 && idx < TR_MAX_COUNT) {
		tr_record(idx, passed ? TR_PASS : TR_FAIL, "%s", reason);
	}
}

/* ============ 逐字节解析（ISR 上下文：只记账 + 塞队列） ============ */

static volatile bool tgt_evt_overflow;	/* 事件队列满（不应发生，协议单帧 ≤ 4 事件） */

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

/* ============ I3C6 target 回调 ============ */

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

/* ============ dispatcher 线程：事件处理 ============ */

static void tgt_handle_begin(uint8_t id)
{
	if (id >= I3C_I2C_TEST_COUNT) {
		return;
	}

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
		/* 结账 id 与当前记账 id 不符：旧语义直接记 FAIL 并完成该 id */
		tgt_item_failed[id] = true;
		strncpy(tgt_item_reason[id], "end_id_mismatch",
			TR_REASON_LEN - 1);
		tgt_item_done[id] = true;
		tgt_record_at_link(id, false, "end_id_mismatch");
		return;
	}

	if (tgt_data_mismatch) {
		tgt_failed = true;
		snprintk(tgt_fail_reason, sizeof(tgt_fail_reason),
			 "write_data_mismatch");
	}

	/* 快照累计结果（STRESS 每轮 'E' 都刷新累计值，旧 tr_record 同语义） */
	tgt_item_failed[id] = tgt_failed;
	strncpy(tgt_item_reason[id], tgt_fail_reason, TR_REASON_LEN - 1);
	tgt_item_reason[id][TR_REASON_LEN - 1] = '\0';
	tgt_record_at_link(id, !tgt_failed, tgt_fail_reason);

	/* 完成判定：普通项首笔 'E' 即完成；STRESS 要集满
	 * SWITCH_STRESS_ROUNDS 笔（最后一笔可能因总线故障缺失 → 该项
	 * 测试等 'P' 收尾，与旧固件"末笔 E 丢失 target 无 DONE"行为同构） */
	if (++tgt_end_count[id] >=
	    (id == TEST_SWITCH_STRESS ? SWITCH_STRESS_ROUNDS : 1U)) {
		tgt_item_done[id] = true;
	}
}

static void tgt_apply_switch(bool to_i2c)
{
	uint32_t state_id = to_i2c ? PINCTRL_STATE_PINMUX_I2C
				   : PINCTRL_STATE_DEFAULT;
	int ret = pinctrl_apply_state(tgt_pcfg, state_id);

	if (ret != 0) {
		tgt_failed = true;
		snprintk(tgt_fail_reason, sizeof(tgt_fail_reason),
			 "pinctrl_apply_state(%s):%d", to_i2c ? "i2c" : "i3c", ret);
	}
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
			/* 延时再切：保证 controller 已停止总线操作（协议时序） */
			k_sleep(K_MSEC(TGT_SWITCH_DELAY_MS));
			tgt_apply_switch(ev.id != 0);
			break;
		case TGT_EVT_SUMMARY:
			tgt_summary_evt = true;
			break;
		}
	}
}

/* ============ 公共 init / dispatcher 入口 ============ */

/* pinctrl_states：双核各自自主验证（不经总线命令），验证 IO 区分机制静态配置。
 * dispatcher 启动前执行（旧固件同序），结果写入快照 + 链接下标记账。 */
static void tgt_self_check_pinctrl_states(void)
{
	bool ok = (tgt_pcfg->state_cnt == 2) &&
		  (tgt_pcfg->states[0].id == PINCTRL_STATE_DEFAULT) &&
		  (tgt_pcfg->states[1].id == PINCTRL_STATE_PINMUX_I2C);

	tgt_item_failed[TEST_PINCTRL_STATES] = !ok;
	if (!ok) {
		snprintk(tgt_item_reason[TEST_PINCTRL_STATES], TR_REASON_LEN,
			 "states:%u[%u,%u]", tgt_pcfg->state_cnt,
			 tgt_pcfg->states[0].id, tgt_pcfg->states[1].id);
	} else {
		tgt_item_reason[TEST_PINCTRL_STATES][0] = '\0';
	}
	tgt_item_done[TEST_PINCTRL_STATES] = true;
	tgt_record_at_link(TEST_PINCTRL_STATES, ok,
			   tgt_item_reason[TEST_PINCTRL_STATES]);
}

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

	tgt_reset_rx();
	tgt_failed = false;
	tgt_fail_reason[0] = '\0';

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
}

void target_dispatcher_run(void)
{
	static K_THREAD_STACK_DEFINE(dispatcher_stack, 4096);
	static struct k_thread dispatcher_thread;

	tgt_self_check_pinctrl_states();

	k_thread_create(&dispatcher_thread, dispatcher_stack,
			K_THREAD_STACK_SIZEOF(dispatcher_stack),
			dispatcher_entry, NULL, NULL, NULL,
			7, 0, K_NO_WAIT);
	/* ztest 模式：不 join——dispatcher 常驻处理总线事件，test_main 返回后
	 * 由 ztest 套件线程继续运行（旧 §4.1 "test_main 不得返回"已随框架迁移
	 * 撤销，Task 7 同款）。 */
}

/* ============ ztest 套件（10 项，与 controller 同名双 PASS 判定） ============ */

ZTEST_SUITE(switch_tgt, NULL, NULL, NULL, NULL, NULL);

/* 单项测试体公共实现：等本项结账快照（或 'P' 收尾）后判定。
 * 'P' 到达但本项无结账 → 记 FAIL "no_end_received"（controller 侧流程
 * 异常终止）；断言失败即测试失败（§13.5 可归因）。 */
static void tgt_test_item(enum i3c_i2c_test_id id)
{
	while (!tgt_item_done[id] && !tgt_summary_evt) {
		k_sleep(K_MSEC(20));
	}

	if (tgt_item_failed[id]) {
		zassert_true(false, "%s", tgt_item_reason[id]);
	} else if (!tgt_item_done[id]) {
		zassert_true(false, "no_end_received");
	}
}

ZTEST(switch_tgt, pinctrl_states)
{
	tgt_test_item(TEST_PINCTRL_STATES);
}

ZTEST(switch_tgt, i3c_baseline)
{
	tgt_test_item(TEST_I3C_BASELINE);
}

ZTEST(switch_tgt, switch_i3c_to_i2c)
{
	tgt_test_item(TEST_SWITCH_I3C_TO_I2C);
}

ZTEST(switch_tgt, i2c_rw_1)
{
	tgt_test_item(TEST_I2C_RW_1);
}

ZTEST(switch_tgt, i2c_rw_16)
{
	tgt_test_item(TEST_I2C_RW_16);
}

ZTEST(switch_tgt, i2c_rw_17)
{
	tgt_test_item(TEST_I2C_RW_17);
}

ZTEST(switch_tgt, i2c_rw_100)
{
	tgt_test_item(TEST_I2C_RW_100);
}

ZTEST(switch_tgt, switch_i2c_to_i3c)
{
	tgt_test_item(TEST_SWITCH_I2C_TO_I3C);
}

ZTEST(switch_tgt, i2c_nack_bad_addr)
{
	tgt_test_item(TEST_I2C_NACK_BAD_ADDR);
}

ZTEST(switch_tgt, switch_stress)
{
	tgt_test_item(TEST_SWITCH_STRESS);
}
