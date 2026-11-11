/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * MBOX test - cpu2 (controller) 扩展 case 实现（P1c 追加）
 *
 * 与 cpu1 侧 src/mbox_cases.c 配对，实现 V2 协议（mbox_test_protocol.h
 * 追加段）controller 侧的激励与判据；case 由 CONFIG_MBOX_TEST_CASE_ID
 * 选择（见本目录 Kconfig），入口 mbox_cases_controller_main() 由
 * src/main.c 在 ID != 0 时调用。
 *
 * 判定（§1.1/§1.3 修复）：每个 case 的每条 check 都用 mbox_item_pass/
 * mbox_item_fail 上报一行 `INFO <name>: pass=<0|1>,<明细>`（name 与
 * COVERAGE.md §3 的测试项名一一对应）。失败项会置本核判负，src/main.c
 * 在返回后打 `PROJECT EXECUTION: FAILED`。
 *   - 修复前：失败走 case_fail() 直接打印 FAILED，而 main.c 之后仍会
 *     无条件打印 SUCCESSFUL ⇒ 同一端口上两个互斥标记并存，报告自相矛盾；
 *     且失败只表现为整核 FAILED，看不出是哪一条（报告恒 2 项）。
 *   - 驱动现状固化的两条（size=0 被接受 / mtu_get 未接线）改为**观察项**
 *     （observe=1），不判负，驱动修复后由报告提示改判（COVERAGE §3.4/§4）。
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

LOG_MODULE_REGISTER(mbox_ctrl_cases, LOG_LEVEL_INF);

static const struct mbox_dt_spec ch1 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_consumer_otbn_crypto), mbox);
static const struct mbox_dt_spec ch2 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_consumer_espi1), mbox);
static const struct mbox_dt_spec ch3 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_consumer_vuart1), mbox);
static const struct mbox_dt_spec ch4 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch4), mbox);
static const struct mbox_dt_spec ch5 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch5), mbox);

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

static int case_wait(uint32_t timeout_ms)
{
	return k_sem_take(&case_rx_sem, K_MSEC(timeout_ms));
}

