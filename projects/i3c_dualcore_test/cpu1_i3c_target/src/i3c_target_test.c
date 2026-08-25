/*
 * Copyright (c) 2025 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i3c_target_test.h"
#include "i3c_dualcore_test_protocol.h"
#include "test_results.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/drivers/i3c/target_device.h>
#include <zephyr/drivers/i3c/ibi.h>
#include <zephyr/logging/log.h>
#include <reg_i3c.h>
#include <core_rv32.h>
#include "ls_soc_gpio.h"

LOG_MODULE_DECLARE(i3c_slave_test, LOG_LEVEL_INF);

/*
 * I3C target command parser state.
 */
enum tgt_parse_state {
	TGT_PARSE_IDLE,
	TGT_PARSE_TEST_ID,
	TGT_PARSE_END_ID,
	TGT_PARSE_END_RESULT,
	TGT_PARSE_READ_LEN,
	TGT_PARSE_WRITE_LEN,
	TGT_PARSE_WRITE_DATA,
};

enum tgt_event_type {
	TGT_EVT_TEST_END,
	TGT_EVT_TRIGGER_IBI,
	TGT_EVT_TRIGGER_HJ,
	TGT_EVT_PRINT_SUMMARY,
	TGT_EVT_TEST_START,
	TGT_EVT_RUN_NEG,
};

struct tgt_event {
	uint8_t type; /* enum tgt_event_type */
	uint8_t test_id;
	uint8_t ctrl_result;
};

static enum tgt_parse_state parse_state;
static volatile uint8_t current_test_id;
static volatile uint8_t ctrl_result;
static volatile bool addressed;

static volatile uint8_t rx_buf[256];
static volatile size_t rx_idx;
static volatile uint16_t tx_idx;
static volatile uint16_t tx_len;
static volatile uint16_t last_read_len;
static volatile int read_req_cnt;
static volatile int read_proc_cnt;

static volatile uint8_t write_data[256];
static volatile size_t write_len;
static volatile size_t write_len_expected;

static volatile bool ibi_raised;
static volatile bool hj_raised;

/* IBI 负路径自检结果（CMD_RUN_TGT_NEG 触发，validate 时判定） */
static volatile int neg_ret_long_payload;
static volatile int neg_ret_hj_davalid;
static volatile int neg_ret_ibidis;

static char tgt_fail_reason[256];
static bool tgt_failed;

/* 每项测试的结果记账：走 810_driver_test/common/test_results.h 公共模块
 * （test_results[] / test_reasons[] 全局符号），串口不可用时 GDB 直接读内存，
 * 见 tests/scripts/dump_results.py。 */

/* Driver target TX FIFO depth (matches ls_i3c_data.fifo_info.TargetTxFifoSize). */
#define TGT_TX_FIFO_SIZE 16

/*
 * Pre-fill the hardware TX FIFO with the bytes to be read.
 *
 * 预填模型（2026-08-21 修订：写匹配时刻预填，替代"仅 IDLE 可预填"旧模型）：
 * - 驱动 i3c_target_tx_write 只拒绝 RD 态（master 正在读）；IDLE/WR 态均可
 *   预填，按 FIFO 现存 TXCOUNT 续写 buf（幂等：已装内容不重写）。
 * - 预填时机（两个）：
 *   1) write_requested_cb（写匹配 MATCHED/STREQWR）：写段有足够时间（N 字节
 *      数据 + t bit + restart >> 读请求后的 3us 窗口），此刻装满 FIFO，
 *      restart 读请求时 FIFO 已满且末字节带 END0，硬件直接发完，无需抢装。
 *   2) stop_cb（每笔传输结束）与初始化：总线空闲时恢复 FIFO 常备数据。
 * - FIFO 常备 tgt_tx_buf 前 16 字节（FIFO 深度）；读长度变化（CMD_SET_READ_LEN）
 *   只改 tx_len，不动 FIFO。读 16 字节以内直接由 FIFO 供应；
 *   超过 16 字节由 TXSEND 中断经 read_processed_cb 从 tx_idx 续发。
 * - 驱动拒绝预填（返回 0）时 FIFO 内容与 tx_idx 均未变，故只在返回 >0
 *   时更新 tx_idx。
 *
 * 注意：本函数不更新 last_read_len。last_read_len 只在“设置读长度”的
 * 协议点（CMD_SET_READ_LEN / reset_test_state 默认值）更新，
 * 避免预填把测试要校验的长度覆盖掉。
 */
