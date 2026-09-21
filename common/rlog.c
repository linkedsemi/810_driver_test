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
#define RLOG_RING       24

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

static const struct mbox_dt_spec rlog_ch = MBOX_DT_SPEC_GET(DT_NODELABEL(rlog_channel), mbox);

/* ---- 背压量化（头文件里有对外声明；此处定义）---- */
volatile uint32_t rlog_stat_tx_lines;
volatile uint32_t rlog_stat_tx_retries;
volatile uint32_t rlog_stat_tx_max_retry;
volatile uint32_t rlog_stat_tx_max_block_ms;
volatile uint32_t rlog_stat_tx_lost;
volatile uint32_t rlog_stat_rx_hwm;

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
	memset(&f, 0, sizeof(f));
	f.magic[0] = 'R';
	f.magic[1] = 'L';
	f.ver = RLOG_VER;
	f.type = type;
	f.core = RLOG_CORE_TGT;
	f.seq = tx.seq++;
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

static void rx_cb(const struct device *dev, mbox_channel_id_t channel,
		  void *user_data, struct mbox_msg *msg)
{
	/* ISR：只入环形缓冲，绝不打印（LOG/printk 在 ISR 会打断串口输出）。 */
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(user_data);

	if ((msg == NULL) || (msg->data == NULL) || (msg->size < sizeof(struct rlog_frame))) {
		rx.lost++;
		return;
	}

	const struct rlog_frame *f = (const struct rlog_frame *)msg->data;

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

void rlog_primary_init(void)
{
	(void)mbox_register_callback(rlog_ch.dev, rlog_ch.channel_id, rx_cb, NULL);
	(void)mbox_set_enabled(rlog_ch.dev, rlog_ch.channel_id, true);
}

void rlog_set_relay_envelope(const char *prefix, const char *core)
{
	relay_prefix = prefix;
	relay_core = core;
}

int rlog_relay_drain(void)
{
	int n = 0;

	while (rx.tail != rx.head) {
		if (relay_prefix != NULL) {
			printk("%s:%s:%s\n", relay_prefix, relay_core,
			       rx.line[rx.tail]);
		} else {
			printk("%s\n", rx.line[rx.tail]);
		}
		rx.tail = (uint8_t)((rx.tail + 1U) % RLOG_RING);
		n++;
	}
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
