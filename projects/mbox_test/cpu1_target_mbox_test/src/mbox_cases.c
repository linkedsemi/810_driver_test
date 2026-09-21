/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * MBOX test - cpu1 (target) 扩展 case 实现（P1c 追加）
 *
 * 只实现 V2 协议（mbox_test_protocol.h 追加段）target 侧的应答逻辑；
 * 具体 case 由 CONFIG_MBOX_TEST_CASE_ID 选择（见本目录 Kconfig），
 * 入口 mbox_cases_target_main() 由 src/main.c 在 ID != 0 时调用。
 *
 * 约定：
 *   - ISR 只记账不打 log（§6.1.2），应答统一由本文件的调用线程发送。
 *   - 判定（§1.1/§1.3 修复）：每条 check 用 mbox_item_pass/mbox_item_fail
 *     上报 `INFO <name>: pass=<0|1>,<明细>`，失败置本核判负，src/main.c
 *     在入口返回后按状态打 SUCCESSFUL / FAILED。
 *     修复前失败走 case_fail() 直接打 FAILED，而 main.c 之后仍无条件打
 *     SUCCESSFUL ⇒ 同端口两个互斥标记并存。
 *   - session 判据全在 controller 侧的 case（=4 invalid）用 mbox_item_guard
 *     声明结构性守卫，不得冒充行为证据（§1.4）。
 *   - 驱动的收发 fifo 按通道成对分配于共享 SRAM 的 MBOX 区，同一通道
 *     双向可用，故 echo 类应答走原通道。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include <string.h>

#if defined(CONFIG_RTC)
#include <zephyr/drivers/rtc.h>
#endif

#include "mbox_test_protocol.h"
#include "mbox_test_report.h"

LOG_MODULE_REGISTER(mbox_tgt_cases, LOG_LEVEL_INF);

/* consumer 通道 spec。ch0(qspi1) 在本构建中由 flash delegation server
 * 占用（soc_flash_ls.c 注册了回调），扩展 case 不使用该通道。 */
static const struct mbox_dt_spec ch1 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_consumer_otbn_crypto), mbox);
static const struct mbox_dt_spec ch2 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_consumer_espi1), mbox);
static const struct mbox_dt_spec ch3 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_consumer_vuart1), mbox);
static const struct mbox_dt_spec ch4 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch4), mbox);
static const struct mbox_dt_spec ch5 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch5), mbox);

/* 各通道 MTU（= mboxes 的 tx_fifo_width cell，收发同宽） */
#define W_CH1  DT_PHA_BY_IDX(DT_NODELABEL(mbox_consumer_otbn_crypto), mboxes, 0, tx_fifo_width)
#define W_CH2  DT_PHA_BY_IDX(DT_NODELABEL(mbox_consumer_espi1), mboxes, 0, tx_fifo_width)
#define W_CH3  DT_PHA_BY_IDX(DT_NODELABEL(mbox_consumer_vuart1), mboxes, 0, tx_fifo_width)
#define W_CH4  DT_PHA_BY_IDX(DT_NODELABEL(mbox_test_ch4), mboxes, 0, tx_fifo_width)
#define W_CH5  DT_PHA_BY_IDX(DT_NODELABEL(mbox_test_ch5), mboxes, 0, tx_fifo_width)

/* 协议常量必须与板级 dts 一致：FIFO 满 case 的判据依赖 ch4/ch5 深度 */
_Static_assert(MBOX_FIFO_DEPTH ==
	       DT_PHA_BY_IDX(DT_NODELABEL(mbox_test_ch4), mboxes, 0, tx_fifo_depth),
	       "MBOX_FIFO_DEPTH 与 mbox_test_ch4 的 fifo 深度不一致");
_Static_assert(MBOX_TEST_V2_MSG_LEN == W_CH4,
	       "V2 帧长必须等于 ch4 MTU");

/* 暂存槽：驱动侧 rx msg.size 恒为该通道 width，按最大 width（64）开槽 */
#define CASE_RX_SLOT  MBOX_TEST_V2_MSG_LEN