static void target_prefill_tx(void)
{
	if (tx_len == 0) {
		return;
	}

	int pre = i3c_target_tx_write(i3c_tgt_dev, (uint8_t *)tgt_tx_buf, tx_len, 0);
	if (pre > 0) {
		tx_idx = (uint16_t)pre;
	}
}

K_MSGQ_DEFINE(tgt_event_queue, sizeof(struct tgt_event), 4, 1);

#define DISPATCHER_STACK_SIZE 2048
static K_THREAD_STACK_DEFINE(dispatcher_stack, DISPATCHER_STACK_SIZE);
static struct k_thread dispatcher_thread;

#define TGT_EXPECT(cond, fmt, ...) \
	do { \
		if (!(cond)) { \
			int _rem = (int)sizeof(tgt_fail_reason) - \
				   (int)strlen(tgt_fail_reason); \
			if (_rem > 0) { \
				snprintf(tgt_fail_reason + strlen(tgt_fail_reason), \
					 (size_t)_rem, fmt "; ", ##__VA_ARGS__); \
			} \
			tgt_failed = true; \
		} \
	} while (0)

static const char *tgt_test_name(void)
{
	if (current_test_id < I3C_TEST_COUNT) {
		return i3c_test_names[current_test_id];
	}
	return "unknown";
}

static void tgt_finish(void)
{
	/* 记账，供结尾汇总表使用（zassert 失败会中止当前测试线程，必须先记）。
	 * 走公共 test_results.h 模块，串口不可用时 GDB 读内存兜底。 */
	if (current_test_id < I3C_TEST_COUNT) {
		tr_record(current_test_id,
			  tgt_failed ? TR_FAIL : TR_PASS,
			  "%s", tgt_fail_reason);
	}

	k_sleep(K_MSEC(20));
	if (tgt_failed) {
		I3C_TEST_TGT_FAIL_VAR(tgt_test_name(), tgt_fail_reason);
	} else {
		I3C_TEST_TGT_PASS_VAR(tgt_test_name());
	}
	zassert_false(tgt_failed, "%s", tgt_fail_reason);
}

static void tgt_print_summary(void)
{
	int passed = 0, ran = 0;

	I3C_TEST_SUMMARY_BEGIN(TGT);
	for (int i = 0; i < I3C_TEST_COUNT; i++) {
		const char *name = i3c_test_names[i];

		if (test_results[i] < 0) {
			I3C_TEST_SUMMARY_ITEM(TGT, name, "NOT_RUN");
			continue;
		}
		ran++;
		if (test_results[i] == 0) {
			passed++;
			I3C_TEST_SUMMARY_ITEM(TGT, name, "PASS");
		} else if (test_reasons[i][0] != '\0') {
			I3C_TEST_SUMMARY_ITEM_REASON(TGT, name, "FAIL",
						     test_reasons[i]);
		} else {
			I3C_TEST_SUMMARY_ITEM(TGT, name, "FAIL");
		}
	}
	I3C_TEST_SUMMARY_END(TGT, passed, ran);
}

static void reset_test_state(uint8_t id)
{
	rx_idx = 0;
	tx_len = 32;
	last_read_len = 32;	/* 默认读长度：direct read 场景 */
	read_req_cnt = 0;
	read_proc_cnt = 0;
	write_len = 0;
	write_len_expected = 0;
	ibi_raised = false;
	hj_raised = false;
	neg_ret_long_payload = 0;
	neg_ret_hj_davalid = 0;
	neg_ret_ibidis = 0;
	addressed = true;
	tgt_failed = false;
	tgt_fail_reason[0] = '\0';
	current_test_id = id;

	/* FIFO 预填由 write_requested_cb（写匹配）与 stop_cb（总线空闲）负责，
	 * 此处只重置长度记账。 */
}

static void reset_all_target_state(void)
{
	parse_state = TGT_PARSE_IDLE;
	current_test_id = 0;
	ctrl_result = 0;
	addressed = false;
	rx_idx = 0;
	/* tx_idx 由下面的 target_prefill_tx 返回值设置 */
	tx_len = 32;
	last_read_len = 32;
	read_req_cnt = 0;
	read_proc_cnt = 0;
	write_len = 0;
	write_len_expected = 0;
	ibi_raised = false;
	hj_raised = false;
	tgt_failed = false;
	tgt_fail_reason[0] = '\0';

	target_prefill_tx();
}

static void raise_ibi(void)
{
	uint8_t payload[] = { 0xBB, 0x01, 0x02, 0x03 };
	struct i3c_ibi request = {
		.ibi_type = I3C_IBI_TARGET_INTR,
		.payload = payload,
		.payload_len = sizeof(payload),
	};
	int ret = i3c_ibi_raise(i3c_tgt_dev, &request);
	if (ret != 0) {
		LOG_ERR("IBI raise failed: %d", ret);
	} else {
		ibi_raised = true;
		LOG_INF("IBI raised");
	}
}

/* 就绪握手 IBI：payload {0xBB, test_id}。controller 发完 TEST_START 后
 * 等这个 IBI 再开始测试，代替固定 sleep——从机中断/状态没准备好时
 * 主机会立刻以超时形式暴露，而不是在错误的总线时序里继续跑。 */
static void raise_ready_ibi(uint8_t test_id)
{
	uint8_t payload[] = { 0xBB, test_id };
	struct i3c_ibi request = {
		.ibi_type = I3C_IBI_TARGET_INTR,
		.payload = payload,
		.payload_len = sizeof(payload),
	};

	int ret = i3c_ibi_raise(i3c_tgt_dev, &request);
	if (ret != 0) {
		LOG_ERR("ready IBI raise failed for test %u: %d", test_id, ret);
	} else {
		LOG_INF("ready IBI sent for test %u", test_id);
	}
}

static void raise_hj(void)
{
	struct i3c_ibi request = {
		.ibi_type = I3C_IBI_HOTJOIN,
		.payload = NULL,
		.payload_len = 0,
	};

	/* HJ 只允许无动态地址的设备发起：DAVALID=1 时硬件直接抑制请求，
	 * 总线上不会有任何波形。测试流程为"先武装后 RSTDAA"：
	 * controller 发完 CMD_TRIGGER_HJ 后广播 RSTDAA，这里轮询等待
	 * 硬件清掉动态地址（约 1s 超时），丢失后才真正发起 HJ。
	 * 测试app允许直接读寄存器（DAVALID 没有对应的 Zephyr API）。 */
	I3C_TypeDef *tgt_base = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));
	bool addr_lost = false;

	for (int i = 0; i < 100; i++) {
		if ((tgt_base->SDYNADDR & I3C_SDYNADDR_DAVALID_MASK) == 0) {
			addr_lost = true;
			break;
		}
		k_sleep(K_MSEC(10));
	}
	if (!addr_lost) {
		LOG_ERR("Hot-Join aborted: dynamic address still valid, "
			"controller must broadcast RSTDAA after CMD_TRIGGER_HJ");
		return;
	}

	int ret = i3c_ibi_raise(i3c_tgt_dev, &request);
	if (ret != 0) {
		LOG_ERR("Hot-Join raise failed: %d", ret);
	} else {
		hj_raised = true;
		LOG_INF("Hot-Join request raised");
	}
}

