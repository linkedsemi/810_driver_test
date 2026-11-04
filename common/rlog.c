/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RLOG 实现（协议与理由见 rlog.h）。
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/sys/printk.h>
#include <zephyr/cache.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rlog.h"

#define RLOG_VER        1
#define RLOG_T_LINE     1
#define RLOG_T_DONE     2
#define RLOG_CORE_TGT   1

/* 从核发不出去时的重试：先让出 CPU 等主核 drain，再试。次数 × RLOG_RETRY_MS
 * 即单行最坏等待；耗尽计入 lost（主核收不到 ⇒ 该行判缺证据）。 */
#define RLOG_RETRY_MAX  200
#define RLOG_RETRY_MS   2

/* 接收环形缓冲行数：要覆盖从核的**突发**（SUMMARY 段一次 ~14 帧）。 */
/* Receive ring depth.  This, not the mbox FIFO depth, is where a burst is lost:
 * an i2cf run emits ~110 lines back to back while the drain prints at UART speed
 * (~9 ms/line at 115200), so a 24-line ring drops most of them (KNOWN_ISSUES #147:
 * raising the mbox depth 16 -> 64 did not help).  Images that relay peer suites
 * override it (see cpu1 CMakeLists).
 */
#ifndef RLOG_RING
#define RLOG_RING       24
#endif

struct rlog_frame {
	uint8_t magic[2];                       /* 'R','L' */
	uint8_t ver;
	uint8_t type;                           /* RLOG_T_LINE / RLOG_T_DONE */
	uint8_t core;                           /* RLOG_CORE_TGT */
	uint8_t seq;                            /* 从核单调 +1，回绕 */
	uint8_t len;                            /* text 有效字节数 */
	uint8_t verdict;                        /* DONE：0=PASS 1=FAIL；LINE 恒 0 */
	char    text[RLOG_TEXT_MAX + 1];
};

#if defined(RLOG_POINTER_TRANSPORT)
/*
 * Pointer transport (integrated images only).
 *
 * The mbox frame carries an 8-byte header plus a slot index instead of the
 * 119-byte text: the text itself sits in a fixed shared-SRAM ring that both
 * cores map at the same address.  This drops the rlog channel from 128x8
 * (2072 B of the 4 KB MBOX carve-out) to 8x16 (280 B).
 *
 * Ring lives right after the 32-byte peer handshake block, inside the board's
 * 4 KB "share_memory" region (0x100BE000); the MBOX carve-out starts at
 * 0x100BF000, so 24 slots of 128 B fit with margin.
 */
#define RLOG_SLOT_SIZE   128
#define RLOG_SLOT_COUNT  24
#define RLOG_SLOT_BASE   0x100BE100u

struct rlog_ptr_frame {
	uint8_t magic[2];                       /* 'R','L' */
	uint8_t type;                           /* RLOG_T_LINE / RLOG_T_DONE */
	uint8_t slot;                           /* ring slot holding the text */
	uint8_t len;                            /* text bytes in that slot */
	uint8_t verdict;                        /* DONE: 0=PASS 1=FAIL */
	uint8_t seq;                            /* 从核单调 +1，回绕 */
	uint8_t rsv;
};

_Static_assert(sizeof(struct rlog_ptr_frame) == 8,
	       "rlog pointer frame must match the 8-byte mbox channel width");

static char *rlog_slot(uint8_t slot)
{
	return (char *)(uintptr_t)(RLOG_SLOT_BASE + (uint32_t)slot * RLOG_SLOT_SIZE);
}
#endif /* RLOG_POINTER_TRANSPORT */

static const struct mbox_dt_spec rlog_ch = MBOX_DT_SPEC_GET(DT_NODELABEL(rlog_channel), mbox);

/* The mbox driver fills its per-channel fifo table by each consumer's position
 * under &mbox (DT_INST_FOREACH_CHILD_STATUS_OKAY order), while send/probe use
 * the "channel" cell of the same node as that array index. A board-level
 * consumer inserted ahead of rlog_channel therefore shifts the real index and
 * silently routes RLOG frames onto the wrong fifo (e.g. a 127-byte frame onto
 * a 2-byte channel) without any devicetree error. Pin the two together at
 * build time. The child index counts all children while the driver counts only
 * status-okay ones, so this assertion is the stricter of the two. */