static K_SEM_DEFINE(case_rx_sem, 0, 1);
static uint8_t  case_rx_buf[CASE_RX_SLOT];
static volatile uint32_t case_rx_len;
static volatile uint32_t case_rx_ch;
static volatile uint32_t case_rx_total;
static volatile uint32_t case_rx_drop;

static void case_rx_cb(const struct device *dev, mbox_channel_id_t ch,
		       void *user_data, struct mbox_msg *msg)
{
	/* ISR：只记账 */
	case_rx_total++;
	if (msg == NULL || msg->data == NULL || msg->size > sizeof(case_rx_buf)) {
		case_rx_drop++;
		return;
	}
	memcpy(case_rx_buf, msg->data, msg->size);
	case_rx_len = msg->size;
	case_rx_ch = ch;
	k_sem_give(&case_rx_sem);
}

/* 阻塞等待一条暂存消息；返回 0 = 有消息 */
static int case_wait(uint32_t timeout_ms)
{
	return k_sem_take(&case_rx_sem, K_MSEC(timeout_ms));
}

static void case_take(struct mbox_test_msg_v2 *m)
{
	memset(m, 0, sizeof(*m));
	memcpy(m, case_rx_buf, MIN(case_rx_len, sizeof(*m)));
}

/* 本帧中实际存在的载荷字节数（短通道帧尾部无载荷） */
static uint32_t case_payload_len(void)
{
	return (case_rx_len > MBOX_TEST_V2_HDR_LEN) ?
	       MIN(case_rx_len - MBOX_TEST_V2_HDR_LEN, (uint32_t)MBOX_TEST_V2_DATA_LEN) : 0u;
}

static int case_send(const struct mbox_dt_spec *spec, struct mbox_test_msg_v2 *m, uint32_t size)
{
	struct mbox_msg msg = { .data = m, .size = size };

	return mbox_send_dt(spec, &msg);
}

static void case_setup(const struct mbox_dt_spec *spec)
{
	(void)mbox_register_callback(spec->dev, spec->channel_id, case_rx_cb, NULL);
	(void)mbox_set_enabled(spec->dev, spec->channel_id, true);
}