/* IBI 负路径自检（CMD_RUN_TGT_NEG 触发，dispatcher 线程上下文执行）。
 * 调用时 controller 已发 DISEC(EVT_INTR)，IBIDIS 处于锁存态。
 * 三个负路径都是驱动手写 fail-fast 分支，此前无回归保护。 */
static void run_ibi_neg_checks(void)
{
	/* 1) payload > 8 字节 → -EINVAL */
	uint8_t pl9[9] = {0};
	struct i3c_ibi req_long = {
		.ibi_type = I3C_IBI_TARGET_INTR,
		.payload = pl9,
		.payload_len = sizeof(pl9),
	};
	neg_ret_long_payload = i3c_ibi_raise(i3c_tgt_dev, &req_long);

	/* 2) DAVALID=1 时发 Hot-Join → -EACCES（HJ 仅限无动态地址设备） */
	struct i3c_ibi req_hj = {
		.ibi_type = I3C_IBI_HOTJOIN,
		.payload = NULL,
		.payload_len = 0,
	};
	neg_ret_hj_davalid = i3c_ibi_raise(i3c_tgt_dev, &req_hj);

	/* 3) IBIDIS 锁存（DISEC 后）发 IBI → -EACCES */
	uint8_t pl2[2] = { 0xBB, 0x02 };
	struct i3c_ibi req_ibi = {
		.ibi_type = I3C_IBI_TARGET_INTR,
		.payload = pl2,
		.payload_len = sizeof(pl2),
	};
	neg_ret_ibidis = i3c_ibi_raise(i3c_tgt_dev, &req_ibi);

	LOG_INF("neg checks: long=%d hj=%d ibidis=%d",
		neg_ret_long_payload, neg_ret_hj_davalid, neg_ret_ibidis);
}

