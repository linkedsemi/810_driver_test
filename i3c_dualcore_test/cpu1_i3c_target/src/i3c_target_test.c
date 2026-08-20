/*
 * Copyright (c) 2025 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i3c_target_test.h"
#include "i3c_dualcore_test_protocol.h"

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

/* 驱动里的调试计数器（ISR 内不打印，只记账） */
extern volatile uint32_t rxp_stuck_cnt;
extern volatile uint32_t i3c_isr_enter_cnt;
extern volatile uint32_t i3c_event_isr_t;
extern volatile uint32_t i3c_serrwarn_last, i3c_serrwarn_cnt, i3c_serrwarn_t;
/* 分发层监视（soc_isr_stacking_e906.c，仅 cpu1） */
extern volatile uint32_t e906_dispatch_cnt, e906_mpie0_cnt, e906_mpie0_mstatus;
extern volatile uint32_t e906_exc_cnt;
/* 中断来源识别：驱动 ISR 入口 (时刻,SSTATUS) 快照 + 分发层 (时刻,IRQ号) 记录 */
extern volatile uint32_t i3c_isr_log_t[], i3c_isr_log_st[], i3c_isr_log_n;
extern volatile uint32_t e906_irq_log_t[], e906_irq_log_n;
extern volatile uint32_t e906_irq_log_mepc[];
extern volatile uint16_t e906_irq_log_id[];
/* MPIE 诊断：最外层 unstack 返回时 mcause.MPIE=0 的现场快照 */
extern volatile uint32_t e906_mpie0_ret_cnt, e906_mpie0_ret_mcause;
extern volatile uint32_t e906_mpie0_ret_mepc, e906_mpie0_ret_t;
/* log_core.c 调试打点：LOG_INF 内部各阶段耗时 */
extern volatile uint32_t log_dbg_alloc_us, log_dbg_commit_us;
extern volatile uint32_t log_dbg_postfin_us, log_dbg_tmr_us, log_dbg_cnt;
extern volatile uint32_t log_dbg_atomic_us, log_dbg_mpsc_us, log_dbg_ts_us;
extern volatile uint32_t log_dbg_ld1_us, log_dbg_ld2_us, log_dbg_ld3_us;
extern volatile uint32_t log_dbg_gap_us[8];
extern volatile uint32_t log_dbg_mtime_us, log_dbg_mcycle_us;
extern volatile uint32_t log_dbg_freeze;
/* timeout.c 调试：z_add_timeout 出入口打点（带真 barrier，时序可信） */
extern volatile uint32_t to_dbg_arm, to_dbg_us, to_dbg_dticks,
			 to_dbg_elapsed, to_dbg_first;

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

static volatile uint8_t rx_buf[128];
static volatile size_t rx_idx;
static volatile uint16_t tx_idx;
static volatile uint16_t tx_len;
static volatile uint16_t last_read_len;
static volatile int read_req_cnt;
static volatile int read_proc_cnt;

static volatile uint8_t write_data[128];
static volatile size_t write_len;
static volatile size_t write_len_expected;

static volatile bool ibi_raised;
static volatile bool hj_raised;

static char tgt_fail_reason[256];
static bool tgt_failed;

/* 每项测试的结果记账：-1 未跑 / 0 PASS / 1 FAIL，用于结尾汇总表 */
static int8_t tgt_results[I3C_TEST_COUNT];
static char tgt_result_reason[I3C_TEST_COUNT][96];

/* Driver target TX FIFO depth (matches ls_i3c_data.fifo_info.TargetTxFifoSize). */
#define TGT_TX_FIFO_SIZE 16