/* ------------------------------------------------------------------ */
/* case 1：4 KB 分片传输 + CRC32                                        */
/* ------------------------------------------------------------------ */
static void case_big_crc(void)
{
	static uint8_t big[MBOX_BIG_CRC_TOTAL];
	struct mbox_test_msg_v2 m;
	uint32_t got = 0, pattern_err = 0, seq_err = 0, ack_err = 0;
	uint32_t crc_tgt, crc_peer = 0, verdict = MBOX_V2_ST_OK;

	case_setup(&ch4);

	LOG_INF("[TGT] big_crc: expect %u B in %u chunks",
		(unsigned)MBOX_BIG_CRC_TOTAL, (unsigned)MBOX_BIG_CRC_CHUNKS);

	for (;;) {
		if (case_wait(20000) != 0) {
			verdict = MBOX_V2_ST_TIMEOUT;
			break;
		}
		case_take(&m);
		if (m.magic != MBOX_TEST_MAGIC_V2) {
			pattern_err++;
			continue;
		}
		if (m.op == MBOX_V2_OP_DATA) {
			uint32_t off = m.seq * MBOX_TEST_V2_DATA_LEN;
			uint32_t n;
			struct mbox_test_msg_v2 ack;

			if (off >= MBOX_BIG_CRC_TOTAL) {
				seq_err++;
				continue;
			}
			n = MIN((uint32_t)MBOX_TEST_V2_DATA_LEN,
				(uint32_t)MBOX_BIG_CRC_TOTAL - off);
			memcpy(&big[off], m.payload, n);
			for (uint32_t i = 0; i < n; i++) {
				if (big[off + i] != MBOX_BIG_CRC_PATTERN(off + i)) {
					pattern_err++;
				}
			}
			got += n;

			ack.magic = MBOX_TEST_MAGIC_V2;
			ack.op = MBOX_V2_OP_ACK;
			ack.status = (uint8_t)pattern_err;
			ack.seq = m.seq;
			ack.aux = got;
			memset(ack.payload, 0, sizeof(ack.payload));
			if (case_send(&ch5, &ack, W_CH5) != 0) {
				ack_err++;
			}
		} else if (m.op == MBOX_V2_OP_DONE) {
			crc_peer = m.aux;
			break;
		} else {
			pattern_err++;
		}
	}

	crc_tgt = mbox_test_crc32(big, MBOX_BIG_CRC_TOTAL, 0);
	if (got != MBOX_BIG_CRC_TOTAL) {
		verdict = MBOX_V2_ST_SEQ;
	} else if (pattern_err != 0) {
		verdict = MBOX_V2_ST_PATTERN;
	} else if (crc_tgt != crc_peer) {
		verdict = MBOX_V2_ST_CRC;
	}

	/* 汇总上报：aux = 本地 CRC32，payload 前 12 字节 = 接收量/错量 */
	struct mbox_test_msg_v2 stat = {
		.magic = MBOX_TEST_MAGIC_V2,
		.op = MBOX_V2_OP_STAT,
		.status = (uint8_t)verdict,
		.seq = MBOX_BIG_CRC_CHUNKS,
		.aux = crc_tgt,
	};

	memcpy(&stat.payload[0], &got, 4);
	memcpy(&stat.payload[4], &pattern_err, 4);
	memcpy(&stat.payload[8], &ack_err, 4);
	(void)case_send(&ch5, &stat, W_CH5);

	/* ① 端到端完整性：重组字节数 / 逐字节 pattern / 两核 CRC32 */
	if (verdict == MBOX_V2_ST_OK) {
		mbox_item_pass(MBOX_I_BIG_PAYLOAD_CRC, "big_payload_crc",
			       "bytes=%u,recv=%u,pattern_err=%u,seq_err=%u,"
			       "crc=0x%08x,peer_crc=0x%08x",
			       (unsigned)MBOX_BIG_CRC_TOTAL, (unsigned)got,
			       (unsigned)pattern_err, (unsigned)seq_err,
			       (unsigned)crc_tgt, (unsigned)crc_peer);
	} else {
		mbox_item_fail(MBOX_I_BIG_PAYLOAD_CRC, "big_payload_crc",
			       "bytes=%u,recv=%u,pattern_err=%u,seq_err=%u,"
			       "crc=0x%08x,peer_crc=0x%08x,verdict_st=%u",
			       (unsigned)MBOX_BIG_CRC_TOTAL, (unsigned)got,
			       (unsigned)pattern_err, (unsigned)seq_err,
			       (unsigned)crc_tgt, (unsigned)crc_peer,
			       (unsigned)verdict);
	}

	/* ② 逐片 ACK 的回传链路（本核发送侧） */
	if (ack_err == 0 && got == MBOX_BIG_CRC_TOTAL) {
		mbox_item_pass(MBOX_I_BIG_CRC_CHUNK_ACK, "big_crc_chunk_ack",
			       "chunks=%u,ack_err=%u",
			       (unsigned)MBOX_BIG_CRC_CHUNKS, (unsigned)ack_err);
	} else {
		mbox_item_fail(MBOX_I_BIG_CRC_CHUNK_ACK, "big_crc_chunk_ack",
			       "chunks=%u,ack_err=%u,recv=%u",
			       (unsigned)MBOX_BIG_CRC_CHUNKS, (unsigned)ack_err,
			       (unsigned)got);
	}
}

