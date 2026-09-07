/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * MBOX test - cpu1 (target) side
 * 等待 cpu2 发送消息，验证 pattern，回复确认。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "mbox_test_protocol.h"

LOG_MODULE_REGISTER(mbox_tgt, LOG_LEVEL_INF);

static const struct mbox_dt_spec rx_ch4 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch4), rx);
static const struct mbox_dt_spec tx_ch4 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch4), tx);
static const struct mbox_dt_spec rx_ch5 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch5), rx);
static const struct mbox_dt_spec tx_ch5 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch5), tx);

static volatile uint32_t rx_count_ch4;
static volatile uint32_t rx_count_ch5;
static volatile bool rx_done_ch4;
static volatile bool rx_done_ch5;
static struct mbox_test_msg last_msg;

static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel,
			      void *user_data, struct mbox_msg *msg)
{
	/* ISR: 只记账，不打 log */
	if (msg && msg->data && msg->size >= sizeof(struct mbox_test_msg)) {
		memcpy(&last_msg, msg->data, sizeof(struct mbox_test_msg));
	}
	if (channel == 4) {
		rx_count_ch4++;
		rx_done_ch4 = true;
	} else if (channel == 5) {
		rx_count_ch5++;
		rx_done_ch5 = true;
	}
}

static int send_ack(const struct mbox_dt_spec *tx, uint8_t ch, uint32_t seq)
{
	struct mbox_test_msg ack = {
		.magic = MBOX_TEST_MAGIC,
		.channel = ch,
		.seq = seq,
	};
	memset(ack.payload, 0xAA, sizeof(ack.payload));
	struct mbox_msg m = { .data = &ack, .size = sizeof(ack) };
	return mbox_send_dt(tx, &m);
}

void mbox_target_main(void)
{
	LOG_INF("[CPU1 MBOX] MBOX target test started");

	mbox_register_callback(rx_ch4.dev, rx_ch4.channel_id, mbox_rx_callback, NULL);
	mbox_register_callback(rx_ch5.dev, rx_ch5.channel_id, mbox_rx_callback, NULL);
	mbox_set_enabled(rx_ch4.dev, rx_ch4.channel_id, 1);
	mbox_set_enabled(rx_ch5.dev, rx_ch5.channel_id, 1);

	LOG_INF("[CPU1 MBOX] waiting for cpu2 messages...");

	/* 等待 cpu2 完成所有测试（最多 30s） */
	for (int t = 0; t < 300; t++) {
		if (rx_done_ch4 && rx_done_ch5) {
			break;
		}
		k_msleep(100);
	}

	LOG_INF("[CPU1 MBOX] ch4_rx=%d ch5_rx=%d", rx_count_ch4, rx_count_ch5);

	send_ack(&tx_ch4, 4, rx_count_ch4);
	send_ack(&tx_ch5, 5, rx_count_ch5);

	LOG_INF("[CPU1 MBOX] target done");
}
