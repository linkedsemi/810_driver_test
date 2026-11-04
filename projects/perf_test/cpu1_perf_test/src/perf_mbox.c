/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * MBOX round-trip throughput against the cpu2 echo peer.
 *
 * cpu1 sends 64-byte frames on perf_ch5; the peer echoes each frame back on
 * perf_ch6.  Each iteration waits for its echo, so the measured time includes
 * the peer's scheduling latency (that is the point of the round-trip number).
 */
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/mbox.h>
#include <string.h>

#include "perf_common.h"

#define PERF_MBOX_MAGIC  0x50455246u /* "PERF" */
#define PERF_READY_MAGIC 0x52454144u /* "READY" */
/* "HELO": the peer echoes frames verbatim, so this coming back proves
 * the echo thread is alive (used when the peer booted long ago). */
#define PERF_HELLO_MAGIC 0x4F4C4548u
#define PERF_FRAMES      500
#define PERF_ECHO_TIMEOUT_MS 1000

struct perf_msg {
	uint32_t magic;
	uint32_t seq;
	uint8_t payload[56];
};

_Static_assert(sizeof(struct perf_msg) == 64,
	       "perf_msg must match the 64-byte mbox channel width");

static const struct mbox_dt_spec mbox_tx = MBOX_DT_SPEC_GET(DT_NODELABEL(perf_ch5), mbox);
static const struct mbox_dt_spec mbox_rx = MBOX_DT_SPEC_GET(DT_NODELABEL(perf_ch6), mbox);

static K_SEM_DEFINE(rx_sem, 0, PERF_FRAMES + 1);
static volatile bool peer_ready;
static volatile uint32_t rx_count;
static volatile uint32_t rx_errors;

static void mbox_rx_cb(const struct device *dev, mbox_channel_id_t channel,
		       void *user_data, struct mbox_msg *msg)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(user_data);

	if (msg == NULL || msg->data == NULL ||
	    msg->size < sizeof(struct perf_msg)) {
		rx_errors++;
		return;
	}

	const struct perf_msg *m = (const struct perf_msg *)msg->data;

	if (m->magic == PERF_READY_MAGIC || m->magic == PERF_HELLO_MAGIC) {
		peer_ready = true;
	} else if (m->magic == PERF_MBOX_MAGIC) {
		rx_count++;
	} else {
		rx_errors++;
		return;
	}

	k_sem_give(&rx_sem);
}

ZTEST(perf_mbox, roundtrip)
{
	zassert_equal(mbox_register_callback(mbox_rx.dev, mbox_rx.channel_id,
					     mbox_rx_cb, NULL), 0,
		      "register rx callback");
	zassert_equal(mbox_set_enabled(mbox_rx.dev, mbox_rx.channel_id, 1), 0,
		      "enable rx channel");

	/* Liveness probe first: the peer echoes every frame verbatim, so a HELLO
	 * coming back proves its echo thread is running.  In the integrated image
	 * cpu1 runs this suite long after cpu2 booted, so a one-shot READY sent at
	 * boot would be missed; the probe works at any time and costs two frames. */
	for (int i = 0; i < 30 && !peer_ready; i++) {
		struct perf_msg hello = {
			.magic = PERF_HELLO_MAGIC,
			.seq = 0,
		};
		struct mbox_msg hmsg = {
			.data = &hello,
			.size = sizeof(hello),
		};

		(void)mbox_send_dt(&mbox_tx, &hmsg);
		(void)k_sem_take(&rx_sem, K_MSEC(100));
	}

	/* Fallback: the peer may announce READY by itself (standalone perf_test,
	 * where both cores start together). */
	int64_t deadline = k_uptime_get() + 5000;

	while (!peer_ready && k_uptime_get() < deadline) {
		(void)k_sem_take(&rx_sem, K_MSEC(100));
	}

	zassert_true(peer_ready, "peer did not report READY");
	zassert_equal(rx_errors, 0, "rx errors before round-trip: %u",
		      (unsigned)rx_errors);

	/* Drain late HELLO echoes so they cannot be mistaken for round-trip
	 * frames (one token per send below). */
	k_msleep(50);
	while (k_sem_take(&rx_sem, K_NO_WAIT) == 0) {
	}
	k_sem_reset(&rx_sem);
	rx_count = 0;

	uint64_t t0 = k_cycle_get_64();

	for (uint32_t i = 0; i < PERF_FRAMES; i++) {
		struct perf_msg m = {
			.magic = PERF_MBOX_MAGIC,
			.seq = i,
		};
		struct mbox_msg msg = {
			.data = &m,
			.size = sizeof(m),
		};

		memset(m.payload, (uint8_t)i, sizeof(m.payload));

		zassert_equal(mbox_send_dt(&mbox_tx, &msg), 0,
			      "send seq %u failed", (unsigned)i);
		zassert_equal(k_sem_take(&rx_sem, K_MSEC(PERF_ECHO_TIMEOUT_MS)),
			      0, "echo timeout at seq %u", (unsigned)i);
	}

	uint64_t t1 = k_cycle_get_64();

	zassert_equal(rx_count, PERF_FRAMES,
		      "echo count %u != %u", (unsigned)rx_count,
		      (unsigned)PERF_FRAMES);
	zassert_equal(rx_errors, 0, "rx errors after round-trip: %u",
		      (unsigned)rx_errors);

	uint64_t us = k_cyc_to_us_floor64(t1 - t0);
	uint64_t bytes = (uint64_t)PERF_FRAMES * sizeof(struct perf_msg);

	perf_bytes("mbox", "roundtrip_64B", bytes, us);
	perf_ops("mbox", "roundtrip", PERF_FRAMES, us);
}

ZTEST_SUITE(perf_mbox, NULL, NULL, NULL, NULL, NULL);