/* ------------------------------------------------------------------ */
/* case 2：1000 条往返，丢包/序号/CRC/延迟统计                          */
/* ------------------------------------------------------------------ */
static void case_stress(void)
{
	struct mbox_test_msg_v2 m;
	uint32_t cnt = 0, seq_err = 0, payload_err = 0, expect = 0;
	uint32_t crc = 0, verdict = MBOX_V2_ST_OK, isr_total, isr_drop;
	bool flow_ok;

	case_setup(&ch4);

	for (;;) {
		if (case_wait(60000) != 0) {
			verdict = MBOX_V2_ST_TIMEOUT;
			break;
		}
		case_take(&m);
		if (m.magic != MBOX_TEST_MAGIC_V2) {
			continue;
		}
		if (m.op == MBOX_V2_OP_DONE) {
			break;
		}
		if (m.op != MBOX_V2_OP_ECHO) {
			continue;
		}

		cnt++;
		if (m.seq != expect) {
			seq_err++;
		}
		expect = m.seq + 1;
		for (uint32_t j = 0; j < MBOX_TEST_V2_DATA_LEN; j++) {
			if (m.payload[j] != (uint8_t)(m.seq + j)) {
				payload_err++;
				break;
			}
		}
		crc = mbox_test_crc32(m.payload, MBOX_TEST_V2_DATA_LEN, crc);

		struct mbox_test_msg_v2 ack = {
			.magic = MBOX_TEST_MAGIC_V2,
			.op = MBOX_V2_OP_ACK,
			.status = (uint8_t)seq_err,
			.seq = m.seq,
			.aux = crc,
		};

		(void)case_send(&ch5, &ack, W_CH5);
	}

	isr_total = case_rx_total;
	isr_drop = case_rx_drop;

	struct mbox_test_msg_v2 stat = {
		.magic = MBOX_TEST_MAGIC_V2,
		.op = MBOX_V2_OP_STAT,
		.status = (uint8_t)((seq_err || payload_err) ? MBOX_V2_ST_SEQ : MBOX_V2_ST_OK),
		.seq = cnt,
		.aux = cnt,
	};

	memcpy(&stat.payload[0], &crc, 4);
	memcpy(&stat.payload[4], &seq_err, 4);
	memcpy(&stat.payload[8], &payload_err, 4);
	memcpy(&stat.payload[12], &isr_total, 4);
	memcpy(&stat.payload[16], &isr_drop, 4);
	(void)case_send(&ch5, &stat, W_CH5);

	flow_ok = (verdict == MBOX_V2_ST_OK) && (seq_err == 0) &&
		  (payload_err == 0) && (isr_drop == 0) &&
		  (cnt == MBOX_STRESS_COUNT);
	if (flow_ok) {
		mbox_item_pass(MBOX_I_STRESS_N_MESSAGES, "stress_n_messages",
			       "recv=%u,expect=%u,seq_err=%u,payload_err=%u,"
			       "isr_total=%u,isr_drop=%u,crc=0x%08x",
			       (unsigned)cnt, (unsigned)MBOX_STRESS_COUNT,
			       (unsigned)seq_err, (unsigned)payload_err,
			       (unsigned)isr_total, (unsigned)isr_drop,
			       (unsigned)crc);
	} else {
		mbox_item_fail(MBOX_I_STRESS_N_MESSAGES, "stress_n_messages",
			       "recv=%u,expect=%u,seq_err=%u,payload_err=%u,"
			       "isr_total=%u,isr_drop=%u,crc=0x%08x,verdict_st=%u",
			       (unsigned)cnt, (unsigned)MBOX_STRESS_COUNT,
			       (unsigned)seq_err, (unsigned)payload_err,
			       (unsigned)isr_total, (unsigned)isr_drop,
			       (unsigned)crc, (unsigned)verdict);
	}
}

/* ------------------------------------------------------------------ */
/* case 3：consumer 通道通路（ch1..ch5；ch0 归 flash delegation）        */
/* ------------------------------------------------------------------ */
static const struct mbox_dt_spec *probe_ch[] = { &ch1, &ch2, &ch3, &ch4 };
static const uint32_t probe_w[] = { W_CH1, W_CH2, W_CH3, W_CH4 };
static const char *probe_name[] = {
	"consumer_ch1_roundtrip", "consumer_ch2_roundtrip",
	"consumer_ch3_roundtrip", "consumer_ch4_roundtrip"
};
static const enum mbox_item probe_item[] = {
	MBOX_I_CONSUMER_CH1, MBOX_I_CONSUMER_CH2, MBOX_I_CONSUMER_CH3, MBOX_I_CONSUMER_CH4
};
#define PROBE_N ARRAY_SIZE(probe_ch)