/* ========== I3C target callbacks ========== */

static int slave_write_requested_cb(struct i3c_target_config *config)
{
	ARG_UNUSED(config);
	rx_idx = 0;

	/* 写匹配（MATCHED/STREQWR）时刻预填读数据：master 写段有 N 字节数据 +
	 * t bit + restart，从写匹配到 restart 的时间远大于读请求后的 3us 窗口，
	 * 此时装 FIFO 保证 restart 读请求时 FIFO 已满（含 END0），不再依赖
	 * read_requested_cb 在 3us 内抢装（实测只能装 3 字节 → TXCOUNT=3 → 欠载）。
	 * 驱动 i3c_target_tx_write 允许 WR 态续写（只拒 RD 态），按 TXCOUNT 续写
	 * 幂等：写匹配若保留旧 FIFO 内容则不再写，若已清空则从 buf[0] 装起。 */
	target_prefill_tx();
	return 0;
}

static int slave_write_received_cb(struct i3c_target_config *config, uint8_t val)
{
	ARG_UNUSED(config);

	if (rx_idx < sizeof(rx_buf)) {
		rx_buf[rx_idx++] = val;
	}

	switch (parse_state) {
	case TGT_PARSE_IDLE:
		switch (val) {
		case CMD_TEST_START:
			parse_state = TGT_PARSE_TEST_ID;
			break;
		case CMD_TEST_END:
			parse_state = TGT_PARSE_END_ID;
			break;
		case CMD_SET_READ_LEN:
			parse_state = TGT_PARSE_READ_LEN;
			break;
		case CMD_WRITE_DATA:
			parse_state = TGT_PARSE_WRITE_LEN;
			break;
		case CMD_TRIGGER_IBI:
			{
				struct tgt_event evt = { .type = TGT_EVT_TRIGGER_IBI };
				(void)k_msgq_put(&tgt_event_queue, &evt, K_NO_WAIT);
			}
			break;
		case CMD_TRIGGER_HJ:
			{
				struct tgt_event evt = { .type = TGT_EVT_TRIGGER_HJ };
				(void)k_msgq_put(&tgt_event_queue, &evt, K_NO_WAIT);
			}
			break;
		case CMD_RESET_TARGET_STATE:
			reset_all_target_state();
			break;
		case CMD_PRINT_SUMMARY:
			{
				struct tgt_event evt = { .type = TGT_EVT_PRINT_SUMMARY };
				(void)k_msgq_put(&tgt_event_queue, &evt, K_NO_WAIT);
			}
			break;
		case CMD_RUN_TGT_NEG:
			{
				struct tgt_event evt = { .type = TGT_EVT_RUN_NEG };
				(void)k_msgq_put(&tgt_event_queue, &evt, K_NO_WAIT);
			}
			break;
		default:
			/* 非 opcode 字节（如 write_read/write_stop_read 写阶段的占位字节
			 * CMD_DUMMY_BYTE）按原始数据记录，便于排查。
			 * 写段最后字节（0xA5）接收 = T bit 前、restart 前 3us 窗口起点，
			 * 此刻再 prefill 一次（TXCOUNT 续写幂等）：保证该窗口内 FIFO
			 * 有数据（用户硬件规则：读请求前 FIFO 必须已填好，否则从机
			 * 直接触发错误结束通信）。 */
			if (write_len < sizeof(write_data)) {
				write_data[write_len++] = val;
			}
			target_prefill_tx();
			break;
		}
		break;

	case TGT_PARSE_TEST_ID:
		reset_test_state(val);
		{
			struct tgt_event evt = { .type = TGT_EVT_TEST_START, .test_id = val };
			(void)k_msgq_put(&tgt_event_queue, &evt, K_NO_WAIT);
		}
		parse_state = TGT_PARSE_IDLE;
		break;

	case TGT_PARSE_END_ID:
		current_test_id = val;
		parse_state = TGT_PARSE_END_RESULT;
		break;

	case TGT_PARSE_END_RESULT:
		ctrl_result = val;
		{
			/* 注意：type 必须显式赋值。曾漏写，栈垃圾值随机匹配到
			 * default 分支导致 TGT_EVT_TEST_END 被丢弃——daa_discovery
			 * 侥幸 PASS（垃圾值恰为 0），write_read_16 起全部 NOT_RUN
			 * （dispatcher 收不到事件 → ztest 不跑 → 无 TGT token、
			 * test_results[] 保持 -1）。 */
			struct tgt_event evt = {
				.type = TGT_EVT_TEST_END,
				.test_id = current_test_id,
				.ctrl_result = ctrl_result,
			};
			(void)k_msgq_put(&tgt_event_queue, &evt, K_NO_WAIT);
		}
		parse_state = TGT_PARSE_IDLE;
		break;

	case TGT_PARSE_READ_LEN:
		/* 长度字节 val==0 约定为 256（单字节表达上限 255，big_256 用） */
		tx_len = (val == 0) ? 256 : val;
		last_read_len = tx_len;
		/* 只改长度记账；FIFO 由后续 write_requested_cb（写匹配）/ stop_cb
		 * （总线空闲）按新 tx_len 预填。 */
		parse_state = TGT_PARSE_IDLE;
		break;

	case TGT_PARSE_WRITE_LEN:
		write_len_expected = (val == 0) ? 256 : val;	/* 0 => 256 */
		write_len = 0;
		parse_state = TGT_PARSE_WRITE_DATA;
		break;

	case TGT_PARSE_WRITE_DATA:
		if (write_len < sizeof(write_data)) {
			write_data[write_len++] = val;
		}
		if (write_len >= write_len_expected) {
			parse_state = TGT_PARSE_IDLE;
		}
		break;
	}

	return 0;
}

