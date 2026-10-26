/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flash proxy server (cpu1).  Handles requests from the cpu2 client on
 * perf_ch7 and replies on perf_ch8.  The data path is the qspi2 board NOR;
 * every operation is performed here, so the cpu2 side measures the full
 * request/transfer round trip.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/flash.h>
#include <errno.h>
#include <string.h>

#include "perf_proxy.h"
#include "perf_flash_lock.h"

static const struct mbox_dt_spec rx_ch = MBOX_DT_SPEC_GET(DT_NODELABEL(perf_ch7), mbox);
static const struct mbox_dt_spec tx_ch = MBOX_DT_SPEC_GET(DT_NODELABEL(perf_ch8), mbox);

static const struct device *flash_dev;
static uint8_t proxy_buf[PERF_PROXY_SIZE];

/* A 4 KB proxy operation is split into ~103 frames; the inbound queue must
 * hold a whole burst or K_NO_WAIT drops requests. */
K_MSGQ_DEFINE(proxy_q, sizeof(struct perf_proxy_msg), 128, 4);

static int proxy_send(const struct perf_proxy_msg *m)
{
	struct mbox_msg msg = {
		.data = m,
		.size = sizeof(*m),
	};

	/* The peer may still be draining the FIFO; retry briefly. */
	for (int i = 0; i < 200; i++) {
		if (mbox_send_dt(&tx_ch, &msg) == 0) {
			return 0;
		}
		k_msleep(1);
	}
	return -EIO;
}

static void proxy_rx_cb(const struct device *dev, mbox_channel_id_t channel,
			void *user_data, struct mbox_msg *msg)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(user_data);

	if (msg == NULL || msg->data == NULL ||
	    msg->size < sizeof(struct perf_proxy_msg)) {
		return;
	}
	(void)k_msgq_put(&proxy_q, msg->data, K_NO_WAIT);
}

static void proxy_send_ack(uint8_t op, uint16_t seq, int status)
{
	struct perf_proxy_msg r = {
		.magic = PERF_PROXY_MAGIC,
		.op = op,
		.flags = PERF_PROXY_FLAG_LAST,
		.seq = seq,
		.status = (uint32_t)status,
	};

	(void)proxy_send(&r);
}

static void proxy_server_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		struct perf_proxy_msg m;

		if (k_msgq_get(&proxy_q, &m, K_FOREVER) != 0) {
			continue;
		}
		if (m.magic != PERF_PROXY_MAGIC) {
			continue;
		}

		switch (m.op) {
		case PERF_PROXY_PING:
			proxy_send_ack(m.op, m.seq, 0);
			break;

		case PERF_PROXY_ERASE: {
			int rc;

			k_mutex_lock(&perf_flash_mutex, K_FOREVER);
			rc = flash_erase(flash_dev, m.offset, m.len);
			k_mutex_unlock(&perf_flash_mutex);
			proxy_send_ack(m.op, m.seq, rc);
			break;
		}

		case PERF_PROXY_WRITE: {
			/* Each frame carries one chunk; offset is the chunk base. */
			int rc;

			k_mutex_lock(&perf_flash_mutex, K_FOREVER);
			rc = flash_write(flash_dev, m.offset, m.data, m.len);
			k_mutex_unlock(&perf_flash_mutex);

			if ((m.flags & PERF_PROXY_FLAG_LAST) || rc != 0) {
				proxy_send_ack(m.op, m.seq, rc);
			}
			break;
		}

		case PERF_PROXY_READ: {
			int rc;

			k_mutex_lock(&perf_flash_mutex, K_FOREVER);
			rc = flash_read(flash_dev, m.offset, proxy_buf, m.len);
			k_mutex_unlock(&perf_flash_mutex);

			if (rc != 0) {
				proxy_send_ack(m.op, m.seq, rc);
				break;
			}

			for (uint32_t off = 0; off < m.len; off += PERF_PROXY_CHUNK) {
				uint32_t n = MIN(PERF_PROXY_CHUNK, m.len - off);
				struct perf_proxy_msg c = {
					.magic = PERF_PROXY_MAGIC,
					.op = PERF_PROXY_READ,
					.seq = m.seq,
					.offset = m.offset + off,
					.len = n,
					.status = 0,
				};

				if (off + n >= m.len) {
					c.flags = PERF_PROXY_FLAG_LAST;
				}
				memcpy(c.data, &proxy_buf[off], n);
				(void)proxy_send(&c);
			}
			break;
		}

		default:
			proxy_send_ack(m.op, m.seq, -EINVAL);
			break;
		}
	}
}

static K_THREAD_STACK_DEFINE(proxy_stack, 4096);
static struct k_thread proxy_td;

int perf_proxy_server_init(void)
{
	flash_dev = DEVICE_DT_GET(DT_NODELABEL(flash2));
	if (!device_is_ready(flash_dev)) {
		return -ENODEV;
	}

	(void)mbox_register_callback(rx_ch.dev, rx_ch.channel_id,
				     proxy_rx_cb, NULL);
	(void)mbox_set_enabled(rx_ch.dev, rx_ch.channel_id, 1);

	k_thread_create(&proxy_td, proxy_stack, K_THREAD_STACK_SIZEOF(proxy_stack),
			proxy_server_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	k_thread_name_set(&proxy_td, "perf_proxy");
	return 0;
}