static void case_consumers(void)
{
	struct mbox_test_msg_v2 m;
	uint32_t ok = 0, bad = 0;
	uint32_t short_frame_len = 0;
	bool probe_ok[PROBE_N];
	bool short_ok;

	for (uint32_t i = 0; i < PROBE_N; i++) {
		case_setup(probe_ch[i]);
		probe_ok[i] = false;
	}
	case_setup(&ch5);

	for (uint32_t i = 0; i < PROBE_N; i++) {
		struct mbox_test_msg_v2 ack;
		bool this_ok = false;
		bool payload_ok = true;

		if (case_wait(5000) != 0) {
			continue;
		}
		case_take(&m);
		if (m.magic == MBOX_TEST_MAGIC_V2 && m.op == MBOX_V2_OP_ECHO &&
		    m.seq == i && case_rx_ch == probe_ch[i]->channel_id &&
		    case_rx_len == probe_w[i]) {
			/* 载荷区只在通道 width > V2 头部长度时存在 */
			for (uint32_t j = 0; j < case_payload_len(); j++) {
				if (m.payload[j] != (uint8_t)(0xA0 + i)) {
					payload_ok = false;
					break;
				}
			}
			this_ok = payload_ok;
		}

		ack.magic = MBOX_TEST_MAGIC_V2;
		ack.op = MBOX_V2_OP_ACK;
		ack.status = this_ok ? MBOX_V2_ST_OK : MBOX_V2_ST_ARG;
		ack.seq = i;
		ack.aux = case_rx_ch;
		memset(ack.payload, 0, sizeof(ack.payload));
		if (case_send(probe_ch[i], &ack, probe_w[i]) == 0 && this_ok) {
			probe_ok[i] = true;
			ok++;
		} else {
			bad++;
		}
	}

	/* ch5 短帧：controller 只发 8 B，驱动 rx size 恒报通道 width(64) */
	if (case_wait(5000) == 0) {
		struct mbox_test_msg_v2 ack;

		case_take(&m);
		short_frame_len = case_rx_len;
		short_ok = (m.op == MBOX_V2_OP_ECHO) && (case_rx_len == W_CH5);
		ack.magic = MBOX_TEST_MAGIC_V2;
		ack.op = MBOX_V2_OP_ACK;
		ack.status = short_ok ? MBOX_V2_ST_OK : MBOX_V2_ST_ARG;
		ack.seq = m.seq;
		ack.aux = case_rx_len;
		memset(ack.payload, 0, sizeof(ack.payload));
		(void)case_send(&ch5, &ack, W_CH5);
	} else {
		short_ok = false;
		bad++;
	}

	/* ① 每个 consumer 通道各自一项 */
	for (uint32_t i = 0; i < PROBE_N; i++) {
		if (probe_ok[i]) {
			mbox_item_pass(probe_item[i], probe_name[i],
				       "ch=%u,size=%u",
				       (unsigned)probe_ch[i]->channel_id,
				       (unsigned)probe_w[i]);
		} else {
			mbox_item_fail(probe_item[i], probe_name[i],
				       "ch=%u,size=%u",
				       (unsigned)probe_ch[i]->channel_id,
				       (unsigned)probe_w[i]);
		}
	}

	/* ② 同一通道双向：ACK 必须由收到的原通道发回（本核 TX/RX region 对偶） */
	if (ok == PROBE_N && bad == 0) {
		mbox_item_pass(MBOX_I_CONSUMER_BIDIR, "consumer_same_channel_bidir",
			       "bidir_ok=%u/%u", (unsigned)ok, (unsigned)PROBE_N);
	} else {
		mbox_item_fail(MBOX_I_CONSUMER_BIDIR, "consumer_same_channel_bidir",
			       "bidir_ok=%u/%u,bad=%u", (unsigned)ok,
			       (unsigned)PROBE_N, (unsigned)bad);
	}

	/* ③ ch5 短帧契约：对端发 8 B，本端 rx size 仍报通道 width(64) */
	if (short_ok) {
		mbox_item_pass(MBOX_I_CONSUMER_CH5_SHORT, "consumer_ch5_short_frame",
			       "rx_len=%u,expect=%u",
			       (unsigned)short_frame_len, (unsigned)W_CH5);
	} else {
		mbox_item_fail(MBOX_I_CONSUMER_CH5_SHORT, "consumer_ch5_short_frame",
			       "rx_len=%u,expect=%u",
			       (unsigned)short_frame_len, (unsigned)W_CH5);
	}
}