BUILD_ASSERT(DT_PHA_BY_IDX(DT_NODELABEL(rlog_channel), mboxes, 0, channel) ==
	     DT_NODE_CHILD_IDX(DT_NODELABEL(rlog_channel)),
	     "rlog_channel mboxes channel cell must equal its child index under &mbox");

/* ---- 背压量化（头文件里有对外声明；此处定义）---- */
volatile uint32_t rlog_stat_tx_lines;
volatile uint32_t rlog_stat_tx_retries;
volatile uint32_t rlog_stat_tx_max_retry;
volatile uint32_t rlog_stat_tx_max_block_ms;
volatile uint32_t rlog_stat_tx_lost;
volatile uint32_t rlog_stat_rx_hwm;

/* Non-zero while the caller wants ztest tokens to stay off its own UART.
 * Read by the PRINT_DATA override (tc_util_user_override.h), which the ztest
 * library's TU also compiles.  A plain global on purpose: ztest runs each test
 * in its OWN thread (ztest.c creates `ztest_thread`), so a thread-name guard
 * inside PRINT_DATA never matched and never suppressed anything. */
volatile int rlog_local_echo_off;

/* 中继信封（PRIMARY 侧，默认关闭 ⇒ 打印形态与既有逐字相同） */
static const char *relay_prefix;
static const char *relay_core;

static struct {
	uint8_t seq;
	bool    used;
} tx;

static struct {
	char     line[RLOG_RING][RLOG_TEXT_MAX + 1];
	volatile uint8_t head;                  /* ISR 写 */
	volatile uint8_t tail;                  /* thread 读 */
	uint8_t  last_seq;
	bool     seq_seen;
	bool     done_seen;
	bool     done_ok;
	bool     anything;
	volatile uint32_t lost;
} rx;

/* ---------------------------------------------------------------- 发送侧 */

static int tx_send(uint8_t type, const char *text, uint8_t verdict)
{
	struct rlog_frame f;
	struct mbox_msg msg = { .data = &f, .size = sizeof(f) };
	size_t n = (text != NULL) ? strlen(text) : 0;

	/* 调用方沿用 `printk("…\n")` 的写法 ⇒ 行尾换行由接收侧补，这里剥掉。
	 * **行首**换行一并剥掉：ztest 的断言行走的是 PRINT_DATA("\n    Assertion
	 * failed at …")（行首带换行）——帧是"一行"的传输单位，行首换行若保留，
	 * 中继时信封只加在第一段上，第二段（真正的断言文本）会掉出分拣范围而留在
	 * 主核归档里，断言→FAIL 的归因就断了。 */
	while ((n > 0U) && ((text[n - 1U] == '\n') || (text[n - 1U] == '\r'))) {
		n--;
	}
	while ((n > 0U) && ((text[0] == '\n') || (text[0] == '\r'))) {
		text++;
		n--;
	}

	bool truncated = (n > RLOG_TEXT_MAX);

	if (truncated) {
		n = RLOG_TEXT_MAX;
	}
	/* Take the sequence number once: the pointer branch below used to also do
	 * `pf.seq = tx.seq++`, which advanced it by two per frame.  The receiver
	 * then saw a gap on every frame and counted a false `lost` (185 in one
	 * peer-safe run) even though nothing was dropped. */
	uint8_t seq = tx.seq++;

	memset(&f, 0, sizeof(f));
	f.magic[0] = 'R';
	f.magic[1] = 'L';
	f.ver = RLOG_VER;
	f.type = type;
	f.core = RLOG_CORE_TGT;
	f.seq = seq;
	f.verdict = verdict;
	f.len = (uint8_t)n;
	if (n > 0U) {
		memcpy(f.text, text, n);
	}
	if (truncated && (n >= 3U)) {
		/* 截断了就明确标出来，读者一眼看出这行不完整（不是"就这么短"） */
		f.text[n - 3] = '.';
		f.text[n - 2] = '.';
		f.text[n - 1] = '.';
	}

#if defined(RLOG_POINTER_TRANSPORT)
	static uint8_t tx_slot;
	struct rlog_ptr_frame pf;
	char *dst;

	memset(&pf, 0, sizeof(pf));
	pf.magic[0] = 'R';
	pf.magic[1] = 'L';
	pf.type = type;
	pf.slot = tx_slot;
	tx_slot = (uint8_t)((tx_slot + 1U) % RLOG_SLOT_COUNT);
	pf.len = (uint8_t)n;
	pf.verdict = verdict;
	pf.seq = seq;

	dst = rlog_slot(pf.slot);
	if (n > 0U) {
		memcpy(dst, text, n);
	}
	if (truncated && (n >= 3U)) {
		dst[n - 3] = '.';
		dst[n - 2] = '.';
		dst[n - 1] = '.';
	}
	dst[n] = '\0';
	/* The peer reads this slot from its own mapping of the same SRAM. */
	(void)sys_cache_data_flush_range(dst, RLOG_SLOT_SIZE);

	msg.data = &pf;
	msg.size = sizeof(pf);
#endif

	int64_t t0 = k_uptime_get();
	int retries = 0;
	int rc = -1;

	for (int i = 0; i < RLOG_RETRY_MAX; i++) {
		if (mbox_send_dt(&rlog_ch, &msg) == 0) {
			rc = 0;
			break;
		}
		if (k_is_in_isr()) {
			break;                  /* ISR 里不能让出；计丢失 */
		}
		retries++;
		k_sleep(K_MSEC(RLOG_RETRY_MS));
	}

	/* 背压量化：阻塞时长 = 单行对测试的最大扰动。ISR 路径不计（它不让路）。 */
	if (!k_is_in_isr()) {
		uint32_t block_ms = (uint32_t)(k_uptime_get() - t0);

		if (block_ms > rlog_stat_tx_max_block_ms) {
			rlog_stat_tx_max_block_ms = block_ms;
		}
		if ((uint32_t)retries > rlog_stat_tx_max_retry) {
			rlog_stat_tx_max_retry = (uint32_t)retries;
		}
		rlog_stat_tx_retries += (uint32_t)retries;
	}
	if (rc != 0) {
		rlog_stat_tx_lost++;
	} else {
		rlog_stat_tx_lines++;
	}
	return rc;
}