/*
 * Pre-fill the hardware TX FIFO with the bytes to be read.
 *
 * 预填模型（配合驱动"仅 IDLE 可预填"语义）：
 * - 只在 IDLE 时刻调用本函数：stop_cb（每笔传输结束）与初始化。
 *   写传输进行中驱动会拒绝预填（返回 0），因为 FLUSHTB 在 WR 态被硬件忽略，
 *   强行写只会把 FIFO 内容/记账搞乱。
 * - FIFO 常备 tgt_tx_buf 前 16 字节；读长度变化（CMD_SET_READ_LEN）
 *   只改 tx_len，不动 FIFO。读 16 字节以内直接由 FIFO 供应；
 *   超过 16 字节由 TXSEND 中断经 read_processed_cb 从 tx_idx 续发。
 * - 驱动拒绝预填（返回 0）时 FIFO 内容与 tx_idx 均未变，故只在返回 >0
 *   时更新 tx_idx。
 *
 * 注意：本函数不更新 last_read_len。last_read_len 只在“设置读长度”的
 * 协议点（CMD_SET_READ_LEN / reset_test_state 默认值）更新，
 * 避免 slave_stop_cb 的恢复性预填把测试要校验的长度覆盖掉。
 */
/* ISR 内不打印（时序敏感）：回调只记账，dispatcher 线程统一输出 */
static volatile uint32_t dbg_wr_req_cnt;
static volatile uint32_t dbg_stop_cnt;
static volatile int dbg_prefill_wrote;
static volatile uint32_t dbg_prefill_txcount;
static volatile uint32_t dbg_a5_sstatus, dbg_a5_sintset, dbg_a5_sintmasked;
/* 回调时刻（k_cycle_get_32 快照，换算 us 后打印，用于和总线帧对齐时间轴） */
static volatile uint32_t dbg_wr_req_t, dbg_stop_t, dbg_a5_t, dbg_urun_t;
static volatile uint32_t dbg_log_span_us;	/* LOG_INF 调用前后耗时（卡死定位） */

/* GPIO 标记（给硬件仿真用）：
 *   PJ06: I3C ISR 执行区间（入口 set，出口 clr，由 i3c_linkedsemi.c 控制）
 *   PJ07: 异常触发 LOG 前后翻转（由 log_core.c 控制） */
#define DBG_PIN_ISR	PJ06
#define DBG_PIN_BUG	PJ07

static bool dbg_io_inited;
static void dbg_io_init(void)
{
	if (!dbg_io_inited) {
		dbg_io_inited = true;
		io_cfg_output(DBG_PIN_ISR);
		io_cfg_output(DBG_PIN_BUG);
		io_clr_pin(DBG_PIN_ISR);
		io_clr_pin(DBG_PIN_BUG);
	}
}

/* log_core.c 通过 extern 调用，避免 log_core 直接 include hal gpio。
 * idx=2: PJ07 翻转（val 忽略）。 */
void log_dbg_gpio_mark(int idx, int val)
{
	ARG_UNUSED(val);
	if (idx == 2) {
		io_toggle_pin(DBG_PIN_BUG);
	}
}

/* 原语阶梯（变体 5）：带 memory clobber 的 csrr，防止编译器跨目标重排 */
static ALWAYS_INLINE uint32_t dbg_cycle(void)
{
	uint32_t r;

	__asm__ volatile("csrr %0, mcycle" : "=r"(r) :: "memory");
	return r;
}
static volatile uint32_t dbg_prim_us[6];
__attribute__((noinline)) static void dbg_cold_fn(void)
{
	__asm__ volatile("nop" ::: "memory");
}

/* 心跳探针：IBI 完成后 dispatcher busy 采样 mstatus/mie/k_cycle，
 * 判定 8ms 死窗性质：采样连续+MIE=1 → CLIC/外设投递层问题；
 * 采样连续+MIE=0 → 有代码在线程上下文长持关中断（MPIE 锁死链）；
 * 采样出现大空洞 → CPU 整体停摆（XIP 取指/总线/调试器）。 */
#define HB_MAX 220
static volatile uint32_t hb_t[HB_MAX];
static volatile uint32_t hb_ms[HB_MAX];
static volatile uint32_t hb_mie[HB_MAX];
static volatile uint32_t hb_mt[HB_MAX];	/* mtime 低 32 位：验证窗口内 mtime 是否变慢/停走 */
static volatile int hb_n;

/* mtimer（linkedsemi,machine-timer @0xe0004000）：MTIME 低字 + 两个 hart 的
 * MTIMECMP 低字。窗口内 mtime 慢走 → 时钟门控；mtimecmp 被写远 → 比较值问题。 */
