/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * MBOX test - cpu2 (controller) side
 * 主动发送消息到 cpu1，验证回复。
 *
 * 驱动改版后用单一 mbox-names="mbox"（不再分 tx/rx）。
 *
 * 判定（§1.1 修复）：本函数不再自己下结论，只把每条 check 的 pass/fail
 * 交给 tests/src/mbox_test_report.c 记账；src/main.c 在返回后按状态打
 * `PROJECT EXECUTION: SUCCESSFUL|FAILED`。修复前失败只打 LOG_INF("…FAIL")，
 * 而 main.c 无条件打 SUCCESSFUL ⇒ send 全失败也判 PASS。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "mbox_test_protocol.h"
#include "mbox_test_report.h"

LOG_MODULE_REGISTER(mbox_ctrl, LOG_LEVEL_INF);

/* 单一 mbox spec（新版驱动，mbox-names="mbox"） */
static const struct mbox_dt_spec mbox_ch4 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch4), mbox);
static const struct mbox_dt_spec mbox_ch5 = MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_test_ch5), mbox);

/* ch4 单帧上限（= mboxes 的 tx_fifo_width cell），MTU 边界的判据来源 */
#define MBOX_CH4_WIDTH  DT_PHA_BY_IDX(DT_NODELABEL(mbox_test_ch4), mboxes, 0, tx_fifo_width)

_Static_assert(sizeof(struct mbox_test_msg) == MBOX_CH4_WIDTH,
	       "basic_mtu_full 用整帧长度打满 ch4；帧长必须等于 ch4 MTU");

static int send_test_msg(const struct mbox_dt_spec *spec, uint32_t seq)
{
	struct mbox_test_msg msg = {
		.magic = MBOX_TEST_MAGIC,
		.channel = (uint8_t)spec->channel_id,
		.seq = seq,
	};
	for (int i = 0; i < sizeof(msg.payload); i++) {
		msg.payload[i] = (uint8_t)(seq + i);
	}
	struct mbox_msg m = { .data = &msg, .size = sizeof(msg) };
	return mbox_send_dt(spec, &m);
}

void mbox_controller_main(void)
{
	LOG_INF("[CPU2 MBOX] MBOX controller test started");

	int ch4_sent = 0, ch4_failed = 0;
	int ch5_sent = 0, ch5_failed = 0;
	int mtu_ret;

	/* 等 cpu1 注册回调 + set_enabled 完成，否则开机竞态会吞掉队首帧
	 * （同各扩展 case 的 k_msleep(600)，见 MBOX_BASIC_STARTUP_MS）。 */
	k_msleep(MBOX_BASIC_STARTUP_MS);

	/* TC1+TC2: ch4 发送 5 条消息 */
	LOG_INF("[CPU2 MBOX] TC1+TC2: ch4 send %d msgs", MBOX_BASIC_CH4_COUNT);
	for (uint32_t seq = 0; seq < MBOX_BASIC_CH4_COUNT; seq++) {
		int ret = send_test_msg(&mbox_ch4, seq);

		if (ret != 0) {
			LOG_ERR("ch4 send seq=%d failed: %d", seq, ret);
			ch4_failed++;
		}
		ch4_sent++;
		k_msleep(100);
	}

	/* TC3: ch5 发送 3 条消息 */
	LOG_INF("[CPU2 MBOX] TC3: ch5 send %d msgs", MBOX_BASIC_CH5_COUNT);
	for (uint32_t seq = 0; seq < MBOX_BASIC_CH5_COUNT; seq++) {
		int ret = send_test_msg(&mbox_ch5, seq);

		if (ret != 0) {
			LOG_ERR("ch5 send seq=%d failed: %d", seq, ret);
			ch5_failed++;
		}
		ch5_sent++;
		k_msleep(100);
	}

	k_msleep(1000);

	/* TC4: MTU 边界测试（size == ch4 width 的满帧必须被接受） */
	LOG_INF("[CPU2 MBOX] TC4: MTU boundary");
	struct mbox_test_msg big_msg = {
		.magic = MBOX_TEST_MAGIC, .channel = 4, .seq = 99,
	};
	memset(big_msg.payload, 0xBB, sizeof(big_msg.payload));
	struct mbox_msg m = { .data = &big_msg, .size = sizeof(big_msg) };
	mtu_ret = mbox_send_dt(&mbox_ch4, &m);
	LOG_INF("[CPU2 MBOX] MTU full: %s (ret=%d)", mtu_ret == 0 ? "OK" : "FAIL", mtu_ret);

	/* ---- 逐项上报（判据见 COVERAGE.md §2） ---- */
	if (ch4_failed == 0) {
		mbox_item_pass(MBOX_I_BASIC_CH4, "basic_ch4_frames",
			       "sent=%d,failed=%d,expect=%d",
			       ch4_sent, ch4_failed, MBOX_BASIC_CH4_COUNT);
	} else {
		mbox_item_fail(MBOX_I_BASIC_CH4, "basic_ch4_frames",
			       "sent=%d,failed=%d,expect=%d",
			       ch4_sent, ch4_failed, MBOX_BASIC_CH4_COUNT);
	}

	if (ch5_failed == 0) {
		mbox_item_pass(MBOX_I_BASIC_CH5, "basic_ch5_frames",
			       "sent=%d,failed=%d,expect=%d",
			       ch5_sent, ch5_failed, MBOX_BASIC_CH5_COUNT);
	} else {
		mbox_item_fail(MBOX_I_BASIC_CH5, "basic_ch5_frames",
			       "sent=%d,failed=%d,expect=%d",
			       ch5_sent, ch5_failed, MBOX_BASIC_CH5_COUNT);
	}

	if (mtu_ret == 0) {
		mbox_item_pass(MBOX_I_BASIC_MTU, "basic_mtu_full",
			       "ret=0,size=%u,width=%u",
			       (unsigned)sizeof(big_msg), (unsigned)MBOX_CH4_WIDTH);
	} else {
		mbox_item_fail(MBOX_I_BASIC_MTU, "basic_mtu_full",
			       "ret=%d,size=%u,width=%u", mtu_ret,
			       (unsigned)sizeof(big_msg), (unsigned)MBOX_CH4_WIDTH);
	}

	LOG_INF("[CPU2 MBOX] sent=%d failed=%d", ch4_sent + ch5_sent,
		ch4_failed + ch5_failed);
}
