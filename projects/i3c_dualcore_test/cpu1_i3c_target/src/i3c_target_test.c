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
#include <zephyr/ztest_test.h>
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
	TGT_PARSE_INDEXED_READ,	/* CMD_INDEXED_READ 后等索引字节 */
	TGT_PARSE_INDEXED_READ_DELAY,	/* CMD_INDEXED_READ_DELAY 后等 索引+延时 2 字节 */
	TGT_PARSE_INDEXED_READ_DELAY_LEN,	/* CMD_INDEXED_READ_DELAY_LEN 后等
						 * 索引+延时+长度 3 字节 */
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

static volatile uint8_t rx_buf[256];
static volatile size_t rx_idx;
static volatile uint16_t tx_idx;
static volatile uint16_t tx_len;
static volatile uint16_t tx_offset;	/* 索引读：tgt_tx_buf 起始偏移（CMD_INDEXED_READ 设置） */
static volatile bool indexed_read_pending;	/* 索引读进行中：写匹配时 tx_offset 未收到，延迟预填 */
static volatile uint8_t delay_read_idx;	/* 延时索引读：0=未收索引 1=已收索引（首字节暂存于 delay_read_offset） */
static volatile uint8_t delay_read_offset;	/* 延时索引读：暂存索引字节（延时字节后 prefill 生效） */
static volatile bool delay_read_seen;	/* 延时索引读：收到过 'Y' opcode（stop_cb 不清，validate 用） */
static volatile bool delay_len_seen;	/* 延时长度索引读：收到过 'Z' opcode（stop_cb 不清，validate 用） */
static volatile uint8_t delay_len_value;	/* 延时长度索引读：收到的读长度原始值（0→256 解析处换算） */
static volatile uint8_t delay_len_delay_step;	/* 延时长度索引读：暂存延时档位（'Z' 第 2 字节，
						 * len 字节触发 spin 时用它计算） */
/* nack_recovery 诊断（临时探针，闭环后清理）：
 * 'X' opcode 到达时刻 parse_state、索引字节到达时刻 tx_offset、
 * 读匹配（read_requested_cb）时刻 tx_offset/tx_idx——区分
 * "'X' 未按 opcode 分发 / 索引未收到 / 偏移未复位"三种失败路径 */
static volatile uint32_t nackx_opcode_pstate;
static volatile uint32_t nackx_idx_txoff;
static volatile uint32_t nackx_read_txoff;
static volatile uint32_t nackx_read_txidx;
static volatile uint32_t spin_ns_last;	/* 延时索引读：最后一笔 spin 实际消耗 cycles（记账核对） */
static volatile uint32_t spin_done_actstate;	/* spin 完成时刻 SSTATUS.ACTSTATE（总线处于哪一阶段） */
static volatile uint32_t spin_done_cyc;		/* spin 完成时刻 cycle */
static volatile uint32_t matched_cyc_last;	/* 本笔 spin 期间 MATCHED（新 match）硬件置位时刻 */
static volatile uint32_t matched_cyc_first;	/* 首笔（step 0）MATCHED 置位时刻 */
static volatile uint32_t wait_sst_entry;	/* wait/spin 入口 SSTATUS 快照（MATCHED 是否已置位） */
static volatile uint32_t wait_sst_timeout;	/* wait_matched 超时时刻 SSTATUS（诊断用） */
static volatile uint32_t hit_txcount;		/* wait 命中时刻 TX FIFO 计数（16=常备未发，<16=数据已发出） */
static volatile uint32_t hit_sst;		/* wait 命中时刻 SSTATUS 快照 */
static volatile uint32_t sst_pre_flush;		/* FLUSHTB 前（spin/wait 结束时刻）SSTATUS 快照 */
static volatile uint32_t pre_fill_cyc;		/* FLUSHTB+prefill 完成时刻 cycle */
#define WDR_MAX_STEP 256
static volatile uint16_t wdr_cnt;		/* 已记档数（每档一笔，≤256） */
static volatile uint32_t wdr_entry_sst[WDR_MAX_STEP];	/* 每档 wait/spin 入口 SSTATUS */
static volatile int32_t wdr_match_delta[WDR_MAX_STEP];	/* 命中-delay处理 cycles（-1=未命中） */
static volatile uint32_t wdr_txcount[WDR_MAX_STEP];	/* 命中时 TXCOUNT（0xFFFF=未命中） */
static volatile uint32_t wdr_hit_sst[WDR_MAX_STEP];	/* 命中时 SSTATUS 快照 */
static volatile uint32_t rx_delay_byte_cyc;	/* 收到 delay 字节（ISR 处理）时刻 cycle */
static volatile uint32_t rx_delay_byte_cyc_first;	/* 首笔（step 0，spin=0ns）delay 字节时刻 */
static volatile bool first_cyc_done;		/* 首笔 cycle 记账完成 */
static volatile uint16_t last_read_len;
static volatile int read_req_cnt;
static volatile int read_proc_cnt;
/* 本测试项内粘滞的"经回调供出的字节数"（仅 TEST_START / 全局复位清零，
 * stop_cb 不清）。语义边界（源码依据 i3c_linkedsemi.c:2136/2211/2857）：
 * target_prefill_tx 经 i3c_target_tx_write 直接写 SWDATAB，**不经过**
 * read_requested_cb / read_processed_cb；只有 FIFO 装不下、由 TXSEND
 * 中断续发的部分才走回调。故本计数 = 本项实际供出字节数 − FIFO 预填量
 * （min(16, tx_len)），是 target 软件唯一能静态确证的"实际供出字节"
 * 证据。len ≤ 16 时 FIFO 一次装完，回调不被调用（txcount != 0 分支跳
 * 过回调），target 软件对 master 实际读了多少字节**不可观测**。 */
static volatile uint32_t rd_cb_bytes;

static volatile uint8_t write_data[256];
static volatile size_t write_len;
static volatile size_t write_len_expected;

static volatile bool ibi_raised;
static volatile bool hj_raised;

/* IBI 负路径自检结果（CMD_RUN_TGT_NEG 触发，validate 时判定） */
static volatile int neg_ret_long_payload;
static volatile int neg_ret_hj_davalid;
static volatile int neg_ret_ibidis;

/* SETAASA/SETDASA 探针：收到 CMD_AASA_PROBE 时（此刻已被新地址寻址）
 * 在 ISR 里快照 SDYNADDR 寄存器（只读寄存器，不打日志不拿锁，§4.2 合规），
 * validate 时核对 DAVALID/DADDR/DCAUSE。 */
static volatile bool aasa_probe_seen;
static volatile uint32_t aasa_snap;

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
 * 预填模型（写匹配时刻预填）：
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

	/* 索引读：从 tx_offset 偏移开始预填；普通读：从 0 开始 */
	const uint8_t *buf = tgt_tx_buf + tx_offset;
	int pre = i3c_target_tx_write(i3c_tgt_dev, (uint8_t *)buf, tx_len, 0);
	if (pre > 0) {
		tx_idx = (uint16_t)pre;
	}
	/* tx_offset 保留到本笔读完成（read_processed_cb 续发用），
	 * 由 stop_cb（传输结束）复位为 0 */
}