#define MTIME_LO_REG	(*(volatile uint32_t *)0xE000BFF8)
#define MTIMECMP0_LO	(*(volatile uint32_t *)0xE0004000)
#define MTIMECMP1_LO	(*(volatile uint32_t *)0xE0004008)

static void target_prefill_tx(void)
{
	if (tx_len == 0) {
		return;
	}

	int pre = i3c_target_tx_write(i3c_tgt_dev, (uint8_t *)tgt_tx_buf, tx_len, 0);
	if (pre > 0) {
		tx_idx = (uint16_t)pre;
	}

	/* 回读 TXCOUNT 确认字节确实落进 FIFO；只记账不打印 */
	I3C_TypeDef *tgt_base = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));
	dbg_prefill_wrote = pre;
	dbg_prefill_txcount = (tgt_base->SDATACTRL & I3C_SDATACTRL_TXCOUNT_MASK) >>
			      I3C_SDATACTRL_TXCOUNT_POS;
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
	/* 记账，供结尾汇总表使用（zassert 失败会中止当前测试线程，必须先记） */
	if (current_test_id < I3C_TEST_COUNT) {
		tgt_results[current_test_id] = tgt_failed ? 1 : 0;
		if (tgt_failed) {
			snprintf(tgt_result_reason[current_test_id],
				 sizeof(tgt_result_reason[current_test_id]),
				 "%s", tgt_fail_reason);
		} else {
			tgt_result_reason[current_test_id][0] = '\0';
		}
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

		if (tgt_results[i] < 0) {
			I3C_TEST_SUMMARY_ITEM(TGT, name, "NOT_RUN");
			continue;
		}
		ran++;
		if (tgt_results[i] == 0) {
			passed++;
			I3C_TEST_SUMMARY_ITEM(TGT, name, "PASS");
		} else if (tgt_result_reason[i][0] != '\0') {
			I3C_TEST_SUMMARY_ITEM_REASON(TGT, name, "FAIL",
						     tgt_result_reason[i]);
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
	addressed = true;
	tgt_failed = false;
	tgt_fail_reason[0] = '\0';
	current_test_id = id;

	/* 不在写传输中途预填（疑似污染源）；FIFO 由 stop_cb/初始化时
	 * 在总线空闲时装填。 */
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

static void probe_arm(void);

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

	dbg_io_init();

	int ret = i3c_ibi_raise(i3c_tgt_dev, &request);
	if (ret != 0) {
		LOG_ERR("ready IBI raise failed for test %u: %d", test_id, ret);
	} else {
		/* 变量分离实验：这行 LOG 是 8ms 死窗的"扳机"（双盲 A/B 已证：
		 * 注释掉 → EVENT 后 ~65us STOP 中断正常上；恢复 → 死窗复现）。
		 * 用三种形态分离"扳机的有效成分"：
		 *   1 = LOG_INF（deferred 入队，log 线程稍后唤醒打印）——FAIL 基线
		 *   2 = k_busy_wait 等耗时，无任何子系统活动 —— 区分纯代码时序
		 *   3 = printk 同步 UART 输出 —— 区分 UART 活动 vs log 子系统 */
#define READY_LOG_VARIANT 1
#if READY_LOG_VARIANT == 1
		{
			/* 卡死定位：LOG_INF 前后打点。若 span≈8ms → 阻塞在 LOG 调用内部；
			 * 若 span 仅几 us 而 hb0 在 8ms 后 → 阻塞在 LOG 返回之后。 */
			uint32_t lg_t0 = k_cycle_get_32();

			log_dbg_freeze = 0;	/* 只捕获这一次 LOG 的内部耗时 */
			to_dbg_arm = 1;		/* 捕获随后第一次 z_add_timeout */
			LOG_INF("ready IBI sent for test %u", test_id);
			dbg_log_span_us = k_cyc_to_us_floor32(k_cycle_get_32() - lg_t0);
		}
#elif READY_LOG_VARIANT == 2
		k_busy_wait(5);
#elif READY_LOG_VARIANT == 3
		printk("ready IBI sent for test %u\n", test_id);
#elif READY_LOG_VARIANT == 4
		{
			/* AMO 裸测：LOG 已被定位到阻塞在 post_finalize 的
			 * atomic_inc(&buffered_cnt)（amoadd.w.aqrl 到 SRAM）。
			 * 这里直接用一颗无关 atomic 复现，证明与 LOG 无关。 */
			static atomic_t dbg_amo;
			uint32_t lg_t0 = k_cycle_get_32();

			atomic_inc(&dbg_amo);
			dbg_log_span_us = k_cyc_to_us_floor32(k_cycle_get_32() - lg_t0);
		}
#elif READY_LOG_VARIANT == 5
		{
			/* 原语阶梯：同位置逐个测量各类指令的耗时，
			 * 哪种原语吃掉 8ms 哪种就是总线冻结的受害者类型。 */
			static volatile uint32_t s_var;
			uint32_t t0, t1;

			t0 = dbg_cycle();
			s_var = 0x5a5a;
			t1 = dbg_cycle();
			dbg_prim_us[0] = t1 - t0;	/* store */

			t0 = dbg_cycle();
			t1 = s_var;
			dbg_prim_us[1] = dbg_cycle() - t0;	/* load */
			(void)t1;

			t0 = dbg_cycle();
			t1 = (uint32_t)k_cyc_to_us_floor32(0x12345678);
			dbg_prim_us[2] = dbg_cycle() - t0;	/* 64bit div */
			(void)t1;

			t0 = dbg_cycle();
			dbg_cold_fn();
			dbg_prim_us[3] = dbg_cycle() - t0;	/* 冷函数调用(fetch) */

			{
				static atomic_t s_amo;

				t0 = dbg_cycle();
				atomic_inc(&s_amo);
				dbg_prim_us[4] = dbg_cycle() - t0;	/* AMO */
			}

			t0 = dbg_cycle();
			__asm__ volatile("nop"); __asm__ volatile("nop");
			__asm__ volatile("nop"); __asm__ volatile("nop");
			dbg_prim_us[5] = dbg_cycle() - t0;	/* nop x4(纯流水) */
		}
#endif
	}
}

/* IBI 后投递探针 v3（k_timer 变化事件记录仪）：v2 发现真正的案发窗口是
 * "IBI 上总线完成 → EVENT 终态中断被处理"之间（controller 收到 IBI 后
 * 40us 内就发出后续帧，此刻 state==IBI）。等 ibi_raise 返回再采样已错过
 * 现场，改为 dispatcher 收到 TEST_START 即布防的定时器采样（500us 步进、
 * 2.5s 窗口），任一监视量变化才记录：
 *  - st/dc(RXCOUNT)/msk 变化而 isr 不变 → 外设立了状态但中断没投递
 *  - 全部不变而示波器有帧               → 外设对帧无反应（硬件/匹配层）
 *  - isr 增长                           → 中断投递正常
 * 采样在 timer ISR 里只存不打印，TEST_END 时统一输出。 */
#define PROBE_EV_MAX 96
static uint32_t probe_ev_t[PROBE_EV_MAX];	/* us，相对布防时刻 */
static uint32_t probe_ev_st[PROBE_EV_MAX];
static uint32_t probe_ev_dc[PROBE_EV_MAX];
static uint32_t probe_ev_msk[PROBE_EV_MAX];
static uint32_t probe_ev_ip[PROBE_EV_MAX];	/* 复用：CLICINT[117] 打包 IP|IE<<8|CTL<<16 */
static uint32_t probe_ev_clic7[PROBE_EV_MAX];	/* CLICINT[7](timer) 打包 */
static uint32_t probe_ev_mc0[PROBE_EV_MAX];	/* MTIMECMP[hart0] 低字 */
static uint32_t probe_ev_mc1[PROBE_EV_MAX];	/* MTIMECMP[hart1] 低字 */
static uint32_t probe_ev_isr[PROBE_EV_MAX];
static int probe_ev_n;
static uint32_t probe_t0_us;	/* 布防时刻的绝对开机 us */

static uint32_t probe_base_cycle, probe_isr_base;
static uint32_t probe_last_st, probe_last_dc, probe_last_msk,
		probe_last_ip, probe_last_isr, probe_last_clic7;
static uint32_t probe_last_mc0, probe_last_mc1;
static uint32_t probe_mc0_pre, probe_mc0_post, probe_mtime_post;
static int probe_ticks;

/* 采样本体：k_timer(500us) 与心跳线程(100us) 共用。
 * 心跳线程在死窗内全程运行（线程上下文，不依赖中断投递），
 * 因此窗口内 SSTATUS/CLIC 的每一刻变化都能被记录。 */
static void probe_sample(void)
{
	I3C_TypeDef *tb = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));
	const uint32_t irqn = DT_IRQN(DT_NODELABEL(i3c6));
	uint32_t st = tb->SSTATUS;
	uint32_t dc = tb->SDATACTRL;
	uint32_t msk = tb->SINTMASKED;
	uint32_t clic = (uint32_t)CLIC->CLICINT[irqn].IP |
			((uint32_t)CLIC->CLICINT[irqn].IE << 8) |
			((uint32_t)CLIC->CLICINT[irqn].CTL << 16);
	uint32_t clic7 = (uint32_t)CLIC->CLICINT[7].IP |
			 ((uint32_t)CLIC->CLICINT[7].IE << 8) |
			 ((uint32_t)CLIC->CLICINT[7].CTL << 16);
	uint32_t mc0 = MTIMECMP0_LO;
	uint32_t mc1 = MTIMECMP1_LO;
	uint32_t isr = i3c_isr_enter_cnt - probe_isr_base;

	if ((st != probe_last_st) || (dc != probe_last_dc) ||
	    (msk != probe_last_msk) || (clic != probe_last_ip) ||
	    (clic7 != probe_last_clic7) || (mc0 != probe_last_mc0) ||
	    (mc1 != probe_last_mc1) || (isr != probe_last_isr)) {
		if (probe_ev_n < PROBE_EV_MAX) {
			int n = probe_ev_n++;
			probe_ev_t[n] = k_cyc_to_us_floor32(k_cycle_get_32() - probe_base_cycle);
			probe_ev_st[n] = st;
			probe_ev_dc[n] = dc;
			probe_ev_msk[n] = msk;
			probe_ev_ip[n] = clic;
			probe_ev_clic7[n] = clic7;
			probe_ev_mc0[n] = mc0;
			probe_ev_mc1[n] = mc1;
			probe_ev_isr[n] = isr;
		}
		probe_last_st = st; probe_last_dc = dc; probe_last_msk = msk;
		probe_last_ip = clic; probe_last_clic7 = clic7;
		probe_last_mc0 = mc0; probe_last_mc1 = mc1;
		probe_last_isr = isr;
	}
}