void rlog_secondary_init(void)
{
	(void)mbox_set_enabled(rlog_ch.dev, rlog_ch.channel_id, true);
}

void rlog_emit(const char *text)
{
	if (tx_send(RLOG_T_LINE, text, 0) != 0) {
		rx.lost++;                      /* 发送侧丢失（同一计数器） */
	}
}

void rlog_emitf(const char *fmt, ...)
{
	char buf[RLOG_TEXT_MAX + 1];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0) {
		buf[0] = '\0';
	} else if ((size_t)n >= sizeof(buf)) {
		/* 超长：vsnprintf 静默截断（返回的是**本应写入**的长度）⇒
		 * 明确标出"这行不完整"，否则读者会以为它就这么短（例如把
		 * ` PASS - … in 0.001 seconds` 的 `seconds` 截掉，解析器就
		 * 认不出这一项，证据凭空消失）。 */
		buf[sizeof(buf) - 1] = '\0';
		memcpy(&buf[sizeof(buf) - 4], "...", 3);
	}
	rlog_emit(buf);
}

void rlog_emit_done(bool verdict_ok, const char *text)
{
	if (tx_send(RLOG_T_DONE, text, verdict_ok ? 0U : 1U) != 0) {
		rx.lost++;
	}
}

/* ---------------------------------------------------------------- 接收侧 */

#if defined(RLOG_DRAIN_THREAD)
/* Given by the mbox ISR after a line is queued, taken by the drain thread.
 * Must be declared before rx_cb/drain_thread, which are defined above the
 * drainer section below. */
static K_SEM_DEFINE(rlog_rx_sem, 0, 1);
#endif