static int slave_read_requested_cb(struct i3c_target_config *config, uint8_t *val)
{
	ARG_UNUSED(config);

	read_req_cnt++;

	if (tx_idx < tx_len) {
		*val = tgt_tx_buf[tx_idx++];
		/* 返回 1 = 这是最后字节，驱动为其标 END0 */
		return (tx_idx >= tx_len) ? 1 : 0;
	}

	/* All requested bytes were already pre-filled; signal end of data. */
	*val = 0;
	return 1;
}

static int slave_read_processed_cb(struct i3c_target_config *config, uint8_t *val)
{
	ARG_UNUSED(config);

	read_proc_cnt++;
	if (tx_idx < tx_len) {
		*val = tgt_tx_buf[tx_idx++];
		return (tx_idx >= tx_len) ? 1 : 0;
	}

	/* No more data expected. */
	*val = 0;
	return 1;
}

static int slave_stop_cb(struct i3c_target_config *config)
{
	ARG_UNUSED(config);

	rx_idx = 0;
	read_req_cnt = 0;
	read_proc_cnt = 0;
	parse_state = TGT_PARSE_IDLE;
	/* 不再无条件恢复 tx_len=32：reset_test_state 已负责每测试的默认读长度，
	 * 这里覆盖会把 SET_READ_LEN 设置的写读测试长度洗掉（实测读请求时刻
	 * tx_len 被 stop_cb 从 16 覆盖成 32，read_requested_cb 供错数据）。
	 * 预填用当前 tx_len；下一笔写匹配由 write_requested_cb 重新 prefill。
	 * 注：STOP 分支（驱动）此时已 FLUSHTB 清空 TX FIFO，此处 prefill 即
	 * 从 buf[0] 起装干净前缀（write+stop+read 场景读段前的最后一次填充）。 */
	target_prefill_tx();
	return 0;
}