static void probe_timer_fn(struct k_timer *timer)
{
	probe_sample();
	if (++probe_ticks >= 5000) {	/* 5000 × 500us = 2.5s 后自动撤防 */
		k_timer_stop(timer);
	}
}

static K_TIMER_DEFINE(probe_timer, probe_timer_fn, NULL);

static void probe_arm(void)
{
	I3C_TypeDef *tb = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));
	const uint32_t irqn = DT_IRQN(DT_NODELABEL(i3c6));

	probe_ev_n = 0;
	probe_ticks = 0;
	probe_base_cycle = k_cycle_get_32();
	probe_t0_us = k_cyc_to_us_floor32(probe_base_cycle);
	probe_isr_base = i3c_isr_enter_cnt;
	/* 采一版初值，后续只记变化 */
	probe_last_st = tb->SSTATUS;
	probe_last_dc = tb->SDATACTRL;
	probe_last_msk = tb->SINTMASKED;
	probe_last_ip = (uint32_t)CLIC->CLICINT[irqn].IP |
			((uint32_t)CLIC->CLICINT[irqn].IE << 8) |
			((uint32_t)CLIC->CLICINT[irqn].CTL << 16);
	probe_last_clic7 = (uint32_t)CLIC->CLICINT[7].IP |
			   ((uint32_t)CLIC->CLICINT[7].IE << 8) |
			   ((uint32_t)CLIC->CLICINT[7].CTL << 16);
	probe_last_mc0 = MTIMECMP0_LO;
	probe_last_mc1 = MTIMECMP1_LO;
	probe_last_isr = 0;
	i3c_isr_log_n = 0;	/* 中断来源识别：从布防时刻开始记 */
	e906_irq_log_n = 0;
	/* 验证 k_timer_start 是否重编 mtimecmp：z_add_timeout 成为超时链表头
	 * 时必须调 sys_clock_set_timeout。实测死窗内 mc0 全程没动（含 start
	 * 之后），疑似 timeout 层异常，这里留 start 前后的直接证据。 */
	probe_mc0_pre = MTIMECMP0_LO;
	k_timer_start(&probe_timer, K_NO_WAIT, K_USEC(500));
	probe_mc0_post = MTIMECMP0_LO;
	probe_mtime_post = MTIME_LO_REG;
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