/* ------------------------------------------------------------------ */
/* case 4：异常参数（判据在 controller 侧，target 仅待机）               */
/* ------------------------------------------------------------------ */
static void case_invalid(void)
{
	/* 本 case 的判据（非法入参返回值）全部在 controller 侧，target 侧
	 * 没有可校验的行为 —— 用 guard 显式声明"本项不是行为证据"，避免它
	 * 继续冒充"双核行为验证"的一半（§1.4）。 */
	LOG_INF("[TGT] invalid: 判据在 controller 侧，target 待机");
	k_msleep(4000);
	mbox_item_guard(MBOX_I_STRUCT_GUARD, "tgt_invalid_idle", "idle_ms=4000");
}

/* ------------------------------------------------------------------ */
/* case 5：RTC 双核时间同步                                            */
/* ------------------------------------------------------------------ */
static void case_rtc_sync(void)
{
#if defined(CONFIG_RTC)
	const struct device *rtc = DEVICE_DT_GET(DT_NODELABEL(rtc));
	struct rtc_time tm;
	struct mbox_test_msg_v2 m, ack;
	uint32_t e1 = 0, e2 = 0;
	int32_t advance = -1;
	uint32_t bad = 0;

	case_setup(&ch4);

	if (!device_is_ready(rtc)) {
		mbox_mark_fail("rtc device not ready");
		return;
	}

	/* 第 1 轮：应答 controller 的时间戳请求 */
	if (case_wait(20000) != 0) {
		mbox_mark_fail("rtc sync round1 超时");
		return;
	}
	case_take(&m);
	if (rtc_get_time(rtc, &tm) != 0) {
		bad++;
	} else {
		e1 = mbox_test_epoch(tm.tm_year + 1900, tm.tm_mon, tm.tm_mday,
				     tm.tm_hour, tm.tm_min, tm.tm_sec);
	}

	/* 走时推进：隔 300 ms 再读一次，本地 RTC 应前进 0..3 s */
	k_msleep(300);
	if (rtc_get_time(rtc, &tm) != 0) {
		bad++;
	} else {
		e2 = mbox_test_epoch(tm.tm_year + 1900, tm.tm_mon, tm.tm_mday,
				     tm.tm_hour, tm.tm_min, tm.tm_sec);
		advance = (int32_t)e2 - (int32_t)e1;
	}

	memset(&ack, 0, sizeof(ack));
	ack.magic = MBOX_TEST_MAGIC_V2;
	ack.op = MBOX_V2_OP_TS;
	ack.status = (uint8_t)bad;
	ack.seq = m.seq;
	ack.aux = e1;
	memcpy(&ack.payload[0], &advance, 4);
	(void)case_send(&ch5, &ack, W_CH5);

	/* 第 2 轮：供 controller 判单调 */
	if (case_wait(10000) == 0) {
		case_take(&m);
		if (rtc_get_time(rtc, &tm) != 0) {
			bad++;
		} else {
			e2 = mbox_test_epoch(tm.tm_year + 1900, tm.tm_mon, tm.tm_mday,
					     tm.tm_hour, tm.tm_min, tm.tm_sec);
		}
		ack.seq = m.seq;
		ack.aux = e2;
		ack.status = (uint8_t)bad;
		memset(ack.payload, 0, sizeof(ack.payload));
		(void)case_send(&ch5, &ack, W_CH5);
	} else {
		bad++;
	}

	/* RTC 1 Hz 计数：300 ms 内前进量 ∈ [0, 3] s（见 COVERAGE §3.6） */
	if (bad == 0 && advance >= 0 && advance <= 3) {
		mbox_item_pass(MBOX_I_RTC_READ_ADVANCES, "rtc_read_advances",
			       "e1=%u,e2=%u,advance_s=%d,source=local",
			       (unsigned)e1, (unsigned)e2, (int)advance);
	} else {
		mbox_item_fail(MBOX_I_RTC_READ_ADVANCES, "rtc_read_advances",
			       "e1=%u,e2=%u,advance_s=%d,bad=%u,source=local",
			       (unsigned)e1, (unsigned)e2, (int)advance,
			       (unsigned)bad);
	}
#else
	LOG_ERR("[TGT] rtc_sync 需要 CONFIG_RTC=y");
	mbox_mark_fail("CONFIG_RTC 未启用");
#endif
}