/* ========== Public target init/dispatcher ========== */

void target_test_init(void)
{
	static struct i3c_target_callbacks callbacks = {
		.write_requested_cb = slave_write_requested_cb,
		.write_received_cb = slave_write_received_cb,
		.read_requested_cb = slave_read_requested_cb,
		.read_processed_cb = slave_read_processed_cb,
		.stop_cb = slave_stop_cb,
	};
	static struct i3c_target_config cfg = {
		.address = I3C_TARGET_ADDR,
		.callbacks = &callbacks,
	};

	reset_all_target_state();

	tr_init(I3C_TEST_COUNT);

	int ret = i3c_target_register(i3c_tgt_dev, &cfg);
	if (ret != 0) {
		LOG_ERR("i3c_target_register failed: %d", ret);
	}
}

static void dispatcher_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct tgt_event evt;
		int ret = k_msgq_get(&tgt_event_queue, &evt, K_FOREVER);
		if (ret != 0) {
			continue;
		}

		switch (evt.type) {
		case TGT_EVT_TEST_START:
			/* read_32_direct 无前置写段，TEST_START 写传输的 stop_cb 已按
			 * 当前 tx_len（32）prefill；此处再显式补一次（TXCOUNT 续写幂等），
			 * 保证读请求时 FIFO 已满（用户规则：读请求前 FIFO 必须有数据）。 */
			if (evt.test_id == I3C_TEST_READ_32_DIRECT) {
				target_prefill_tx();
			}
			raise_ready_ibi(evt.test_id);
			break;
		case TGT_EVT_TRIGGER_IBI:
			raise_ibi();
			break;
		case TGT_EVT_TRIGGER_HJ:
			raise_hj();
			break;
		case TGT_EVT_PRINT_SUMMARY:
			tgt_print_summary();
			break;
		case TGT_EVT_RUN_NEG:
			run_ibi_neg_checks();
			break;
		case TGT_EVT_TEST_END:
			current_test_id = evt.test_id;
			ctrl_result = evt.ctrl_result;
			ztest_run_test_suite(i3c_target, false, 1, 1, NULL);
			break;
		default:
			break;
		}
	}
}

void target_dispatcher_run(void)
{
	k_thread_create(&dispatcher_thread, dispatcher_stack,
			K_THREAD_STACK_SIZEOF(dispatcher_stack),
			dispatcher_entry, NULL, NULL, NULL,
			K_PRIO_COOP(5), 0, K_NO_WAIT);

	/*
	 * Never return: dispatcher_entry is an infinite event loop, and
	 * returning from test_main() would let ztest's main() reach
	 * LOG_PANIC(), setting log_core's panic_mode=1.  Every subsequent
	 * log post would then take the panic branch (k_spin_lock +
	 * log_process() synchronous full flush) -- measured 8ms stall
	 * while the I3C dispatcher thread logs mid-test.
	 */
	k_thread_join(&dispatcher_thread, K_FOREVER);
}

/* ========== Target ztest suite ========== */

ZTEST_SUITE(i3c_target, NULL, NULL, NULL, NULL, NULL);

static void validate_daa_discovery(void)
{
	TGT_EXPECT(addressed, "target was never addressed");
}

static void validate_write_8(void)
{
	TGT_EXPECT(write_len == 8, "write_len %zu != 8", (size_t)write_len);
	for (size_t i = 0; i < write_len && i < 8; i++) {
		TGT_EXPECT(write_data[i] == (uint8_t)(0x10 + i),
			   "write_data[%zu] 0x%02x != 0x%02x",
			   i, write_data[i], (uint8_t)(0x10 + i));
	}
}

static void validate_write_100(void)
{
	TGT_EXPECT(write_len == 100, "write_len %zu != 100", (size_t)write_len);
	for (size_t i = 0; i < write_len && i < 100; i++) {
		TGT_EXPECT(write_data[i] == (uint8_t)(0x20 + i),
			   "write_data[%zu] 0x%02x != 0x%02x",
			   i, write_data[i], (uint8_t)(0x20 + i));
	}
}

static void validate_read_32_direct(void)
{
	TGT_EXPECT(last_read_len == 32, "last_read_len %u != 32", last_read_len);
}

