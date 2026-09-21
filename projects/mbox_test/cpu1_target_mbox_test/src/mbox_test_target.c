/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * MBOX test - cpu1 (target) side
 * 等待 cpu2 发送消息，验证 pattern，回复确认。
 *
 * 驱动改版后用单一 mbox-names="mbox"（不再分 tx/rx）。
 *
 * 判定（§1.1 修复）：修复前本函数**从不校验**收到的内容与条数 —— 只要两个
 * 通道各收到过 ≥1 帧就退出，然后 main.c 无条件打 SUCCESSFUL ⇒ 对端一条都
 * 收不到也判 PASS（等着等着超时同样打 SUCCESS）。现在：
 *   - 每条消息在 ISR 里过 mbox_test_verify()（magic / channel / payload pattern）
 *     与序号连续性检查，只记账不打 log（§6.1.2）；
 *   - 主线程等到**契约条数**（ch4=MBOX_BASIC_CH4_COUNT、ch5=MBOX_BASIC_CH5_COUNT）
 *     或超时才退出，再按 条数 + 校验错 逐项上报。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "mbox_test_protocol.h"
#include "mbox_test_report.h"

LOG_MODULE_REGISTER(mbox_tgt, LOG_LEVEL_INF);

/* 单一 mbox spec（新版驱动，mbox-names="mbox"） */
static const struct mbox_dt_spec mbox_ch4 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch4), mbox);
static const struct mbox_dt_spec mbox_ch5 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch5), mbox);

/* 驱动约定：rx 回调上报的 msg.size 恒为该通道 width（mbox_linkedsemi.c:135-138），
 * ch4/ch5 的 width 均为 64，故整帧长度 = sizeof(struct mbox_test_msg)。 */
_Static_assert(sizeof(struct mbox_test_msg) ==
	       DT_PHA_BY_IDX(DT_NODELABEL(mbox_test_ch4), mboxes, 0, rx_fifo_width),
	       "basic 校验要求 ch4 rx width == V1 帧长");

static volatile uint32_t rx_count_ch4;
static volatile uint32_t rx_count_ch5;
static volatile uint32_t verify_err_ch4;
static volatile uint32_t verify_err_ch5;
static volatile uint32_t seq_err_ch4;
static volatile uint32_t seq_err_ch5;
static volatile uint32_t next_seq_ch4;
static volatile uint32_t next_seq_ch5;

static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel,
			      void *user_data, struct mbox_msg *msg)
{
	/* ISR: 只记账，不打 log */
	uint32_t ch = (uint32_t)channel;
	bool is_ch4 = (ch == (uint32_t)mbox_ch4.channel_id);
	bool is_ch5 = (ch == (uint32_t)mbox_ch5.channel_id);

	if (!is_ch4 && !is_ch5) {
		return;
	}

	if (msg == NULL || msg->data == NULL ||
	    msg->size < sizeof(struct mbox_test_msg)) {
		/* 短帧/空帧：本工程的基本 case 只发整帧，收到即为驱动异常 */
		if (is_ch4) {
			verify_err_ch4++;
		} else {
			verify_err_ch5++;
		}
		return;
	}

	const struct mbox_test_msg *m = (const struct mbox_test_msg *)msg->data;

	if (mbox_test_verify(m, (uint8_t)ch) != 0) {
		if (is_ch4) {
			verify_err_ch4++;
		} else {
			verify_err_ch5++;
		}
	}

	/* 同一通道的 TX/RX FIFO 是环形队列（peek 取最老项 → drop），
	 * 送达序必与发送序一致，故序号连续性是可断言的驱动不变量。 */
	if (is_ch4) {
		if (m->seq != next_seq_ch4) {
			seq_err_ch4++;
		}
		next_seq_ch4 = m->seq + 1;
		rx_count_ch4++;
	} else {
		if (m->seq != next_seq_ch5) {
			seq_err_ch5++;
		}
		next_seq_ch5 = m->seq + 1;
		rx_count_ch5++;
	}
}