/* ========== I3C target callbacks ========== */

static int slave_write_requested_cb(struct i3c_target_config *config)
{
	ARG_UNUSED(config);
	rx_idx = 0;
	dbg_wr_req_cnt++;
	dbg_wr_req_t = k_cycle_get_32();
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
		default:
			/* 非 opcode 字节（如 write_read/write_stop_read 写阶段的占位字节
			 * CMD_DUMMY_BYTE）按原始数据记录，便于排查。
			 * 注意：此处不能调 target_prefill_tx() 做写段中途预填。 */
			if (write_len < sizeof(write_data)) {
				write_data[write_len++] = val;
			}
			/* 调试快照只记账：ISR 内不打印（时序敏感） */
			{
				I3C_TypeDef *tb = (I3C_TypeDef *)DT_REG_ADDR(DT_NODELABEL(i3c6));
				dbg_a5_sstatus = tb->SSTATUS;
				dbg_a5_sintset = tb->SINTSET;
				dbg_a5_sintmasked = tb->SINTMASKED;
				dbg_a5_t = k_cycle_get_32();
			}
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
			struct tgt_event evt = {
				.test_id = current_test_id,
				.ctrl_result = ctrl_result,
			};
			(void)k_msgq_put(&tgt_event_queue, &evt, K_NO_WAIT);
		}
		parse_state = TGT_PARSE_IDLE;
		break;

	case TGT_PARSE_READ_LEN:
		tx_len = val;
		last_read_len = val;
		/* 不在写传输中途预填（疑似污染源）；读数据由随后的 stop_cb
		 * 在总线空闲时装填。 */
		parse_state = TGT_PARSE_IDLE;
		break;

	case TGT_PARSE_WRITE_LEN:
		write_len_expected = val;
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