/* 忙等延时（ns）：ISR 上下文可用（仅读 cycle counter），用于
 * write_delay_read_16 测试——模拟"从机根据写段 data 计算回读数据"
 * 所消耗的时间，实测写段结束到读段欠载的可用准备窗口。
 * 返回实际消耗的 cycle 数（供记账核对 spin 是否按名义值生效）。 */
static inline uint32_t spin_ns(uint32_t ns, volatile uint32_t *matched_cyc)
{
	uint32_t target = (uint32_t)((uint64_t)ns *
				     CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000000ULL);
	uint32_t start = k_cycle_get_32();
	I3C_TypeDef *base = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));

	*matched_cyc = 0;	/* 0 = spin 期间"新 match"（MATCHED 标志）未置位 */
	hit_txcount = 0xFFFFU;	/* 清零防残留（未命中时保持 0xFFFF） */
	hit_sst = 0;
	wait_sst_entry = base->SSTATUS;	/* 入口快照：本笔 restart+地址 是否已匹配 */
	while ((uint32_t)(k_cycle_get_32() - start) < target) {
		/* 轮询硬件 MATCHED 标志：记录本笔 restart+地址匹配的物理时刻
		 * （读 SSTATUS 不清标志；写段匹配的标志已被前序 ISR 清除，
		 * 此处捕获的是 restart 后的新匹配） */
		if (*matched_cyc == 0U &&
		    (base->SSTATUS & I3C_SSTATUS_MATCHED_MASK)) {
			*matched_cyc = k_cycle_get_32();
			hit_txcount = (base->SDATACTRL & I3C_SDATACTRL_TXCOUNT_MASK) >>
				      I3C_SDATACTRL_TXCOUNT_POS;
			hit_sst = base->SSTATUS;
		}
	}
	return (uint32_t)(k_cycle_get_32() - start);
}

/* 等待本笔"新 match"（MATCHED 硬件置位）或超时，返回置位时刻（0=超时）。
 * 无人工延时场景（step 0）测量 delay 字节 → 地址匹配的物理间隔用。 */
static inline void wait_matched(uint32_t timeout_ns, volatile uint32_t *matched_cyc)
{
	uint32_t target = (uint32_t)((uint64_t)timeout_ns *
				     CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000000ULL);
	uint32_t start = k_cycle_get_32();
	I3C_TypeDef *base = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));

	*matched_cyc = 0;
	hit_txcount = 0xFFFFU;	/* 清零防残留（未命中时保持 0xFFFF） */
	hit_sst = 0;
	wait_sst_entry = base->SSTATUS;	/* 入口快照：delay 字节处理时 restart+地址 是否已匹配 */
	do {
		if (base->SSTATUS & I3C_SSTATUS_MATCHED_MASK) {
			*matched_cyc = k_cycle_get_32();
			hit_txcount = (base->SDATACTRL & I3C_SDATACTRL_TXCOUNT_MASK) >>
				      I3C_SDATACTRL_TXCOUNT_POS;
			hit_sst = base->SSTATUS;
			return;
		}
	} while ((uint32_t)(k_cycle_get_32() - start) < target);
	wait_sst_timeout = base->SSTATUS;	/* 超时时刻快照（诊断：MATCHED 一直 0 时看其他标志） */
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

static void tgt_finish(void)
{
	/* 记账，供结尾汇总表使用（zassert 失败会中止当前测试线程，必须先记）。
	 * 走公共 test_results.h 模块，串口不可用时 GDB 读内存兜底。
	 *
	 * 索引 = 协议枚举 id：target 侧 test_main 不返回、zt_run_all 不执行
	 * （§4.1 dispatcher k_thread_join），ztest_glue 不参与记账，此处按
	 * current_test_id（枚举序）记账；dump_results.py 的 TGT 测试名列表
	 * 按协议枚举序对齐（与 controller 侧链接序记账分工）。 */
	if (current_test_id < I3C_TEST_COUNT) {
		tr_record(current_test_id,
			  tgt_failed ? TR_FAIL : TR_PASS,
			  "%s", tgt_fail_reason);
	}

	k_sleep(K_MSEC(20));

	zassert_false(tgt_failed, "%s", tgt_fail_reason);
}

static void tgt_print_summary(void)
{
	int passed = 0, ran = 0;

	for (int i = 0; i < I3C_TEST_COUNT; i++) {
		const char *name = i3c_test_names[i];

		if (test_results[i] < 0) {
			LOG_INF("SUMMARY: NOT_RUN %s", name);
			continue;
		}
		ran++;
		if (test_results[i] == 0) {
			passed++;
			LOG_INF("SUMMARY: PASS %s", name);
		} else if (test_reasons[i][0] != '\0') {
			LOG_INF("SUMMARY: FAIL %s: %s", name, test_reasons[i]);
		} else {
			LOG_INF("SUMMARY: FAIL %s", name);
		}
	}
	LOG_INF("SUMMARY: %d/%d PASS", passed, ran);
}