static int send_ack(const struct mbox_dt_spec *spec, uint32_t seq)
{
	struct mbox_test_msg ack = {
		.magic = MBOX_TEST_MAGIC,
		.channel = (uint8_t)spec->channel_id,
		.seq = seq,
	};
	memset(ack.payload, 0xAA, sizeof(ack.payload));
	struct mbox_msg m = { .data = &ack, .size = sizeof(ack) };
	return mbox_send_dt(spec, &m);
}

void mbox_target_main(void)
{
	bool ok4, ok5;

	LOG_INF("[CPU1 MBOX] MBOX target test started");

	(void)mbox_register_callback(mbox_ch4.dev, mbox_ch4.channel_id, mbox_rx_callback, NULL);
	(void)mbox_register_callback(mbox_ch5.dev, mbox_ch5.channel_id, mbox_rx_callback, NULL);
	(void)mbox_set_enabled(mbox_ch4.dev, mbox_ch4.channel_id, 1);
	(void)mbox_set_enabled(mbox_ch5.dev, mbox_ch5.channel_id, 1);

	LOG_INF("[CPU1 MBOX] waiting for cpu2 messages...");

	/* 等到契约条数齐（最多 30s）。修复前判据是"两通道各收到 ≥1 帧"即
	 * break —— 条数少一半也照样退出然后打 SUCCESS。 */
	for (int t = 0; t < 300; t++) {
		if (rx_count_ch4 >= MBOX_BASIC_CH4_COUNT &&
		    rx_count_ch5 >= MBOX_BASIC_CH5_COUNT) {
			break;
		}
		k_msleep(100);
	}

	LOG_INF("[CPU1 MBOX] ch4_rx=%u ch5_rx=%u verify_err=%u/%u seq_err=%u/%u",
		rx_count_ch4, rx_count_ch5, verify_err_ch4, verify_err_ch5,
		seq_err_ch4, seq_err_ch5);

	ok4 = (rx_count_ch4 == MBOX_BASIC_CH4_COUNT) && (verify_err_ch4 == 0) &&
	      (seq_err_ch4 == 0);
	ok5 = (rx_count_ch5 == MBOX_BASIC_CH5_COUNT) && (verify_err_ch5 == 0) &&
	      (seq_err_ch5 == 0);

	/* 逐项上报（判据见 COVERAGE.md §2）。条数不对 / payload pattern 不符 /
	 * 序号断裂 ⇒ 本核判负 ⇒ main.c 打 PROJECT EXECUTION: FAILED。 */
	if (ok4) {
		mbox_item_pass(MBOX_I_BASIC_CH4, "basic_ch4_frames",
			       "recv=%u,expect=%u,verify_err=%u,seq_err=%u",
			       rx_count_ch4, MBOX_BASIC_CH4_COUNT, verify_err_ch4,
			       seq_err_ch4);
	} else {
		mbox_item_fail(MBOX_I_BASIC_CH4, "basic_ch4_frames",
			       "recv=%u,expect=%u,verify_err=%u,seq_err=%u",
			       rx_count_ch4, MBOX_BASIC_CH4_COUNT, verify_err_ch4,
			       seq_err_ch4);
	}

	if (ok5) {
		mbox_item_pass(MBOX_I_BASIC_CH5, "basic_ch5_frames",
			       "recv=%u,expect=%u,verify_err=%u,seq_err=%u",
			       rx_count_ch5, MBOX_BASIC_CH5_COUNT, verify_err_ch5,
			       seq_err_ch5);
	} else {
		mbox_item_fail(MBOX_I_BASIC_CH5, "basic_ch5_frames",
			       "recv=%u,expect=%u,verify_err=%u,seq_err=%u",
			       rx_count_ch5, MBOX_BASIC_CH5_COUNT, verify_err_ch5,
			       seq_err_ch5);
	}

	/* MTU 满帧（TC4）不校验内容：它的判据在 cpu2 侧的返回值。 */

	(void)send_ack(&mbox_ch4, rx_count_ch4);
	(void)send_ack(&mbox_ch5, rx_count_ch5);

	LOG_INF("[CPU1 MBOX] target done");
}