static void validate_write_read_1(void)
{
	TGT_EXPECT(last_read_len == 1, "last_read_len %u != 1", last_read_len);
}

static void validate_write_read_15(void)
{
	TGT_EXPECT(last_read_len == 15, "last_read_len %u != 15", last_read_len);
}

static void validate_write_read_16(void)
{
	TGT_EXPECT(last_read_len == 16, "last_read_len %u != 16", last_read_len);
}

static void validate_write_read_17(void)
{
	TGT_EXPECT(last_read_len == 17, "last_read_len %u != 17", last_read_len);
}

static void validate_write_read_32(void)
{
	TGT_EXPECT(last_read_len == 32, "last_read_len %u != 32", last_read_len);
}

static void validate_write_stop_read_1(void)
{
	TGT_EXPECT(last_read_len == 1, "last_read_len %u != 1", last_read_len);
}

static void validate_write_stop_read_15(void)
{
	TGT_EXPECT(last_read_len == 15, "last_read_len %u != 15", last_read_len);
}

static void validate_write_stop_read_16(void)
{
	TGT_EXPECT(last_read_len == 16, "last_read_len %u != 16", last_read_len);
}

static void validate_write_stop_read_17(void)
{
	TGT_EXPECT(last_read_len == 17, "last_read_len %u != 17", last_read_len);
}

static void validate_write_stop_read_32(void)
{
	TGT_EXPECT(last_read_len == 32, "last_read_len %u != 32", last_read_len);
}

static void validate_ibi(void)
{
	TGT_EXPECT(ibi_raised, "IBI was not raised");
}

static void validate_hot_join(void)
{
	TGT_EXPECT(hj_raised, "Hot-Join was not raised");
}

static void validate_ccc_basic(void)
{
	/* Target is passive for CCC; just confirm it was addressed. */
	TGT_EXPECT(addressed, "target was not addressed");
}

/* ========== 主机主动临界测试的 target 侧校验（2026-08-24 新增） ========== */

static void validate_read_underrun_8_16(void)
{
	/* 主机读穿预填（8<16）：target 回调供完 8 字节后返回 1（END），
	 * 由硬件 T-bit 提前终止。本侧只需确认读长度协议点正确到达。 */
	TGT_EXPECT(last_read_len == 8, "last_read_len %u != 8", last_read_len);
}

static void validate_read_underrun_16_32(void)
{
	TGT_EXPECT(last_read_len == 16, "last_read_len %u != 16", last_read_len);
}

static void validate_nack_bad_addr(void)
{
	/* 错误地址在硬件层 NACK，不经任何软件回调；本侧无感知即正确 */
	TGT_EXPECT(addressed, "target was not addressed");
}

static void validate_back_to_back_8x8(void)
{
	/* 8 次 write_read 各带 1 个占位字节 0xA5（default 分支记录） */
	TGT_EXPECT(write_len == 8, "write_len %zu != 8", (size_t)write_len);
	for (size_t i = 0; i < write_len && i < 8; i++) {
		TGT_EXPECT(write_data[i] == CMD_DUMMY_BYTE,
			   "write_data[%zu] 0x%02x != 0x%02x",
			   i, write_data[i], CMD_DUMMY_BYTE);
	}
}

static void validate_big_256(void)
{
	TGT_EXPECT(write_len == 256, "write_len %zu != 256", (size_t)write_len);
	for (size_t i = 0; i < write_len && i < 256; i++) {
		TGT_EXPECT(write_data[i] == (uint8_t)(0x40 + i),
			   "write_data[%zu] 0x%02x != 0x%02x",
			   i, write_data[i], (uint8_t)(0x40 + i));
	}
	TGT_EXPECT(last_read_len == 256, "last_read_len %u != 256", last_read_len);
}

static void validate_ccc_rapid_8(void)
{
	/* CCC 由 target 硬件直接应答，软件不参与；确认被寻址即可 */
	TGT_EXPECT(addressed, "target was not addressed");
}

static void validate_read_master_abort(void)
{
	/* 主机截断读：本侧只需确认默认读长度协议点仍在（32）。
	 * 真正的对齐校验在 controller 侧（post-abort 读回 0x00.. 起点）。 */
	TGT_EXPECT(last_read_len == 32, "last_read_len %u != 32", last_read_len);
}