static void rx_cb(const struct device *dev, mbox_channel_id_t channel,
		  void *user_data, struct mbox_msg *msg)
{
	/* ISR：只入环形缓冲，绝不打印（LOG/printk 在 ISR 会打断串口输出）。 */
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(user_data);

#if defined(RLOG_POINTER_TRANSPORT)
	struct rlog_frame f_local;
	const struct rlog_ptr_frame *pf;
	const struct rlog_frame *f = &f_local;

	if ((msg == NULL) || (msg->data == NULL) ||
	    (msg->size < sizeof(struct rlog_ptr_frame))) {
		rx.lost++;
		return;
	}
	pf = (const struct rlog_ptr_frame *)msg->data;
	if ((pf->magic[0] != 'R') || (pf->magic[1] != 'L') ||
	    (pf->slot >= RLOG_SLOT_COUNT) || (pf->len > RLOG_TEXT_MAX) ||
	    (pf->type == 0U) || (pf->type > RLOG_T_DONE)) {
		rx.lost++;
		return;
	}
	memset(&f_local, 0, sizeof(f_local));
	f_local.magic[0] = pf->magic[0];
	f_local.magic[1] = pf->magic[1];
	/* The 8-byte pointer frame carries no version field; without this the
	 * shared validation below (f->ver != RLOG_VER) rejects every frame and
	 * nothing ever reaches the relay ring. */
	f_local.ver = RLOG_VER;
	f_local.type = pf->type;
	f_local.len = pf->len;
	f_local.verdict = pf->verdict;
	f_local.seq = pf->seq;
	f_local.core = RLOG_CORE_TGT;
	/* The sender flushed its slot; make sure this core reads fresh data. */
	(void)sys_cache_data_invd_range(rlog_slot(pf->slot), RLOG_SLOT_SIZE);
	memcpy(f_local.text, rlog_slot(pf->slot), pf->len);
#else
	if ((msg == NULL) || (msg->data == NULL) || (msg->size < sizeof(struct rlog_frame))) {
		rx.lost++;
		return;
	}

	const struct rlog_frame *f = (const struct rlog_frame *)msg->data;
#endif

	if ((f->magic[0] != 'R') || (f->magic[1] != 'L') || (f->ver != RLOG_VER) ||
	    (f->len > RLOG_TEXT_MAX) || (f->type == 0U) || (f->type > RLOG_T_DONE)) {
		rx.lost++;
		return;
	}
	if (rx.seq_seen && (uint8_t)(rx.last_seq + 1U) != f->seq) {
		rx.lost++;                      /* 跳号 = 中间有帧没到 */
	}
	rx.last_seq = f->seq;
	rx.seq_seen = true;

	uint8_t nh = (uint8_t)((rx.head + 1U) % RLOG_RING);

	if (nh == rx.tail) {
		rx.lost++;                      /* 环形缓冲满：主核 drain 太慢 */
		return;
	}
	memcpy(rx.line[rx.head], f->text, f->len);
	rx.line[rx.head][f->len] = '\0';
	rx.head = nh;
	rx.anything = true;
#if defined(RLOG_DRAIN_THREAD)
	k_sem_give(&rlog_rx_sem);
#endif

	{
		uint8_t occ = (uint8_t)((rx.head - rx.tail + RLOG_RING) % RLOG_RING);

		if (occ > rlog_stat_rx_hwm) {
			rlog_stat_rx_hwm = occ;
		}
	}

	if (f->type == RLOG_T_DONE) {
		rx.done_seen = true;
		rx.done_ok = (f->verdict == 0U);
	}
}

#if defined(RLOG_DRAIN_THREAD)
/*
 * Background drainer for integrated images.
 *
 * The i3c controller test drains the relay at the end of each case, which is
 * fine when the only producer is the i3c target.  When the peer core also runs
 * ordinary suites (`itest peer safe`), their tokens pile up in this 24-line
 * ring and are never printed - the user sees nothing on cpu1.  This thread
 * drains continuously (low priority) so every peer line lands on the console.
 */
static void drain_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		/* Wake on a new line; the timeout only covers lines that arrived
		 * while we were printing. */
		(void)k_sem_take(&rlog_rx_sem, K_MSEC(50));
		(void)rlog_relay_drain();
	}
}

static K_THREAD_STACK_DEFINE(drain_stack, 1024);
static struct k_thread drain_td;
#endif

