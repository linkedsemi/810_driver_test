/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Telemetry relay server (cpu1).  The cpu2 peer sends one text line per 64-byte
 * frame on perf_ch9; this server prints it on the cpu1 console with a
 * "PERF_TEST:CTRL:" envelope.  That makes cpu1's UART the single test-data
 * outlet while the raw record still carries the source core.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/mbox.h>
#include <string.h>

#define TELEMETRY_FRAME_SIZE 128

static const struct mbox_dt_spec rx_ch = MBOX_DT_SPEC_GET(DT_NODELABEL(perf_ch9), mbox);

K_MSGQ_DEFINE(tele_q, TELEMETRY_FRAME_SIZE, 32, 4);

static void telemetry_rx_cb(const struct device *dev, mbox_channel_id_t channel,
			    void *user_data, struct mbox_msg *msg)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(user_data);

	if (msg == NULL || msg->data == NULL ||
	    msg->size < TELEMETRY_FRAME_SIZE) {
		return;
	}
	(void)k_msgq_put(&tele_q, msg->data, K_NO_WAIT);
}

static void telemetry_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		char buf[TELEMETRY_FRAME_SIZE];

		if (k_msgq_get(&tele_q, buf, K_FOREVER) != 0) {
			continue;
		}
		buf[TELEMETRY_FRAME_SIZE - 1] = '\0';
		printk("PERF_TEST:CTRL:%s\n", buf);
	}
}

static K_THREAD_STACK_DEFINE(telemetry_stack, 2048);
static struct k_thread telemetry_td;

int perf_telemetry_init(void)
{
	(void)mbox_register_callback(rx_ch.dev, rx_ch.channel_id,
				     telemetry_rx_cb, NULL);
	(void)mbox_set_enabled(rx_ch.dev, rx_ch.channel_id, 1);

	k_thread_create(&telemetry_td, telemetry_stack,
			K_THREAD_STACK_SIZEOF(telemetry_stack),
			telemetry_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(9), 0, K_NO_WAIT);
	k_thread_name_set(&telemetry_td, "perf_telemetry");
	return 0;
}