static void reset_test_state(uint8_t id)
{
	rx_idx = 0;
	tx_len = 32;
	tx_offset = 0;
	indexed_read_pending = false;
	last_read_len = 32;	/* 默认读长度：direct read 场景 */
	read_req_cnt = 0;
	read_proc_cnt = 0;
	rd_cb_bytes = 0;
	delay_read_seen = false;
	delay_len_seen = false;
	spin_done_actstate = 0;
	spin_done_cyc = 0;
	rx_delay_byte_cyc_first = 0;
	matched_cyc_first = 0;
	matched_cyc_last = 0;
	wait_sst_entry = 0;
	wait_sst_timeout = 0;
	hit_txcount = 0;
	hit_sst = 0;
	sst_pre_flush = 0;
	pre_fill_cyc = 0;
	wdr_cnt = 0;
	first_cyc_done = false;
	write_len = 0;
	write_len_expected = 0;
	ibi_raised = false;
	hj_raised = false;
	neg_ret_long_payload = 0;
	neg_ret_hj_davalid = 0;
	neg_ret_ibidis = 0;
	aasa_probe_seen = false;
	aasa_snap = 0;
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
	rx_idx = 0;
	/* tx_idx 由下面的 target_prefill_tx 返回值设置 */
	tx_len = 32;
	tx_offset = 0;
	indexed_read_pending = false;
	last_read_len = 32;
	read_req_cnt = 0;
	read_proc_cnt = 0;
	rd_cb_bytes = 0;
	delay_read_seen = false;
	delay_len_seen = false;
	spin_done_actstate = 0;
	spin_done_cyc = 0;
	rx_delay_byte_cyc_first = 0;
	matched_cyc_first = 0;
	matched_cyc_last = 0;
	wait_sst_entry = 0;
	wait_sst_timeout = 0;
	hit_txcount = 0;
	hit_sst = 0;
	sst_pre_flush = 0;
	pre_fill_cyc = 0;
	wdr_cnt = 0;
	first_cyc_done = false;
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
	 * 幂等：写匹配若保留旧 FIFO 内容则不再写，若已清空则从 buf[0] 装起。
	 *
	 * 索引读（CMD_INDEXED_READ）：写匹配时 tx_offset 还没收到（索引在写段
	 * 数据里），此时不预填；等 write_received_cb 收到索引后按 tx_offset 预填。 */
	if (!indexed_read_pending) {
		target_prefill_tx();
	}
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
		case CMD_INDEXED_READ:
			nackx_opcode_pstate = parse_state;	/* 探针：opcode 分发前状态 */
			parse_state = TGT_PARSE_INDEXED_READ;
			indexed_read_pending = true;
			break;
		case CMD_INDEXED_READ_DELAY:
			/* 延时索引读：等 索引+延时 2 字节，收到延时字节后
			 * spin 延时再预填（测 P1→欠载可用窗口） */
			parse_state = TGT_PARSE_INDEXED_READ_DELAY;
			indexed_read_pending = true;
			delay_read_idx = 0;
			delay_read_seen = true;
			break;
		case CMD_INDEXED_READ_DELAY_LEN:
			/* 延时长度索引读：等 索引+延时+长度 3 字节，收到长度字节
			 * （=写段最后字节）后 spin 延时再预填——同 'Y' 的窗口测量，
			 * 但读段长度由写段内容决定（FIFO 深 16，>16 由续发补齐） */
			parse_state = TGT_PARSE_INDEXED_READ_DELAY_LEN;
			indexed_read_pending = true;
			delay_read_idx = 0;
			delay_read_seen = true;
			delay_len_seen = true;
			break;
		case CMD_AASA_PROBE:
			/* SETAASA/SETDASA 探针：controller 已在新地址（静态地址或
			 * SETDASA 分配地址）上寻址到本机，此刻快照 SDYNADDR——
			 * 寄存器里 DADDR/DCAUSE 就是硬件刚才地址切换的证据。
			 * 随后的 magic 字节走 default 分支记入 write_data。 */
			aasa_snap = ((I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6)))->SDYNADDR;
			aasa_probe_seen = true;
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

	case TGT_PARSE_INDEXED_READ:
		/* 索引读：按索引设置 tx_offset 和 tx_len（16 字节），并立即预填。
		 * 注意：写方向的 write_requested_cb 已在收到索引前跳过预填
		 * （indexed_read_pending=true），这里需要先 FLUSHTB 清空 FIFO，
		 * 再按新偏移预填，否则数据错位。 */
		nackx_idx_txoff = tx_offset;	/* 探针：索引到达时残留偏移 */
		tx_offset = val;
		tx_len = 16;
		last_read_len = tx_len;
		indexed_read_pending = false;
		{
			/* 清空 TX FIFO 后按新偏移预填（FLUSHTB 清除写方向预填的旧数据） */
			I3C_TypeDef *base = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));
			base->SDATACTRL |= I3C_SDATACTRL_FLUSHTB_MASK;
			target_prefill_tx();
		}
		parse_state = TGT_PARSE_IDLE;
		break;

	case TGT_PARSE_INDEXED_READ_DELAY:
		/* 延时索引读：第 1 字节 = 索引（暂存），第 2 字节 = 延时档位。
		 * 收到延时字节（= 写段最后字节）后 spin 延时，模拟"根据写段
		 * data 计算回读数据"消耗的时间；再 FLUSHTB + 按新偏移预填。
		 * controller 侧扫描延时档位，欠载临界档位即 P1→T-bit 实测窗口。 */
		if (delay_read_idx == 0) {
			delay_read_idx = 1;	/* 用 1 标记"已收索引"，避免与索引 0 冲突 */
			delay_read_offset = val;
			break;
		}
		delay_read_idx = 0;
		rx_delay_byte_cyc = k_cycle_get_32();	/* delay 字节 ISR 处理时刻（≈ACK 后 0.1-0.3µs） */
		if (!first_cyc_done) {
			first_cyc_done = true;
			rx_delay_byte_cyc_first = rx_delay_byte_cyc;
		}
		if (val == 0) {
			/* step 0（无人工延时）：不 wait_matched——死等 20µs 远超
			 * 实测窗口（6-12µs），且真读匹配从不出现（spin 轮询实证），
			 * 只靠写段残留假匹配命中（SCL 慢时前序 ISR 已清除 → 死等
			 * 满 → 必欠载，6m25 档确定性 first_short=0）。直接记录
			 * 入口状态后 FLUSHTB（等清空完成）+ prefill。 */
			I3C_TypeDef *b = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));
			matched_cyc_first = 0;
			hit_txcount = 0xFFFFU;
			hit_sst = 0;
			wait_sst_entry = b->SSTATUS;
			b->SDATACTRL |= I3C_SDATACTRL_FLUSHTB_MASK;
			/* 无 spin 缓冲：FLUSHTB 后必须等清空完成（TXCOUNT 归零）
			 * 再 prefill，否则写入与清空竞争被抹（0xff 偶发根因） */
			uint32_t fw = 0U;
			while (((b->SDATACTRL & I3C_SDATACTRL_TXCOUNT_MASK) != 0U) &&
			       (fw < 10000U)) {
				fw++;
			}
		} else {
			/* step>0：FLUSHTB 提前到 spin 之前——spin 期间硬件必然完成
			 * 清空，spin 结束直接 prefill 无清空竞态（实测 spin 后
			 * FLUSHTB+立即 prefill 与异步清空竞争，写入被抹 → FIFO
			 * 复位值 0xff，触发率 ~1/3）。 */
			((I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6)))->SDATACTRL |=
				I3C_SDATACTRL_FLUSHTB_MASK;
			spin_ns_last = spin_ns((uint32_t)val * 50U, &matched_cyc_last);
		}
		/* per-step 记账（ISR 内只记 cycle/快照，validate 统一打印）：
		 * 每档 wait 入口状态、命中间隔（delay 字节处理→MATCHED）、命中时
		 * TXCOUNT/SSTATUS——区分"读地址真匹配"与"写段残留匹配"。 */
		int32_t wdr_match = (val == 0) ?
			((matched_cyc_first != 0U) ? (int32_t)(matched_cyc_first - rx_delay_byte_cyc) : -1) :
			((matched_cyc_last != 0U) ? (int32_t)(matched_cyc_last - rx_delay_byte_cyc) : -1);
		if (wdr_cnt < WDR_MAX_STEP) {
			wdr_entry_sst[wdr_cnt] = wait_sst_entry;
			wdr_match_delta[wdr_cnt] = wdr_match;
			wdr_txcount[wdr_cnt] = (wdr_match >= 0) ? hit_txcount : 0xFFFFU;
			wdr_hit_sst[wdr_cnt] = hit_sst;
			wdr_cnt++;
		}
		spin_done_cyc = k_cycle_get_32();
		spin_done_actstate =
			((I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6)))->SSTATUS &
			I3C_SSTATUS_ACTSTATE_MASK;
		{
			I3C_TypeDef *base = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));
			sst_pre_flush = base->SSTATUS;	/* spin 结束时刻总线状态（FLUSHTB 已提前） */
			tx_offset = delay_read_offset;
			tx_len = 16;
			last_read_len = tx_len;
			indexed_read_pending = false;
			/* FLUSHTB 已在 spin 前执行（清空完成），此处直接 prefill */
			target_prefill_tx();
			pre_fill_cyc = k_cycle_get_32();
		}
		parse_state = TGT_PARSE_IDLE;
		break;

	case TGT_PARSE_INDEXED_READ_DELAY_LEN:
		/* 延时长度索引读：与 'Y' 同构（idx → delay → len 三字节），
		 * len 字节（=写段最后字节）收完即 spin。FLUSHTB 前置到 spin 前
		 * （WDR16 修复：避免异步清空与 prefill 竞态）；prefill 只装
		 * FIFO 容得下的前缀（≤16），>16 由 TXSEND 中断 read_processed_cb
		 * 续发——跨 FIFO 深度边界的回复能力即本测试核心。 */
		if (delay_read_idx < 2) {
			if (delay_read_idx == 0) {
				delay_read_offset = val;	/* 首字节 = 索引 */
			} else {
				delay_len_delay_step = val;	/* 第 2 字节 = 延时档位（暂存） */
			}
			delay_read_idx++;
			break;
		}
		delay_read_idx = 0;
		rx_delay_byte_cyc = k_cycle_get_32();	/* len 字节（写段最后字节）ISR 处理时刻 */
		delay_len_value = val;			/* 第 3 字节 = 读长度 */
		val = delay_len_delay_step;		/* spin 用延时档位（len 字节触发） */
		if (val == 0) {
			/* step 0（无人工延时）：同 'Y' 修复——不 wait_matched
			 * （死等 20µs 远超窗口必欠载），直接 FLUSHTB（等清空
			 * 完成）再 prefill。 */
			I3C_TypeDef *b = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));
			matched_cyc_first = 0;
			hit_txcount = 0xFFFFU;
			hit_sst = 0;
			wait_sst_entry = b->SSTATUS;
			b->SDATACTRL |= I3C_SDATACTRL_FLUSHTB_MASK;
			uint32_t fw = 0U;
			while (((b->SDATACTRL & I3C_SDATACTRL_TXCOUNT_MASK) != 0U) &&
			       (fw < 10000U)) {
				fw++;
			}
		} else {
			((I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6)))->SDATACTRL |=
				I3C_SDATACTRL_FLUSHTB_MASK;
			spin_ns_last = spin_ns((uint32_t)val * 50U, &matched_cyc_last);
		}
		spin_done_cyc = k_cycle_get_32();
		spin_done_actstate =
			((I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6)))->SSTATUS &
			I3C_SSTATUS_ACTSTATE_MASK;
		{
			I3C_TypeDef *base = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));
			sst_pre_flush = base->SSTATUS;	/* spin 结束时刻总线状态 */
			tx_offset = delay_read_offset;
			tx_len = (delay_len_value == 0) ? 256 : delay_len_value;
			last_read_len = tx_len;
			indexed_read_pending = false;
			/* FLUSHTB 已在 spin 前执行（清空完成），此处直接 prefill：
			 * 只装 FIFO 容得下的前缀（≤16），余下由 read_processed_cb
			 * 续发（tx_idx 已在 prefill 内按实际装入数更新） */
			target_prefill_tx();
			pre_fill_cyc = k_cycle_get_32();
		}
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
	nackx_read_txoff = tx_offset;	/* 探针：读匹配时偏移 */
	nackx_read_txidx = tx_idx;	/* 探针：读匹配时索引 */

	if (tx_idx < tx_len) {
		/* tx_idx 是相对索引（已预填/已发送字节数），实际数据在
		 * tgt_tx_buf[tx_offset + tx_idx]（索引读时 tx_offset 非 0） */
		*val = tgt_tx_buf[tx_offset + tx_idx++];
		rd_cb_bytes++;	/* 回调供出的字节（>FIFO 预填量的续发部分） */
		/* 返回 1 = 这是最后字节，驱动为其标 END0 */
		if (tx_idx >= tx_len) {
			/* 本笔读完成：复位 tx_offset 和 indexed_read_pending（索引读只影响
			 * 本笔），避免污染后续普通读 */
			tx_offset = 0;
			indexed_read_pending = false;
			return 1;
		}
		return 0;
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
		/* tx_idx 是相对索引（已预填/已发送字节数），实际数据在
		 * tgt_tx_buf[tx_offset + tx_idx]（索引读时 tx_offset 非 0） */
		*val = tgt_tx_buf[tx_offset + tx_idx++];
		rd_cb_bytes++;	/* 回调供出的字节（>FIFO 预填量的续发部分） */
		if (tx_idx >= tx_len) {
			/* 本笔读完成：复位 tx_offset 和 indexed_read_pending（索引读只影响
			 * 本笔），避免污染后续普通读 */
			tx_offset = 0;
			indexed_read_pending = false;
			return 1;
		}
		return 0;
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
	/* 索引读的 tx_offset 只影响本笔读：write+restart+read 场景下 stop_cb
	 * 在写传输结束时触发，此时 tx_offset 还没被 write_requested_cb
	 * （restart 后的读匹配）使用，不能复位；write+stop+read 场景下
	 * stop_cb 在写传输结束时触发，读传输是独立的 START，此时应复位。
	 * 统一在 read_requested_cb（读匹配时）复位（读完成后），见该函数。 */
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
			/* 按项运行同名 ztest 套件：35 项各注册一个单测试套件
			 * （套件名 == 测试项名 == i3c_test_names[] 字符串），
			 * 运行期按名称查表（z_ztest_run_test_suite → strcmp）。
			 * target 无 zt_run_all（test_main 不返回，§4.1），
			 * 逐项 ztest 输出 = 判定数据源，与 'E' 到达一一对应，
			 * 保证每次校验的数据新鲜度/时序。 */
			if (evt.test_id < I3C_TEST_COUNT) {
				z_ztest_run_test_suite(i3c_test_names[evt.test_id],
						       false, 1, 1, NULL);
			}
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

/* ========== Target ztest suites ==========
 *
 * 35 个单测试套件：套件名 == 测试项名（i3c_test_names[] 的字符串值，
 * strcmp 查表依赖，必须逐字符一致），dispatcher 按 'E' 携带的 test_id
 * 运行期调 z_ztest_run_test_suite() 运行对应套件。
 * 无 setup/before/after/teardown：target 校验全部基于 dispatcher 事件
 * 驱动的状态（validate_* 函数），ztest 只承担逐项输出与断言。
 */

ZTEST_SUITE(daa_discovery, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_8, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_100, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(read_32_direct, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_read_1, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_read_15, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_read_16, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_read_17, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_read_32, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(ibi, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(hot_join, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(ccc_basic, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_stop_read_1, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_stop_read_15, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_stop_read_16, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_stop_read_17, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_stop_read_32, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(read_underrun_8_16, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(read_underrun_16_32, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(nack_bad_addr, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(back_to_back_8x8, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(big_256, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(ccc_rapid_8, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(read_master_abort, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(ibi_neg_paths, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(api_neg_paths, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(multi_msg_4, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_indexed_read_16, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_delay_read_16, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(write_delay_read_len, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(read_underrun_stress, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(multi_msg_short_read, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(short_read_then_nack, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(setaasa, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(setdasa, NULL, NULL, NULL, NULL, NULL);

static void validate_daa_discovery(void)
{
	/* target 侧独立证据（原 `addressed` 恒真断言已删，见 KNOWN_ISSUES 类
	 * 问题"恒真空壳"）：DAA 之后本机硬件的动态地址寄存器必须已生效。
	 * CTRL 侧断言的是 controller 软件视角（target_desc->dynamic_addr），
	 * 此处读本机 SDYNADDR 是同一事实的**另一侧**观测：
	 *   - 驱动正确时必过：ENTDAA 成功 → 硬件写 DADDR=0x32、DAVALID=1；
	 *   - 典型失效时必不过：ENTDAA 未发/地址分配失败 → DAVALID=0；
	 *     分配到错误地址 → DADDR≠0x32。
	 * （DADDR 字段为 7 位地址，编码依 NXP I3C slave programmers model，
	 *   与 validate_aasa_common 的 DADDR 比对同一语义。） */
	uint32_t dyn = ((I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6)))->SDYNADDR;
	uint8_t daddr = (uint8_t)((dyn & I3C_SDYNADDR_DADDR_MASK) >>
				  I3C_SDYNADDR_DADDR_POS);

	TGT_EXPECT((dyn & I3C_SDYNADDR_DAVALID_MASK) != 0U,
		   "SDYNADDR.DAVALID=0 (target got no dynamic address from DAA)");
	TGT_EXPECT(daddr == I3C_TARGET_ADDR,
		   "SDYNADDR.DADDR 0x%02x != 0x%02x", daddr, I3C_TARGET_ADDR);
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
	/* 协议点校验（原先裸依赖 reset_test_state 的默认值 32 ⇒ 恒真）。
	 * controller 现已显式下发 CMD_SET_READ_LEN(32)。
	 * 读数据正确性由 CTRL 侧 pattern 校验；target 侧对"master 实际读了
	 * 多少字节"不可观测（len ≤ FIFO 深度 16 时全部由预填供货，续发回调
	 * 不被调用，见 i3c_linkedsemi.c txcount!=0 分支）。 */
	TGT_EXPECT(last_read_len == 32, "last_read_len %u != 32", last_read_len);
}

static void validate_write_read_1(void)
{
	/* 写方向：可观测的真实数据通路校验（原先只回显长度命令字节）。
	 * 驱动正确时必过：写段占位字节 0xA5 被 write_received_cb 原样收到；
	 * 典型失效时必不过：写段丢字节/多字节/内容错 → write_len/write_data 不符。 */
	TGT_EXPECT(write_len == 1, "write_len %zu != 1", (size_t)write_len);
	if (write_len >= 1) {
		TGT_EXPECT(write_data[0] == CMD_DUMMY_BYTE,
			   "write_data[0] 0x%02x != 0x%02x",
			   (unsigned)write_data[0], CMD_DUMMY_BYTE);
	}
	TGT_EXPECT(last_read_len == 1, "last_read_len %u != 1", last_read_len);
}

static void validate_write_read_15(void)
{
	/* 同 write_read_1：写方向真实数据校验（关键长度 15）。 */
	TGT_EXPECT(write_len == 1, "write_len %zu != 1", (size_t)write_len);
	if (write_len >= 1) {
		TGT_EXPECT(write_data[0] == CMD_DUMMY_BYTE,
			   "write_data[0] 0x%02x != 0x%02x",
			   (unsigned)write_data[0], CMD_DUMMY_BYTE);
	}
	TGT_EXPECT(last_read_len == 15, "last_read_len %u != 15", last_read_len);
}

static void validate_write_read_16(void)
{
	/* 同 write_read_1：写方向真实数据校验（关键长度 16 = FIFO 深度）。 */
	TGT_EXPECT(write_len == 1, "write_len %zu != 1", (size_t)write_len);
	if (write_len >= 1) {
		TGT_EXPECT(write_data[0] == CMD_DUMMY_BYTE,
			   "write_data[0] 0x%02x != 0x%02x",
			   (unsigned)write_data[0], CMD_DUMMY_BYTE);
	}
	TGT_EXPECT(last_read_len == 16, "last_read_len %u != 16", last_read_len);
}

static void validate_write_indexed_read_16(void)
{
	/* 索引读：tx_len 应为 16（write_received_cb 设置），
	 * tx_offset 已被 stop_cb 复位为 0（传输结束），只验证长度 */
	TGT_EXPECT(last_read_len == 16, "last_read_len %u != 16", last_read_len);
}

static void validate_write_delay_read_16(void)
{
	/* 延时索引读：协议点确认——收到过 'Y'（delay_read_seen，stop_cb
	 * 不清此标志）、读长度协议正确。数据能否在 T-bit 前入 FIFO
	 * （欠载与否）由 controller 侧扫描判定，target 侧不参与。 */
	TGT_EXPECT(delay_read_seen, "no CMD_INDEXED_READ_DELAY received");
	TGT_EXPECT(last_read_len == 16, "last_read_len %u != 16", last_read_len);
	/* nack_recovery 诊断（临时探针）：'X' 分发前 parse_state（非 IDLE 即
	 * 卡状态）、索引到达时残留 tx_offset（0x10=WDR 残留未复位）、
	 * 读匹配时 tx_offset/tx_idx（0x10/0 = 偏移未复位直接供残留数据） */
	printk("TGT_DBG7: nackx opcode_pstate=0x%x idx_txoff=%u read_txoff=%u read_txidx=%u\n",
	       nackx_opcode_pstate, nackx_idx_txoff, nackx_read_txoff,
	       nackx_read_txidx);
	/* spin 实测核对 + 总线阶段/新 match 时序（最后有效一笔）：
	 * - spin_done_actstate：spin 完成时刻总线阶段（0=idle 1=write 2=read）
	 * - match2delay：delay 字节收完 → MATCHED 置位（"新 match"）的物理
	 *   间隔；0 = spin 期间未匹配（该笔 master 间隙 > spin，欠载档） */
	printk("TGT_DBG: spin=%ucyc(%uns) actstate=0x%x match2delay=%dus\n",
	       spin_ns_last,
	       spin_ns_last * 1000000000ULL /
		       (uint64_t)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC,
	       spin_done_actstate,
	       (int)((matched_cyc_last - rx_delay_byte_cyc) /
		     (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000ULL)));
	/* 首笔（step 0，无人工延时）：delay 字节收完 → MATCHED 物理间隔
	 * = master 写段结束到 restart+地址匹配的实际时间（直接对照波形）。
	 * wait_sst_entry：wait 入口（delay 字节 ISR 处理时刻）SSTATUS 快照——
	 * 0x200（MATCHED）= 入口时 restart+地址 已匹配（gap≈0，ISR 处理 delay
	 * 字节时匹配已完成）；0 = 未匹配（wait 等新匹配）。 */
	printk("TGT_DBG2: step0 match2delay=%dus (no spin, master gap+addr)\n",
	       (int)((matched_cyc_first - rx_delay_byte_cyc_first) /
		     (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000ULL)));
	printk("TGT_DBG3: step0 sst_entry=0x%x (%s) wait_timeout_sst=0x%x match_first=%u(0=timeout) rx_first=%u\n",
	       wait_sst_entry,
	       (wait_sst_entry & I3C_SSTATUS_MATCHED_MASK) ? "MATCHED-already-set" : "no-match-yet",
	       wait_sst_timeout,
	       matched_cyc_first, rx_delay_byte_cyc_first);
	printk("TGT_DBG4: last sst_entry=0x%x match_last=%u(0=spin-didnt-see) rx_delay_cyc=%u\n",
	       wait_sst_entry, matched_cyc_last, rx_delay_byte_cyc);
	printk("TGT_DBG5: hit_txcount=%u hit_sst=0x%x (%s)\n",
	       hit_txcount, hit_sst,
	       (hit_txcount == 0xFFFFU) ? "spin-didnt-see" : "16=常备未发,<16=数据已发出");
	/* per-step 时间线：每档 wait 入口 SSTATUS、命中间隔（delay→MATCHED
	 * cycles，-1=未命中）、命中时 TXCOUNT。观察"假匹配"（写段残留）与
	 * 真读匹配的分界，回答死等预算与 match 时机的关系。 */
	printk("WDR_TIMELINE:\n");
	for (uint16_t i = 0; i < wdr_cnt && i < WDR_MAX_STEP; i++) {
		printk("W%03u:%s e=0x%x d=%d tx=%u hs=0x%x\n", i,
		       (wdr_match_delta[i] >= 0) ? "hit" : "miss",
		       wdr_entry_sst[i], wdr_match_delta[i], wdr_txcount[i],
		       wdr_hit_sst[i]);
	}
	printk("TGT_DBG6: pre_flush=0x%x prefill_cyc=%u (spin后FLUSHTB前状态)\n",
	       sst_pre_flush, pre_fill_cyc);
	/* CPU 频率 probe（cycle→ns 基准校准）：printk 逐字节轮询 @115200，
	 * 77 字符 ≈ 77×86.8µs = 6.68ms 外部基准。dt_cycles / 6.68ms =
	 * cycle counter 实际频率（CONFIG 声明 600MHz，若实际不同则所有
	 * cycle→ns 换算虚高/虚低）。 */
	uint32_t _t0 = k_cycle_get_32();
	printk("CPUFREQ_PROBE:0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\n");
	uint32_t _dt = k_cycle_get_32() - _t0;
	printk("CPUFREQ: cyc=%u -> %u MHz (declared 600)\n", _dt,
	       (uint32_t)((uint64_t)_dt * 1000ULL / 6680ULL));
}

static void validate_write_delay_read_len(void)
{
	/* 延时长度索引读：协议点确认——收到过 'Z'（delay_len_seen，stop_cb
	 * 不清此标志）、读长度来自写段内容（≥17 = 跨 FIFO 深度 16 边界，
	 * 由续发路径补齐；非默认 16）。数据正确性由 controller 侧扫描判定
	 * （pattern 校验读回内容），target 侧只确认协议点与长度记账。 */
	TGT_EXPECT(delay_len_seen, "no CMD_INDEXED_READ_DELAY_LEN received");
	TGT_EXPECT(last_read_len >= 17 && last_read_len <= 256,
		   "last_read_len %u not from write segment", last_read_len);
	printk("TGT_DBG: wdrlen last_len=%u spin=%ucyc actstate=0x%x\n",
	       last_read_len, spin_ns_last, spin_done_actstate);
}

static void validate_write_read_17(void)
{
	/* 写方向真实数据校验 + 读方向续发证据（关键长度 17 > FIFO 深度 16）。 */
	TGT_EXPECT(write_len == 1, "write_len %zu != 1", (size_t)write_len);
	if (write_len >= 1) {
		TGT_EXPECT(write_data[0] == CMD_DUMMY_BYTE,
			   "write_data[0] 0x%02x != 0x%02x",
			   (unsigned)write_data[0], CMD_DUMMY_BYTE);
	}
	TGT_EXPECT(last_read_len == 17, "last_read_len %u != 17", last_read_len);
	/* 读方向（关键长度 17）：17 = 16(FIFO 预填) + 1(续发)。第 17 字节
	 * 只能经 read_processed_cb 供出，是 target 侧唯一可静态确证的"实际
	 * 供出字节数"证据（i3c_linkedsemi.c:2857 预填受 FIFO 满自停）。
	 * 驱动正确时必过（续发回调至少 1 次）；
	 * 典型失效时必不过（续发路径断 → rd_cb_bytes==0，CTRL 侧表现为欠载）。 */
	TGT_EXPECT(rd_cb_bytes >= 1U,
		   "read continuation served 0 bytes (len=17 needs 1 beyond FIFO)");
}

static void validate_write_read_32(void)
{
	TGT_EXPECT(last_read_len == 32, "last_read_len %u != 32", last_read_len);
}

static void validate_write_stop_read_1(void)
{
	/* 写方向真实数据校验（关键长度 1）：STOP 分隔的 write+read 变体，
	 * 写段同样带 1 字节占位 0xA5。 */
	TGT_EXPECT(write_len == 1, "write_len %zu != 1", (size_t)write_len);
	if (write_len >= 1) {
		TGT_EXPECT(write_data[0] == CMD_DUMMY_BYTE,
			   "write_data[0] 0x%02x != 0x%02x",
			   (unsigned)write_data[0], CMD_DUMMY_BYTE);
	}
	TGT_EXPECT(last_read_len == 1, "last_read_len %u != 1", last_read_len);
}

static void validate_write_stop_read_15(void)
{
	/* 同 write_stop_read_1：写方向真实数据校验（关键长度 15）。 */
	TGT_EXPECT(write_len == 1, "write_len %zu != 1", (size_t)write_len);
	if (write_len >= 1) {
		TGT_EXPECT(write_data[0] == CMD_DUMMY_BYTE,
			   "write_data[0] 0x%02x != 0x%02x",
			   (unsigned)write_data[0], CMD_DUMMY_BYTE);
	}
	TGT_EXPECT(last_read_len == 15, "last_read_len %u != 15", last_read_len);
}

static void validate_write_stop_read_16(void)
{
	/* 同 write_stop_read_1：写方向真实数据校验（关键长度 16 = FIFO 深度）。 */
	TGT_EXPECT(write_len == 1, "write_len %zu != 1", (size_t)write_len);
	if (write_len >= 1) {
		TGT_EXPECT(write_data[0] == CMD_DUMMY_BYTE,
			   "write_data[0] 0x%02x != 0x%02x",
			   (unsigned)write_data[0], CMD_DUMMY_BYTE);
	}
	TGT_EXPECT(last_read_len == 16, "last_read_len %u != 16", last_read_len);
}

static void validate_write_stop_read_17(void)
{
	/* 写方向真实数据校验 + 读方向续发证据（关键长度 17 > FIFO 深度 16）。 */
	TGT_EXPECT(write_len == 1, "write_len %zu != 1", (size_t)write_len);
	if (write_len >= 1) {
		TGT_EXPECT(write_data[0] == CMD_DUMMY_BYTE,
			   "write_data[0] 0x%02x != 0x%02x",
			   (unsigned)write_data[0], CMD_DUMMY_BYTE);
	}
	TGT_EXPECT(last_read_len == 17, "last_read_len %u != 17", last_read_len);
	/* 读方向：STOP 分隔变体同样需要续发第 17 字节（预填受 FIFO 16 自停）。 */
	TGT_EXPECT(rd_cb_bytes >= 1U,
		   "read continuation served 0 bytes (len=17 needs 1 beyond FIFO)");
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
	/* CCC（GETPID/GETBCR/ENEC/DISEC）由 target 硬件自动应答，软件无回调，
	 * 故无"CCC 处理结果"可直接观测。可判别的 target 侧不变量：CCC 帧不得
	 * 被误当作私有写投递到 write_received_cb ——
	 *   - 驱动正确时必过：CCC 走硬件路径，write_len 保持 TEST_START 清零值 0；
	 *   - 典型失效时必不过：驱动把 CCC 帧误分发/误解析为私有写 →
	 *     write_data/write_len 被污染（write_len > 0）。 */
	TGT_EXPECT(write_len == 0U,
		   "CCC frames leaked into private-write buffer: write_len=%zu",
		   (size_t)write_len);
}

/* ========== 主机主动临界测试的 target 侧校验 ========== */

static void validate_read_underrun_8_16(void)
{
	/* 主机读穿预填（8<16）：target 回调供完 8 字节后返回 1（END），
	 * 由硬件 T-bit 提前终止。本侧只需确认读长度协议点正确到达。 */
	TGT_EXPECT(last_read_len == 8, "last_read_len %u != 8", last_read_len);
}

static void validate_read_underrun_16_32(void)
{
	/* 写方向真实数据校验（关键长度 16）：READ_UNDERRUN 宏每项发**两次**
	 * write_read（先欠载读 32B，再恢复读 16B），每次写段 1 字节占位 0xA5，
	 * 故 target 累计收到 2 字节（write_len 只在 TEST_START 清零）。
	 * 驱动正确时必过；典型失效时必不过（写段丢字节/内容错）。 */
	TGT_EXPECT(write_len == 2, "write_len %zu != 2", (size_t)write_len);
	for (size_t i = 0; i < write_len && i < 2; i++) {
		TGT_EXPECT(write_data[i] == CMD_DUMMY_BYTE,
			   "write_data[%zu] 0x%02x != 0x%02x",
			   i, (unsigned)write_data[i], CMD_DUMMY_BYTE);
	}
	TGT_EXPECT(last_read_len == 16, "last_read_len %u != 16", last_read_len);
}

static void validate_nack_bad_addr(void)
{
	/* 错误地址在硬件层 NACK，不经任何软件回调。可判别的 target 侧不变量：
	 * 发往未挂载地址（0x55）的写不得被本机接收 ——
	 *   - 驱动/硬件正确时必过：非本机地址被地址匹配逻辑丢弃，write_len=0；
	 *   - 典型失效时必不过：地址过滤失效 → 外部地址的写被投递给应用，
	 *     write_len > 0（这是硬件/驱动地址匹配缺陷的强信号）。 */
	TGT_EXPECT(write_len == 0U,
		   "write to foreign address 0x55 was received: write_len=%zu",
		   (size_t)write_len);
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
	/* 读方向（关键长度 256 = 16(FIFO) + 240(续发)）：续发路径必须供出
	 * 全部 240 字节 ——
	 *   - 驱动正确时必过：read_processed_cb 逐字节补满 240；
	 *   - 典型失效时必不过：续发断流 → rd_cb_bytes < 240（CTRL 侧同一
	 *     故障表现为读到 0xff/欠载）。这是该长度下 target 侧的独立证据。 */
	TGT_EXPECT(rd_cb_bytes >= 240U,
		   "read continuation served %u bytes < 240 (16 FIFO + 240 expected)",
		   (unsigned)rd_cb_bytes);
}

static void validate_ccc_rapid_8(void)
{
	/* CCC 由 target 硬件直接应答，软件不参与。可判别的 target 侧不变量：
	 * 连发 8 次 GETPID 不得污染私有写缓冲（同 ccc_basic 判据，针对
	 * "重复 CCC 帧"路径）——
	 *   - 驱动正确时必过：8 帧全部走硬件，write_len 保持 0；
	 *   - 典型失效时必不过：重复 CCC 帧被误当作私有写投递 → write_len > 0。 */
	TGT_EXPECT(write_len == 0U,
		   "rapid CCC leaked into private-write buffer: write_len=%zu",
		   (size_t)write_len);
}

static void validate_read_master_abort(void)
{
	/* 主机截断读：本侧确认读长度协议点（controller 现已显式下发
	 * CMD_SET_READ_LEN(32)，此前裸依赖 reset_test_state 默认值 ⇒ 恒真）。
	 * 真正的截断恢复校验在 controller 侧（post-abort 读回 0x00.. 起点）。 */
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
	/* i3c_transfer 的四个负路径（num_msgs=0 / dynamic_addr=0 / hdr_mode=1 /
	 * msgs=NULL）都应在驱动入口被拦截、**完全不上总线**。可判别的 target
	 * 侧不变量：整项期间本机不得收到任何私有写数据 ——
	 *   - 驱动正确时必过：负路径提前返回，write_len=0（本项末尾的恢复读
	 *     是读方向，不写 write_data）；
	 *   - 典型失效时必不过：入口检查失效 → 非法参数被真的发到总线，
	 *     write_len > 0。这是"负路径只验返回值"之外唯一的跨核独立证据。 */
	TGT_EXPECT(write_len == 0U,
		   "negative-path API call put data on the bus: write_len=%zu",
		   (size_t)write_len);
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
	/* 协议点：controller 现已显式下发 CMD_SET_READ_LEN(32)（此前裸依赖
	 * reset_test_state 默认值 ⇒ 恒真）。写段两个占位字节已在上方校验。 */
}

static void validate_read_underrun_stress(void)
{
	/* 8 轮短读（read_len=8）+ 1 轮恢复读（read_len=16）。
	 * last_read_len 记录最后一次 CMD_SET_READ_LEN 的值。 */
	TGT_EXPECT(last_read_len == 16, "last_read_len %u != 16 (recovery)",
		   last_read_len);
}

static void validate_multi_msg_short_read(void)
{
	/* 多消息帧2个写段 + 恢复读1个写段 = 3 个占位字节 0xA5 */
	TGT_EXPECT(write_len == 3, "write_len %zu != 3", (size_t)write_len);
	for (size_t i = 0; i < write_len && i < 3; i++) {
		TGT_EXPECT(write_data[i] == CMD_DUMMY_BYTE,
			   "write_data[%zu] 0x%02x != 0x%02x",
			   i, write_data[i], CMD_DUMMY_BYTE);
	}
	/* read_len 由最后一次 ctrl_set_read_len(8) 设定 */
	TGT_EXPECT(last_read_len == 8, "last_read_len %u != 8", last_read_len);
}

static void validate_short_read_then_nack(void)
{
	/* 每轮短读后 controller 设 read_len=8 做恢复读 */
	TGT_EXPECT(last_read_len == 8, "last_read_len %u != 8", last_read_len);
}

/* SETAASA/SETDASA 公共校验。
 * 证据链（i3c_peripheral_programmers_model.pdf DYNADDR @0x64）：
 * 1. 探针写在新地址上被收到（aasa_probe_seen）——地址切换后总线可达；
 * 2. 探针时刻快照的 SDYNADDR：DAVALID=1、DADDR=期望地址、DCAUSE=2
 *    （SETDASA/SETAASA/SETNEWDA 置位路径）；
 * 3. 探针 magic 字节内容正确（数据通路正常）。
 * 注：RSTDAA/SETAASA/SETDASA/SETNEWDA 全部由硬件自动处理（SSTATUS.CCC
 * 仅对硬件不处理的 CCC 中断），无需驱动介入。 */
static void validate_aasa_common(uint8_t expect_addr)
{
	static const uint8_t magic[] = {
		AASA_PROBE_MAGIC_0, AASA_PROBE_MAGIC_1,
		AASA_PROBE_MAGIC_2, AASA_PROBE_MAGIC_3,
	};
	I3C_TypeDef *base = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));

	/* 静态地址确实已写进硬件（SETDASA/SETAASA 匹配的前提） */
	uint32_t saddr = (base->SCONFIG & I3C_SCONFIG_SADDR_MASK) >> I3C_SCONFIG_SADDR_POS;
	TGT_EXPECT(saddr == I3C_TARGET_STATIC_ADDR,
		   "SCONFIG.SADDR 0x%02x != 0x%02x", saddr, I3C_TARGET_STATIC_ADDR);

	TGT_EXPECT(aasa_probe_seen, "AASA probe not received");
	if (!aasa_probe_seen) {
		return;
	}

	uint32_t snap = aasa_snap;
	uint8_t daddr = (snap & I3C_SDYNADDR_DADDR_MASK) >> I3C_SDYNADDR_DADDR_POS;
	uint8_t dcause = (snap & I3C_SDYNADDR_DCAUSE_MASK) >> I3C_SDYNADDR_DCAUSE_POS;

	TGT_EXPECT(snap & I3C_SDYNADDR_DAVALID_MASK, "SDYNADDR.DAVALID=0");
	TGT_EXPECT(daddr == expect_addr,
		   "SDYNADDR.DADDR 0x%02x != 0x%02x", daddr, expect_addr);
	TGT_EXPECT(dcause == 2, "SDYNADDR.DCAUSE %u != 2", dcause);

	TGT_EXPECT(write_len >= sizeof(magic),
		   "probe magic write_len %zu < %zu", write_len, sizeof(magic));
	for (size_t i = 0; i < sizeof(magic) && i < write_len; i++) {
		TGT_EXPECT(write_data[i] == magic[i],
			   "probe magic[%zu] 0x%02x != 0x%02x",
			   i, write_data[i], magic[i]);
	}
}

static void validate_setaasa(void)
{
	/* SETAASA 后动态地址 = 静态地址 */
	validate_aasa_common(I3C_TARGET_STATIC_ADDR);
}

static void validate_setdasa(void)
{
	/* SETDASA 后动态地址 = controller 指定的 0x45 */
	validate_aasa_common(I3C_SETDASA_NEW_ADDR);
}

/* 每项一个命名 ZTEST：套件名 == 测试项名，dispatcher 按 test_id 运行对应
 * 套件；校验在 validate_<item>() 内按 dispatcher 事件驱动的状态进行。 */
#define TGT_ZTEST(item) \
	ZTEST(item, item) \
	{ \
		validate_##item(); \
		tgt_finish(); \
	}

TGT_ZTEST(daa_discovery)
TGT_ZTEST(write_8)
TGT_ZTEST(write_100)
TGT_ZTEST(read_32_direct)
TGT_ZTEST(write_read_1)
TGT_ZTEST(write_read_15)
TGT_ZTEST(write_read_16)
TGT_ZTEST(write_read_17)
TGT_ZTEST(write_read_32)
TGT_ZTEST(ibi)
TGT_ZTEST(hot_join)
TGT_ZTEST(ccc_basic)
TGT_ZTEST(write_stop_read_1)
TGT_ZTEST(write_stop_read_15)
TGT_ZTEST(write_stop_read_16)
TGT_ZTEST(write_stop_read_17)
TGT_ZTEST(write_stop_read_32)
TGT_ZTEST(read_underrun_8_16)
TGT_ZTEST(read_underrun_16_32)
TGT_ZTEST(nack_bad_addr)
TGT_ZTEST(back_to_back_8x8)
TGT_ZTEST(big_256)
TGT_ZTEST(ccc_rapid_8)
TGT_ZTEST(read_master_abort)
TGT_ZTEST(ibi_neg_paths)
TGT_ZTEST(api_neg_paths)
TGT_ZTEST(multi_msg_4)
TGT_ZTEST(write_indexed_read_16)
TGT_ZTEST(write_delay_read_16)
TGT_ZTEST(write_delay_read_len)
TGT_ZTEST(read_underrun_stress)
TGT_ZTEST(multi_msg_short_read)
TGT_ZTEST(short_read_then_nack)
TGT_ZTEST(setaasa)
TGT_ZTEST(setdasa)
