/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * MBOX test - cpu2 (controller) side
 * 主动发送消息到 cpu1，验证回复。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "mbox_test_protocol.h"

LOG_MODULE_REGISTER(mbox_ctrl, LOG_LEVEL_INF);

static const struct mbox_dt_spec tx_ch4 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch4), tx);
static const struct mbox_dt_spec tx_ch5 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch5), tx);

static int send_test_msg(const struct mbox_dt_spec *tx, uint8_t ch, uint32_t seq)
{
	struct mbox_test_msg msg = {
		.magic = MBOX_TEST_MAGIC,
		.channel = ch,
		.seq = seq,
	};
	for (int i = 0; i < sizeof(msg.payload); i++) {
		msg.payload[i] = (uint8_t)(seq + i);
	}
	struct mbox_msg m = { .data = &msg, .size = sizeof(msg) };
	return mbox_send_dt(tx, &m);
}

void mbox_controller_main(void)
{
	LOG_INF("[CPU2 MBOX] MBOX controller test started");

	int total_sent = 0;
	int total_failed = 0;

	/* TC1+TC2: ch4 发送 5 条消息 */
	LOG_INF("[CPU2 MBOX] TC1+TC2: ch4 send 5 msgs");
	for (uint32_t seq = 0; seq < 5; seq++) {
		int ret = send_test_msg(&tx_ch4, 4, seq);
		if (ret != 0) {
			LOG_ERR("ch4 send seq=%d failed: %d", seq, ret);
			total_failed++;
		}
		total_sent++;
		k_msleep(100);
	}

	/* TC3: ch5 发送 3 条消息 */
	LOG_INF("[CPU2 MBOX] TC3: ch5 send 3 msgs");
	for (uint32_t seq = 0; seq < 3; seq++) {
		int ret = send_test_msg(&tx_ch5, 5, seq);
		if (ret != 0) {
			LOG_ERR("ch5 send seq=%d failed: %d", seq, ret);
			total_failed++;
		}
		total_sent++;
		k_msleep(100);
	}

	k_msleep(1000);

	/* TC4: MTU 边界测试 */
	LOG_INF("[CPU2 MBOX] TC4: MTU boundary");
	struct mbox_test_msg big_msg = {
		.magic = MBOX_TEST_MAGIC, .channel = 4, .seq = 99,
	};
	memset(big_msg.payload, 0xBB, sizeof(big_msg.payload));
	struct mbox_msg m = { .data = &big_msg, .size = sizeof(big_msg) };
	int ret = mbox_send_dt(&tx_ch4, &m);
	LOG_INF("[CPU2 MBOX] MTU full: %s (ret=%d)", ret == 0 ? "OK" : "FAIL", ret);
	if (ret != 0) total_failed++;

	LOG_INF("[CPU2 MBOX] sent=%d failed=%d", total_sent, total_failed);

	if (total_failed == 0) {
		LOG_INF("[CPU2 MBOX] ALL PASS");
	} else {
		LOG_INF("[CPU2 MBOX] FAIL");
	}
}