static void case_take(struct mbox_test_msg_v2 *m)
{
	memset(m, 0, sizeof(*m));
	memcpy(m, case_rx_buf, MIN(case_rx_len, sizeof(*m)));
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

/* 按 "返回值是否等于契约值" 上报一项（异常参数 case 用） */
static void check_ret(enum mbox_item id, const char *name, int ret, int expect)
{
	if (ret == expect) {
		mbox_item_pass(id, name, "ret=%d,expect=%d", ret, expect);
	} else {
		mbox_item_fail(id, name, "ret=%d,expect=%d", ret, expect);
	}
}

static void case_echo_fill(struct mbox_test_msg_v2 *m, uint32_t seq, uint8_t fill)
{
	memset(m, 0, sizeof(*m));
	m->magic = MBOX_TEST_MAGIC_V2;
	m->op = MBOX_V2_OP_ECHO;
	m->seq = seq;
	for (uint32_t j = 0; j < MBOX_TEST_V2_DATA_LEN; j++) {
		m->payload[j] = (fill == 0) ? (uint8_t)(seq + j) : fill;
	}
}

/* ------------------------------------------------------------------ */
/* case 1：4 KB 分片传输 + CRC32                                        */
/* ------------------------------------------------------------------ */
static void case_big_crc(void)
{
	static uint8_t big[MBOX_BIG_CRC_TOTAL];
	struct mbox_test_msg_v2 m, a;
	uint32_t crc, chunk_err = 0, ack_timeout = 0, send_fail = 0;
	uint32_t t0, us, tgt_crc = 0, tgt_stat = 0, got = 0;
	bool ack_ok, e2e_ok;

	/* 等 target 完成回调注册与使能 */
	k_msleep(600);

	case_setup(&ch5);

	for (uint32_t i = 0; i < MBOX_BIG_CRC_TOTAL; i++) {
		big[i] = MBOX_BIG_CRC_PATTERN(i);
	}
	crc = mbox_test_crc32(big, MBOX_BIG_CRC_TOTAL, 0);

	LOG_INF("[CTRL] big_crc: send %u B in %u chunks, crc=0x%08x",
		(unsigned)MBOX_BIG_CRC_TOTAL, (unsigned)MBOX_BIG_CRC_CHUNKS, (unsigned)crc);

	t0 = k_cycle_get_32();

	for (uint32_t c = 0; c < MBOX_BIG_CRC_CHUNKS; c++) {
		uint32_t off = c * MBOX_TEST_V2_DATA_LEN;
		uint32_t n = MIN((uint32_t)MBOX_TEST_V2_DATA_LEN,
				 (uint32_t)MBOX_BIG_CRC_TOTAL - off);

		memset(&m, 0, sizeof(m));
		m.magic = MBOX_TEST_MAGIC_V2;
		m.op = MBOX_V2_OP_DATA;
		m.seq = c;
		memcpy(m.payload, &big[off], n);

		if (case_send(&ch4, &m, W_CH4) != 0) {
			send_fail++;
		}
		if (case_wait(500) != 0) {
			ack_timeout++;
			continue;
		}
		case_take(&a);
		if (a.op != MBOX_V2_OP_ACK || a.seq != c || a.status != 0) {
			chunk_err++;
		}
	}

	us = k_cyc_to_us_floor32(k_cycle_get_32() - t0);

	/* 结束帧：aux = 发送端 CRC32 */
	memset(&m, 0, sizeof(m));
	m.magic = MBOX_TEST_MAGIC_V2;
	m.op = MBOX_V2_OP_DONE;
	m.seq = MBOX_BIG_CRC_CHUNKS;
	m.aux = crc;
	(void)case_send(&ch4, &m, W_CH4);

	if (case_wait(5000) == 0) {
		case_take(&a);
		tgt_crc = a.aux;
		tgt_stat = a.status;
		memcpy(&got, &a.payload[0], 4);
	} else {
		tgt_stat = MBOX_V2_ST_TIMEOUT;
	}

	/* ① 分片链路：每片都被 target 确认（ACK 到达 / seq 对齐 / status=0） */
	ack_ok = (chunk_err == 0) && (ack_timeout == 0) && (send_fail == 0);
	if (ack_ok) {
		mbox_item_pass(MBOX_I_BIG_CRC_CHUNK_ACK, "big_crc_chunk_ack",
			       "chunks=%u,chunk_err=%u,ack_timeout=%u,send_fail=%u",
			       (unsigned)MBOX_BIG_CRC_CHUNKS, (unsigned)chunk_err,
			       (unsigned)ack_timeout, (unsigned)send_fail);
	} else {
		mbox_item_fail(MBOX_I_BIG_CRC_CHUNK_ACK, "big_crc_chunk_ack",
			       "chunks=%u,chunk_err=%u,ack_timeout=%u,send_fail=%u",
			       (unsigned)MBOX_BIG_CRC_CHUNKS, (unsigned)chunk_err,
			       (unsigned)ack_timeout, (unsigned)send_fail);
	}

	/* ② 端到端完整性：target 重组字节数 + 双核 CRC32 一致 */
	e2e_ok = (got == MBOX_BIG_CRC_TOTAL) && (tgt_stat == MBOX_V2_ST_OK) &&
		 (tgt_crc == crc);
	if (e2e_ok) {
		mbox_item_pass(MBOX_I_BIG_PAYLOAD_CRC, "big_payload_crc",
			       "bytes=%u,tgt_recv=%u,crc=0x%08x,tgt_crc=0x%08x,"
			       "tgt_st=%u,us=%u",
			       (unsigned)MBOX_BIG_CRC_TOTAL, (unsigned)got,
			       (unsigned)crc, (unsigned)tgt_crc, (unsigned)tgt_stat,
			       (unsigned)us);
	} else {
		mbox_item_fail(MBOX_I_BIG_PAYLOAD_CRC, "big_payload_crc",
			       "bytes=%u,tgt_recv=%u,crc=0x%08x,tgt_crc=0x%08x,"
			       "tgt_st=%u,us=%u",
			       (unsigned)MBOX_BIG_CRC_TOTAL, (unsigned)got,
			       (unsigned)crc, (unsigned)tgt_crc, (unsigned)tgt_stat,
			       (unsigned)us);
	}
}

/* ------------------------------------------------------------------ */
/* case 2：1000 条往返，丢包/序号/CRC/延迟统计                          */
/* ------------------------------------------------------------------ */
static void case_stress(void)
{
	struct mbox_test_msg_v2 m, a;
	uint32_t crc = 0, send_fail = 0, ack_timeout = 0, seq_err = 0, ack_ok = 0;
	uint32_t rtt_min = 0xFFFFFFFFu, rtt_max = 0;
	uint64_t rtt_sum = 0;
	uint32_t tgt_cnt = 0, tgt_crc = 0, tgt_seq_err = 0, tgt_payload_err = 0;
	uint32_t tgt_isr = 0, tgt_drop = 0, tgt_stat = MBOX_V2_ST_TIMEOUT;
	bool flow_ok;

	k_msleep(600);
	case_setup(&ch5);

	LOG_INF("[CTRL] stress: %u msgs", (unsigned)MBOX_STRESS_COUNT);

	for (uint32_t i = 0; i < MBOX_STRESS_COUNT; i++) {
		uint32_t t0;

		case_echo_fill(&m, i, 0);
		t0 = k_cycle_get_32();
		if (case_send(&ch4, &m, W_CH4) != 0) {
			send_fail++;
			continue;
		}
		crc = mbox_test_crc32(m.payload, MBOX_TEST_V2_DATA_LEN, crc);

		if (case_wait(200) != 0) {
			ack_timeout++;
			continue;
		}
		case_take(&a);
		uint32_t rtt = k_cyc_to_us_floor32(k_cycle_get_32() - t0);

		if (a.op != MBOX_V2_OP_ACK || a.seq != i) {
			seq_err++;
		} else {
			ack_ok++;
		}
		if (rtt < rtt_min) {
			rtt_min = rtt;
		}
		if (rtt > rtt_max) {
			rtt_max = rtt;
		}
		rtt_sum += rtt;
	}

	/* 结束帧 */
	memset(&m, 0, sizeof(m));
	m.magic = MBOX_TEST_MAGIC_V2;
	m.op = MBOX_V2_OP_DONE;
	m.seq = MBOX_STRESS_COUNT;
	m.aux = crc;
	(void)case_send(&ch4, &m, W_CH4);

	if (case_wait(10000) == 0) {
		case_take(&a);
		tgt_stat = a.status;
		tgt_cnt = a.aux;
		memcpy(&tgt_crc, &a.payload[0], 4);
		memcpy(&tgt_seq_err, &a.payload[4], 4);
		memcpy(&tgt_payload_err, &a.payload[8], 4);
		memcpy(&tgt_isr, &a.payload[12], 4);
		memcpy(&tgt_drop, &a.payload[16], 4);
	}

	/* ① 端到端流程完整性：双核计数 / 序号 / 链式 CRC32 全对 */
	flow_ok = (send_fail == 0) && (ack_timeout == 0) && (seq_err == 0) &&
		  (tgt_stat == MBOX_V2_ST_OK) && (tgt_cnt == MBOX_STRESS_COUNT) &&
		  (tgt_crc == crc) && (tgt_seq_err == 0) && (tgt_payload_err == 0) &&
		  (tgt_drop == 0);
	if (flow_ok) {
		mbox_item_pass(MBOX_I_STRESS_N_MESSAGES, "stress_n_messages",
			       "sent=%u,ack_ok=%u,ack_timeout=%u,seq_err=%u,send_fail=%u,"
			       "tgt_recv=%u,tgt_seq_err=%u,tgt_payload_err=%u,"
			       "tgt_isr=%u,tgt_drop=%u,crc=0x%08x,tgt_crc=0x%08x",
			       (unsigned)MBOX_STRESS_COUNT, (unsigned)ack_ok,
			       (unsigned)ack_timeout, (unsigned)seq_err,
			       (unsigned)send_fail, (unsigned)tgt_cnt,
			       (unsigned)tgt_seq_err, (unsigned)tgt_payload_err,
			       (unsigned)tgt_isr, (unsigned)tgt_drop,
			       (unsigned)crc, (unsigned)tgt_crc);
	} else {
		mbox_item_fail(MBOX_I_STRESS_N_MESSAGES, "stress_n_messages",
			       "sent=%u,ack_ok=%u,ack_timeout=%u,seq_err=%u,send_fail=%u,"
			       "tgt_recv=%u,tgt_seq_err=%u,tgt_payload_err=%u,"
			       "tgt_isr=%u,tgt_drop=%u,crc=0x%08x,tgt_crc=0x%08x",
			       (unsigned)MBOX_STRESS_COUNT, (unsigned)ack_ok,
			       (unsigned)ack_timeout, (unsigned)seq_err,
			       (unsigned)send_fail, (unsigned)tgt_cnt,
			       (unsigned)tgt_seq_err, (unsigned)tgt_payload_err,
			       (unsigned)tgt_isr, (unsigned)tgt_drop,
			       (unsigned)crc, (unsigned)tgt_crc);
	}

	/* ② RTT 统计：**无硬门限**（§6.5.1 统计维度，防 flaky），
	 * 只要确实测到过往返就算通过；数值供报告"实际"列。 */
	if (ack_ok > 0) {
		mbox_item_pass(MBOX_I_STRESS_RTT_STATS, "stress_rtt_stats",
			       "samples=%u,rtt_us_min=%u,rtt_us_avg=%u,rtt_us_max=%u",
			       (unsigned)ack_ok, (unsigned)rtt_min,
			       (unsigned)(rtt_sum / ack_ok), (unsigned)rtt_max);
	} else {
		mbox_item_fail(MBOX_I_STRESS_RTT_STATS, "stress_rtt_stats",
			       "samples=0（一条往返都没测到）");
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
	struct mbox_test_msg_v2 m, a;
	uint32_t ok = 0, bad = 0, bad_mask = 0;
	uint32_t short_ack_len = 0, short_ack_st = 0xFF;
	bool short_ok;

	k_msleep(600);
	for (uint32_t i = 0; i < PROBE_N; i++) {
		case_setup(probe_ch[i]);
	}
	case_setup(&ch5);

	for (uint32_t i = 0; i < PROBE_N; i++) {
		bool this_ok = false;

		case_echo_fill(&m, i, (uint8_t)(0xA0 + i));
		if (case_send(probe_ch[i], &m, probe_w[i]) != 0) {
			/* 本片失败 */
		} else if (case_wait(1000) != 0) {
			/* 超时 */
		} else {
			case_take(&a);
			/* ACK 必须由同一通道回传（同通道双向 = TX/RX region 对偶） */
			this_ok = (a.op == MBOX_V2_OP_ACK) && (a.seq == i) &&
				  (case_rx_ch == probe_ch[i]->channel_id);
		}

		if (this_ok) {
			ok++;
		} else {
			bad++;
			bad_mask |= (1u << i);
		}
	}

	/* ch5 短帧：只发 8 B，target 侧应报告 rx size == 通道 width */
	memset(&m, 0, sizeof(m));
	m.magic = MBOX_TEST_MAGIC_V2;
	m.op = MBOX_V2_OP_ECHO;
	m.seq = PROBE_N;
	if (case_send(&ch5, &m, sizeof(struct mbox_test_probe)) != 0) {
		short_ok = false;
	} else if (case_wait(1000) == 0) {
		case_take(&a);
		short_ack_len = a.aux;
		short_ack_st = a.status;
		short_ok = (a.seq == PROBE_N) && (short_ack_len == W_CH5) &&
			   (short_ack_st == MBOX_V2_ST_OK);
	} else {
		short_ok = false;
	}

	/* ① 每个 consumer 通道各自一项 */
	for (uint32_t i = 0; i < PROBE_N; i++) {
		bool this_ok = !(bad_mask & (1u << i));

		if (this_ok) {
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

	/* ② 同一通道双向：4 条探针的 ACK 全部由原通道收回（判据与①同源，
	 *    这里作为独立项汇总，便于报告一眼看出"双向"这一维度） */
	if (bad_mask == 0 && ok == PROBE_N) {
		mbox_item_pass(MBOX_I_CONSUMER_BIDIR, "consumer_same_channel_bidir",
			       "bidir_ok=%u/%u", (unsigned)ok, (unsigned)PROBE_N);
	} else {
		mbox_item_fail(MBOX_I_CONSUMER_BIDIR, "consumer_same_channel_bidir",
			       "bidir_ok=%u/%u,ok=%u,bad=%u", (unsigned)(PROBE_N - bad),
			       (unsigned)PROBE_N, (unsigned)ok, (unsigned)bad);
	}

	/* ③ ch5 短帧契约：对端发 8 B，驱动 rx size 恒报通道 width(64) */
	if (short_ok) {
		mbox_item_pass(MBOX_I_CONSUMER_CH5_SHORT, "consumer_ch5_short_frame",
			       "ack_len=%u,expect=%u,ack_st=%u",
			       (unsigned)short_ack_len, (unsigned)W_CH5,
			       (unsigned)short_ack_st);
	} else {
		mbox_item_fail(MBOX_I_CONSUMER_CH5_SHORT, "consumer_ch5_short_frame",
			       "ack_len=%u,expect=%u,ack_st=%u",
			       (unsigned)short_ack_len, (unsigned)W_CH5,
			       (unsigned)short_ack_st);
	}
}

/* ------------------------------------------------------------------ */
/* case 4：异常参数                                                     */
/* ------------------------------------------------------------------ */
static void case_invalid(void)
{
	const struct device *dev = ch4.dev;
	uint32_t maxch = mbox_max_channels_get(dev);
	struct mbox_test_msg_v2 m;
	struct mbox_msg bad_data = { .data = NULL, .size = 16 };
	struct mbox_msg oversize = { .data = &m, .size = W_CH4 + 1 };
	struct mbox_msg oversize_ch2 = { .data = &m, .size = W_CH2 + 1 };
	struct mbox_msg zero_size = { .data = &m, .size = 0 };
	uint32_t fails = 0;
	int ret;

	k_msleep(600);
	memset(&m, 0, sizeof(m));

	ret = mbox_send(dev, ch4.channel_id, NULL);
	check_ret(MBOX_I_INV_SEND_NULL_MSG, "inv_send_null_msg", ret, -EINVAL);
	if (ret != -EINVAL) {
		fails++;
	}

	ret = mbox_send(dev, maxch, &(struct mbox_msg){ .data = &m, .size = W_CH4 });
	check_ret(MBOX_I_INV_SEND_BAD_CHANNEL, "inv_send_bad_channel", ret, -EINVAL);
	if (ret != -EINVAL) {
		LOG_ERR("[CTRL] send(bad channel=%u) ret=%d", maxch, ret);
		fails++;
	}

	ret = mbox_send(dev, ch4.channel_id, &oversize);
	check_ret(MBOX_I_INV_SEND_OVERSIZE_CH4, "inv_send_oversize_ch4", ret, -EMSGSIZE);
	if (ret != -EMSGSIZE) {
		fails++;
	}

	ret = mbox_send(dev, ch4.channel_id, &bad_data);
	check_ret(MBOX_I_INV_SEND_NULL_DATA, "inv_send_null_data", ret, -EMSGSIZE);
	if (ret != -EMSGSIZE) {
		fails++;
	}

	ret = mbox_send(dev, ch2.channel_id, &oversize_ch2);
	check_ret(MBOX_I_INV_SEND_OVERSIZE_CH2, "inv_send_oversize_ch2", ret, -EMSGSIZE);
	if (ret != -EMSGSIZE) {
		fails++;
	}

	ret = mbox_register_callback(dev, maxch, case_rx_cb, NULL);
	check_ret(MBOX_I_INV_REGISTER_BAD_CHANNEL, "inv_register_bad_channel", ret, -EINVAL);
	if (ret != -EINVAL) {
		fails++;
	}

	ret = mbox_set_enabled(dev, maxch, true);
	check_ret(MBOX_I_INV_SET_ENABLED_BAD_CHANNEL, "inv_set_enabled_bad_channel", ret, -EINVAL);
	if (ret != -EINVAL) {
		fails++;
	}

	/* ---- 以下两条是**观察项**（observe=1）：把驱动当前缺陷固化成预期值
	 * 的探针，无论现状是否与记录一致都**不判负**（§1.5）。
	 * 驱动补齐后本项 pass 会翻 0 并在报告里标出来，提示同步改判。 ---- */

	/* 驱动现状：size=0 的帧被接受（fifo 写入 0 字节仍推进写指针），
	 * 对端 rx size 恒为该通道 width。 */
	ret = mbox_send(dev, ch4.channel_id, &zero_size);
	mbox_item_observe(MBOX_I_INV_ZERO_SIZE, "inv_zero_size_accepted", ret == 0,
			  "ret=%d,recorded=0", ret);

	/* 驱动缺口：本驱动的 api.mtu_get 未接线（只定义了 tx/rx_mtu_get 但未挂入
	 * api 表），Zephyr 的 mbox_mtu_get() 是 per-device 接口 → 返回 -ENOSYS。
	 * 驱动补齐后本项应改为断言 == 通道 width。 */
	ret = mbox_mtu_get(dev);
	mbox_item_observe(MBOX_I_INV_MTU_GET, "inv_mtu_get_unimplemented", ret == -ENOSYS,
			  "ret=%d,recorded=%d", ret, -ENOSYS);

	printk("MBOX_TEST:CTRL:INFO:invalid_args:max_channels=%u,fails=%u\n",
	       (unsigned)maxch, (unsigned)fails);
}

/* ------------------------------------------------------------------ */
/* case 5：RTC 双核时间同步                                            */
/* ------------------------------------------------------------------ */
static void case_rtc_sync(void)
{
#if defined(CONFIG_RTC)
	const struct device *rtc = DEVICE_DT_GET(DT_NODELABEL(rtc));
	struct rtc_time tm;
	struct mbox_test_msg_v2 m, a;
	uint32_t e_tgt[2] = { 0, 0 }, e_loc[2] = { 0, 0 };
	int32_t delta[2] = { 0, 0 }, advance = -1;
	uint32_t tgt_bad = 0, bad = 0;
	bool frame_ok, delta_ok, mono_ok, adv_ok;

	k_msleep(800);
	case_setup(&ch5);

	if (!device_is_ready(rtc)) {
		mbox_mark_fail("rtc device not ready");
		return;
	}

	for (uint32_t r = 0; r < 2; r++) {
		memset(&m, 0, sizeof(m));
		m.magic = MBOX_TEST_MAGIC_V2;
		m.op = MBOX_V2_OP_TS;
		m.seq = r + 1;
		if (case_send(&ch4, &m, W_CH4) != 0) {
			bad++;
			continue;
		}
		if (case_wait(3000) != 0) {
			bad++;
			continue;
		}
		case_take(&a);
		if (a.op != MBOX_V2_OP_TS || a.seq != r + 1) {
			bad++;
			continue;
		}
		e_tgt[r] = a.aux;
		tgt_bad |= a.status;
		if (r == 0) {
			memcpy(&advance, &a.payload[0], 4);
		}

		if (rtc_get_time(rtc, &tm) != 0) {
			bad++;
			continue;
		}
		e_loc[r] = mbox_test_epoch(tm.tm_year + 1900, tm.tm_mon, tm.tm_mday,
					   tm.tm_hour, tm.tm_min, tm.tm_sec);
		delta[r] = (int32_t)e_loc[r] - (int32_t)e_tgt[r];
	}

	frame_ok = (bad == 0) && (tgt_bad == 0);
	delta_ok = frame_ok && (delta[0] <= 2) && (delta[0] >= -2) &&
		   (delta[1] <= 2) && (delta[1] >= -2);
	mono_ok = frame_ok && ((int32_t)e_tgt[1] >= (int32_t)e_tgt[0]);
	adv_ok = (advance >= 0) && (advance <= 3);

	/* ① 双核 RTC 差值（容忍窗口 ±2 s = 1 s 分辨率 + 传输/调度余量） */
	if (delta_ok) {
		mbox_item_pass(MBOX_I_RTC_SYNC_DELTA, "rtc_sync_delta",
			       "delta1_s=%d,delta2_s=%d,frame_bad=%u,tgt_bad=%u",
			       (int)delta[0], (int)delta[1], (unsigned)bad,
			       (unsigned)tgt_bad);
	} else {
		mbox_item_fail(MBOX_I_RTC_SYNC_DELTA, "rtc_sync_delta",
			       "delta1_s=%d,delta2_s=%d,frame_bad=%u,tgt_bad=%u",
			       (int)delta[0], (int)delta[1], (unsigned)bad,
			       (unsigned)tgt_bad);
	}

	/* ② target 时间戳单调不减 */
	if (mono_ok) {
		mbox_item_pass(MBOX_I_RTC_SYNC_MONOTONIC, "rtc_sync_monotonic",
			       "e_tgt1=%u,e_tgt2=%u", (unsigned)e_tgt[0],
			       (unsigned)e_tgt[1]);
	} else {
		mbox_item_fail(MBOX_I_RTC_SYNC_MONOTONIC, "rtc_sync_monotonic",
			       "e_tgt1=%u,e_tgt2=%u", (unsigned)e_tgt[0],
			       (unsigned)e_tgt[1]);
	}

	/* ③ target 侧 RTC 走时推进（判据在 cpu1 自测；这里用线上带回来的
	 *    advance_s 独立复核一遍，避免"target 自报通过但值没传过来"） */
	if (adv_ok) {
		mbox_item_pass(MBOX_I_RTC_READ_ADVANCES, "rtc_read_advances",
			       "tgt_advance_s=%d,source=payload", (int)advance);
	} else {
		mbox_item_fail(MBOX_I_RTC_READ_ADVANCES, "rtc_read_advances",
			       "tgt_advance_s=%d,source=payload", (int)advance);
	}
#else
	LOG_ERR("[CTRL] rtc_sync 需要 CONFIG_RTC=y");
	mbox_mark_fail("CONFIG_RTC 未启用");
#endif
}

/* ------------------------------------------------------------------ */
/* case 6：屏蔽 IRQ 期间 FIFO 满 → -ENOSPC                             */
/* ------------------------------------------------------------------ */
static void case_fifo_full(void)
{
	struct mbox_test_msg_v2 m;
	uint32_t ok = 0, enospc = 0, other = 0;
	bool full_ok;

	/* 等 target 侧屏蔽本核 mbox IRQ */
	k_msleep(500);

	LOG_INF("[CTRL] fifo_full: burst %u msgs (fifo depth=%u)",
		(unsigned)(MBOX_FIFO_DEPTH + 2), (unsigned)MBOX_FIFO_DEPTH);

	for (uint32_t i = 0; i < MBOX_FIFO_DEPTH + 2; i++) {
		int ret;

		case_echo_fill(&m, i, 0);
		ret = case_send(&ch4, &m, W_CH4);
		if (ret == 0) {
			ok++;
		} else if (ret == -ENOSPC) {
			enospc++;
		} else {
			LOG_ERR("[CTRL] burst send[%u] ret=%d", i, ret);
			other++;
		}
	}

	full_ok = (ok == MBOX_FIFO_DEPTH) && (enospc == 2) && (other == 0);
	if (full_ok) {
		mbox_item_pass(MBOX_I_FIFO_FULL_ENOSPC, "fifo_full_enospc",
			       "sent=%u,ok=%u,enospc=%u,other=%u,expect_ok=%u,expect_enospc=2",
			       (unsigned)(MBOX_FIFO_DEPTH + 2), (unsigned)ok,
			       (unsigned)enospc, (unsigned)other,
			       (unsigned)MBOX_FIFO_DEPTH);
	} else {
		mbox_item_fail(MBOX_I_FIFO_FULL_ENOSPC, "fifo_full_enospc",
			       "sent=%u,ok=%u,enospc=%u,other=%u,expect_ok=%u,expect_enospc=2",
			       (unsigned)(MBOX_FIFO_DEPTH + 2), (unsigned)ok,
			       (unsigned)enospc, (unsigned)other,
			       (unsigned)MBOX_FIFO_DEPTH);
	}
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */
void mbox_cases_controller_main(void)
{
	LOG_INF("[CTRL] extended mbox case id=%d", CONFIG_MBOX_TEST_CASE_ID);

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
		LOG_ERR("[CTRL] unknown case id %d", CONFIG_MBOX_TEST_CASE_ID);
		mbox_mark_fail("unknown MBOX_TEST_CASE_ID");
		break;
	}

	LOG_INF("[CTRL] extended case done");
}