/* ------------------------------------------------------------------ */
/* case 6：屏蔽 IRQ 期间 FIFO 满 → -ENOSPC，再使能后补排                */
/* ------------------------------------------------------------------ */
static void case_fifo_full(void)
{
	uint32_t drained;

	case_setup(&ch4);

	/* 屏蔽本核 mbox IRQ：窗口内 ch4 的 RX fifo 不被排空，
	 * controller 的第 depth+1 次发送必然命中驱动的 -ENOSPC 分支。 */
	(void)mbox_set_enabled(ch4.dev, ch4.channel_id, false);
	LOG_INF("[TGT] fifo_full: IRQ masked, waiting controller burst");
	k_msleep(800);

	/* 重新使能：驱动在 set_enabled(true) 内补排已入队消息（回调被唤起） */
	(void)mbox_set_enabled(ch4.dev, ch4.channel_id, true);
	k_msleep(100);

	drained = case_rx_total;

	if (drained == MBOX_FIFO_DEPTH && case_rx_drop == 0) {
		mbox_item_pass(MBOX_I_FIFO_FULL_DRAIN, "fifo_full_drain_on_enable",
			       "drained=%u,drop=%u,expect=%u",
			       (unsigned)drained, (unsigned)case_rx_drop,
			       (unsigned)MBOX_FIFO_DEPTH);
	} else {
		mbox_item_fail(MBOX_I_FIFO_FULL_DRAIN, "fifo_full_drain_on_enable",
			       "drained=%u,drop=%u,expect=%u",
			       (unsigned)drained, (unsigned)case_rx_drop,
			       (unsigned)MBOX_FIFO_DEPTH);
	}
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */
void mbox_cases_target_main(void)
{
	LOG_INF("[TGT] extended mbox case id=%d", CONFIG_MBOX_TEST_CASE_ID);

	switch (CONFIG_MBOX_TEST_CASE_ID) {
	case MBOX_CASE_BIG_CRC:
		case_big_crc();
		break;
	case MBOX_CASE_STRESS:
		case_stress();
		break;
	case MBOX_CASE_CONSUMERS:
		case_consumers();
		break;
	case MBOX_CASE_INVALID:
		case_invalid();
		break;
	case MBOX_CASE_RTC_SYNC:
		case_rtc_sync();
		break;
	case MBOX_CASE_FIFO_FULL:
		case_fifo_full();
		break;
	default:
		LOG_ERR("[TGT] unknown case id %d", CONFIG_MBOX_TEST_CASE_ID);
		mbox_mark_fail("unknown MBOX_TEST_CASE_ID");
		break;
	}

	LOG_INF("[TGT] extended case done");
}