/* 实验（排查 IBI 后 8ms 死窗）：1 = 屏蔽 STOP 回调里的 target_prefill_tx。
 * 结论（NOPREFIL 烧录实测）：屏蔽后死窗依旧，prefill 假设排除，恢复为 0。 */
#define TGT_DBG_NO_STOP_PREFILL 0

static int slave_stop_cb(struct i3c_target_config *config)
{
	ARG_UNUSED(config);

	/* ISR 内不打印（时序敏感）：只记账，dispatcher 线程统一输出 */
	dbg_stop_cnt++;
	dbg_stop_t = k_cycle_get_32();

	rx_idx = 0;
	read_req_cnt = 0;
	read_proc_cnt = 0;
	parse_state = TGT_PARSE_IDLE;
	/* Restore default response length so a following direct read is deterministic. */
	tx_len = 32;
#if !TGT_DBG_NO_STOP_PREFILL
	target_prefill_tx();
#endif
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

	printk("TGT BUILD_TAG: FAST1MS\n");	/* 临时标记：验证烧录镜像新旧 */

	for (int i = 0; i < I3C_TEST_COUNT; i++) {
		tgt_results[i] = -1;
	}

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
			probe_arm();	/* 布防：覆盖 IBI 盲区 + 整个测试过程 */
			raise_ready_ibi(evt.test_id);
			/* 心跳：100us x 220 ≈ 22ms，覆盖 IBI 后的死窗；
			 * 顺带做寄存器变化采样（线程上下文，不受投递失败影响） */
			hb_n = 0;
			for (int i = 0; i < HB_MAX; i++) {
				uint32_t ms, mie;

				__asm__ volatile("csrr %0, mstatus" : "=r"(ms));
				__asm__ volatile("csrr %0, mie" : "=r"(mie));
				hb_t[i] = k_cycle_get_32();
				hb_ms[i] = ms;
				hb_mie[i] = mie;
				hb_mt[i] = MTIME_LO_REG;
				hb_n = i + 1;
				probe_sample();
				k_busy_wait(100);
			}
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
		case TGT_EVT_TEST_END:
			current_test_id = evt.test_id;
			ctrl_result = evt.ctrl_result;
			/* ISR 回调记账统一在这里（线程上下文）输出。时刻为开机 us */
			printk("TGT_DBG: wr_req=%u@%lu stop=%u@%lu prefill=%d/%lu "
			       "a5[st=%08lx set=%08lx msk=%08lx]@%lu rxp_stuck=%u "
			       "event_isr@%lu serrwarn=%lux%08lx@%lu "
			       "logspan=%luus hb0@%lu pticks=%d\n",
			       dbg_wr_req_cnt, k_cyc_to_us_floor32(dbg_wr_req_t),
			       dbg_stop_cnt, k_cyc_to_us_floor32(dbg_stop_t),
			       dbg_prefill_wrote, dbg_prefill_txcount,
			       dbg_a5_sstatus, dbg_a5_sintset, dbg_a5_sintmasked,
			       k_cyc_to_us_floor32(dbg_a5_t), rxp_stuck_cnt,
			       k_cyc_to_us_floor32(i3c_event_isr_t),
			       i3c_serrwarn_cnt, i3c_serrwarn_last,
			       k_cyc_to_us_floor32(i3c_serrwarn_t),
			       dbg_log_span_us,
			       hb_n ? k_cyc_to_us_floor32(hb_t[0]) : 0,
			       probe_ticks);
			printk("LOG_DBG: alloc=%luus postfin=%luus atomic=%luus "
			       "tmr=%luus ts=%luus mpsc=%luus cnt=%lu "
			       "ld1=%luus ld2=%luus ld3=%luus\n",
			       log_dbg_alloc_us, log_dbg_postfin_us, log_dbg_atomic_us,
			       log_dbg_tmr_us, log_dbg_ts_us, log_dbg_mpsc_us,
			       log_dbg_cnt,
			       log_dbg_ld1_us, log_dbg_ld2_us, log_dbg_ld3_us);
				printk("GAP_DBG: mtime=%luus mcycle=%luus g0=%luus g1=%luus g2=%luus g3=%luus "
				       "g4=%luus g5=%luus g6=%luus g7=%luus\n",
				       log_dbg_mtime_us, log_dbg_mcycle_us, log_dbg_gap_us[0], log_dbg_gap_us[1], log_dbg_gap_us[2],
				       log_dbg_gap_us[3], log_dbg_gap_us[4], log_dbg_gap_us[5],
				       log_dbg_gap_us[6], log_dbg_gap_us[7]);
			printk("TO_DBG: add_to=%luus dticks=%lu elapsed=%lu first=%lu\n",
			       to_dbg_us, to_dbg_dticks, to_dbg_elapsed, to_dbg_first);
			printk("E906_DBG: dispatch=%u mpie0=%u mstatus=%08lx exc=%u "
			       "mpie0ret=%u@%lu mcause=%08lx mepc=%08lx\n",
			       e906_dispatch_cnt, e906_mpie0_cnt, e906_mpie0_mstatus,
			       e906_exc_cnt, e906_mpie0_ret_cnt,
			       k_cyc_to_us_floor32(e906_mpie0_ret_t),
			       e906_mpie0_ret_mcause, e906_mpie0_ret_mepc);
			/* 心跳分析：最大采样空洞、MIE=0 / MTIE=0 样本数 */
			{
				uint32_t maxgap = 0, gapat = 0;
				int mie0 = 0, mtie0 = 0;

				for (int i = 1; i < hb_n; i++) {
					uint32_t g = hb_t[i] - hb_t[i - 1];

					if (g > maxgap) {
						maxgap = g;
						gapat = hb_t[i - 1];
					}
					if ((hb_ms[i] & 0x8) == 0) {
						mie0++;
					}
					if ((hb_mie[i] & 0x80) == 0) {
						mtie0++;
					}
				}
				printk("HB: n=%d maxgap=%luus@%lu mie0=%d/%d mtie0=%d "
				       "ms_last=%08lx mie_last=%08lx\n",
				       hb_n, k_cyc_to_us_floor32(maxgap),
				       k_cyc_to_us_floor32(gapat), mie0, hb_n, mtie0,
				       hb_n ? hb_ms[hb_n - 1] : 0,
				       hb_n ? hb_mie[hb_n - 1] : 0);
				/* mtime 斜率：前半段（含死窗）vs 后半段（恢复后）。
				 * 两段比率不一致 = 窗口内 mtime 慢走/停走（时钟门控）。 */
				if (hb_n >= 20) {
					int mid = hb_n / 2;
					uint32_t cyc_a = hb_t[mid] - hb_t[0];
					uint32_t mt_a = hb_mt[mid] - hb_mt[0];
					uint32_t cyc_b = hb_t[hb_n - 1] - hb_t[mid];
					uint32_t mt_b = hb_mt[hb_n - 1] - hb_mt[mid];

					printk("HB_TIME: 1st cyc=%lu mt=%lu | 2nd cyc=%lu mt=%lu\n",
					       cyc_a, mt_a, cyc_b, mt_b);
				}
			}
			/* IBI 后投递探针：t 为相对 IBI 完成的 us；isr 为 ISR 进入增量 */
			printk("PROBE t0=%lu (abs us) mc0pre=%08lx mc0post=%08lx mtimepost=%08lx\n",
			       probe_t0_us, probe_mc0_pre, probe_mc0_post, probe_mtime_post);
			for (int i = 0; i < probe_ev_n; i++) {
				printk("PROBE[%02d]: t=%lu st=%08lx dc=%08lx msk=%08lx "
				       "clic=%06lx c7=%06lx mc0=%08lx mc1=%08lx isr=+%u\n",
				       i, probe_ev_t[i], probe_ev_st[i], probe_ev_dc[i],
				       probe_ev_msk[i], probe_ev_ip[i], probe_ev_clic7[i],
				       probe_ev_mc0[i], probe_ev_mc1[i], probe_ev_isr[i]);
			}
			/* 中断来源识别：ISR 入口 SSTATUS 快照 + 分发层 IRQ 投递记录 */
			printk("ISRLOG n=%u\n", i3c_isr_log_n);
			for (int i = 0; i < i3c_isr_log_n; i++) {
				printk("ISRLOG[%02d]: t=%lu st=%08lx\n", i,
				       k_cyc_to_us_floor32(i3c_isr_log_t[i]),
				       i3c_isr_log_st[i]);
			}
			printk("IRQLOG n=%u\n", e906_irq_log_n);
			for (int i = 0; i < e906_irq_log_n; i++) {
				uint16_t id = e906_irq_log_id[i];

				if (id & 0x8000) {
					printk("IRQLOG[%02d]: t=%lu EXC mcause=%03x mepc=%08lx\n", i,
					       k_cyc_to_us_floor32(e906_irq_log_t[i]),
					       id & 0xfff, e906_irq_log_mepc[i]);
				} else {
					printk("IRQLOG[%02d]: t=%lu irq=%u\n", i,
					       k_cyc_to_us_floor32(e906_irq_log_t[i]), id);
				}
			}
			/* Run the entire suite; only the matching test validates. */
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
	case I3C_TEST_WRITE_STOP_READ_16:
		validate_write_stop_read_16();
		break;
	case I3C_TEST_WRITE_STOP_READ_17:
		validate_write_stop_read_17();
		break;
	case I3C_TEST_WRITE_STOP_READ_32:
		validate_write_stop_read_32();
		break;
	default:
		TGT_EXPECT(false, "unknown test id %u", current_test_id);
		break;
	}

	tgt_finish();
}