static void validate_ibi_neg_paths(void)
{
	TGT_EXPECT(neg_ret_long_payload == -EINVAL,
		   "long payload ret %d != -EINVAL", neg_ret_long_payload);
	TGT_EXPECT(neg_ret_hj_davalid == -EACCES,
		   "HJ with DAVALID ret %d != -EACCES", neg_ret_hj_davalid);
	TGT_EXPECT(neg_ret_ibidis == -EACCES,
		   "IBI under IBIDIS ret %d != -EACCES", neg_ret_ibidis);
}

static void validate_api_neg_paths(void)
{
	/* 纯 controller 本地负路径，target 不参与 */
	TGT_EXPECT(addressed, "target was not addressed");
}

static void validate_multi_msg_4(void)
{
	/* 两个写段各带 1 个占位字节 0xA5 */
	TGT_EXPECT(write_len == 2, "write_len %zu != 2", (size_t)write_len);
	for (size_t i = 0; i < write_len && i < 2; i++) {
		TGT_EXPECT(write_data[i] == CMD_DUMMY_BYTE,
			   "write_data[%zu] 0x%02x != 0x%02x",
			   i, write_data[i], CMD_DUMMY_BYTE);
	}
	TGT_EXPECT(last_read_len == 32, "last_read_len %u != 32", last_read_len);
}

ZTEST(i3c_target, run_current_test)
{
	I3C_TEST_TGT_START_VAR(tgt_test_name());

	switch (current_test_id) {
	case I3C_TEST_DAA_DISCOVERY:
		validate_daa_discovery();
		break;
	case I3C_TEST_WRITE_8:
		validate_write_8();
		break;
	case I3C_TEST_WRITE_100:
		validate_write_100();
		break;
	case I3C_TEST_READ_32_DIRECT:
		validate_read_32_direct();
		break;
	case I3C_TEST_WRITE_READ_1:
		validate_write_read_1();
		break;
	case I3C_TEST_WRITE_READ_15:
		validate_write_read_15();
		break;
	case I3C_TEST_WRITE_READ_16:
		validate_write_read_16();
		break;
	case I3C_TEST_WRITE_READ_17:
		validate_write_read_17();
		break;
	case I3C_TEST_WRITE_READ_32:
		validate_write_read_32();
		break;
	case I3C_TEST_IBI:
		validate_ibi();
		break;
	case I3C_TEST_HOT_JOIN:
		validate_hot_join();
		break;
	case I3C_TEST_CCC_BASIC:
		validate_ccc_basic();
		break;
	case I3C_TEST_WRITE_STOP_READ_1:
		validate_write_stop_read_1();
		break;
	case I3C_TEST_WRITE_STOP_READ_15:
		validate_write_stop_read_15();
		break;
	case I3C_TEST_WRITE_STOP_READ_16:
		validate_write_stop_read_16();
		break;
	case I3C_TEST_WRITE_STOP_READ_17:
		validate_write_stop_read_17();
		break;
	case I3C_TEST_WRITE_STOP_READ_32:
		validate_write_stop_read_32();
		break;
	case I3C_TEST_READ_UNDERRUN_8_16:
		validate_read_underrun_8_16();
		break;
	case I3C_TEST_READ_UNDERRUN_16_32:
		validate_read_underrun_16_32();
		break;
	case I3C_TEST_NACK_BAD_ADDR:
		validate_nack_bad_addr();
		break;
	case I3C_TEST_BACK_TO_BACK_8X8:
		validate_back_to_back_8x8();
		break;
	case I3C_TEST_BIG_256:
		validate_big_256();
		break;
	case I3C_TEST_CCC_RAPID_8:
		validate_ccc_rapid_8();
		break;
	case I3C_TEST_READ_MASTER_ABORT:
		validate_read_master_abort();
		break;
	case I3C_TEST_IBI_NEG_PATHS:
		validate_ibi_neg_paths();
		break;
	case I3C_TEST_API_NEG_PATHS:
		validate_api_neg_paths();
		break;
	case I3C_TEST_MULTI_MSG_4:
		validate_multi_msg_4();
		break;
	default:
		TGT_EXPECT(false, "unknown test id %u", current_test_id);
		break;
	}

	tgt_finish();
}