void rlog_primary_init(void)
{
	(void)mbox_register_callback(rlog_ch.dev, rlog_ch.channel_id, rx_cb, NULL);
	(void)mbox_set_enabled(rlog_ch.dev, rlog_ch.channel_id, true);

#if defined(RLOG_DRAIN_THREAD)
	k_thread_create(&drain_td, drain_stack, K_THREAD_STACK_SIZEOF(drain_stack),
			drain_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(12), 0, K_NO_WAIT);
	k_thread_name_set(&drain_td, "rlog_drain");
#endif
}

void rlog_set_relay_envelope(const char *prefix, const char *core)
{
	relay_prefix = prefix;
	relay_core = core;
}

static K_MUTEX_DEFINE(rlog_drain_lock);

#if !defined(ITEST_PEER_SECONDARY)
/* Inventory of the peer's suites; only the primary allocates it.  Declared
 * before capture_inventory(), which is the only consumer below. */
static char inv_lines[RLOG_INV_MAX][RLOG_INV_TEXT];
static uint8_t inv_count;
#endif

/* Inventory lines are control data, not test output: capture them and keep
 * them off the console (the shell prints a formatted block from the capture). */
static bool capture_inventory(const char *line)
{
#if !defined(ITEST_PEER_SECONDARY)
	if (strstr(line, "INV ") != NULL) {
		if (inv_count < RLOG_INV_MAX) {
			strncpy(inv_lines[inv_count], line, RLOG_INV_TEXT - 1);
			inv_lines[inv_count][RLOG_INV_TEXT - 1] = '\0';
			inv_count++;
		}
		return true;
	}
#else
	ARG_UNUSED(line);
#endif
	return false;
}

/* FAIL 明细副本，给批末 summary 用（capacity 见 rlog.h）。 */
static char fail_lines[RLOG_FAIL_MAX][RLOG_TEXT_MAX + 1];
static uint8_t fail_count;

#if !defined(ITEST_PEER_SECONDARY)
void rlog_inv_reset(void)
{
	inv_count = 0;
}

int rlog_inv_count(void)
{
	return inv_count;
}

const char *rlog_inv_line(int idx)
{
	if (idx < 0 || idx >= inv_count) {
		return NULL;
	}
	return inv_lines[idx];
}
#else
void rlog_inv_reset(void) { }
int rlog_inv_count(void) { return 0; }
const char *rlog_inv_line(int idx) { ARG_UNUSED(idx); return NULL; }
#endif

void rlog_fail_reset(void)
{
	fail_count = 0;
}

int rlog_fail_count(void)
{
	return fail_count;
}

const char *rlog_fail_line(int idx)
{
	if (idx < 0 || idx >= fail_count) {
		return NULL;
	}
	return fail_lines[idx];
}

int rlog_relay_drain(void)
{
	int n = 0;

	if (k_mutex_lock(&rlog_drain_lock, K_MSEC(50)) != 0) {
		return 0;               /* someone else is draining */
	}

	while (rx.tail != rx.head) {
		if (!capture_inventory(rx.line[rx.tail])) {
			if (relay_prefix != NULL) {
				printk("%s:%s:%s\n", relay_prefix, relay_core,
				       rx.line[rx.tail]);
			} else {
				printk("%s\n", rx.line[rx.tail]);
			}
		}
		if (strstr(rx.line[rx.tail], " FAIL (") != NULL &&
		    fail_count < RLOG_FAIL_MAX) {
			strncpy(fail_lines[fail_count], rx.line[rx.tail],
				RLOG_TEXT_MAX);
			fail_lines[fail_count][RLOG_TEXT_MAX] = '\0';
			fail_count++;
		}
		rx.tail = (uint8_t)((rx.tail + 1U) % RLOG_RING);
		n++;
	}
	(void)k_mutex_unlock(&rlog_drain_lock);
	return n;
}

bool rlog_wait_secondary_done(uint32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + (int64_t)timeout_ms;

	while (k_uptime_get() < deadline) {
		(void)rlog_relay_drain();
		if (rx.done_seen) {
			break;
		}
		k_sleep(K_MSEC(2));
	}
	(void)rlog_relay_drain();
	return rx.done_seen;
}

bool rlog_secondary_ok(void)
{
	return rx.done_seen && rx.done_ok && (rx.lost == 0U);
}

bool rlog_secondary_silent(void)
{
	return !rx.anything;
}

uint32_t rlog_lost(void)
{
	return rx.lost;
}
