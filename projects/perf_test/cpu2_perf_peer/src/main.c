/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * perf_test cpu2 peer.
 *
 * Two jobs:
 *   1. Echo 64-byte frames (perf_ch5 -> perf_ch6) for the cpu1 mbox test.
 *   2. Drive the flash proxy client (perf_ch7/perf_ch8): cpu2 asks the cpu1
 *      server to operate the qspi2 NOR and measures the end-to-end round trip.
 *
 * All test data is reported to cpu1 over perf_ch9 as text lines; cpu1 prints
 * them on its own console with a "PERF_TEST:CTRL:" envelope, so the whole
 * test record is visible on cpu1's UART only.  This core keeps just a boot
 * banner on its local UART.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/sm3_alt.h"
#include "mbedtls_otbn_rsa_mbox.h"
#include "rsa_test_key.h"

#include "perf_proxy.h"

#define PERF_MBOX_MAGIC  0x50455246u /* "PERF" */
#define PERF_READY_MAGIC 0x52454144u /* "READY" */
#define PERF_READY_DELAY_MS 600
#define PERF_FRAMES      500
#define TELE_FRAME_SIZE  128

struct perf_msg {
	uint32_t magic;
	uint32_t seq;
	uint8_t payload[56];
};

_Static_assert(sizeof(struct perf_msg) == 64,
	       "perf_msg must match the 64-byte mbox channel width");

/* ------------------------------------------------------------- mbox echo */

static const struct mbox_dt_spec mbox_rx = MBOX_DT_SPEC_GET(DT_NODELABEL(perf_ch5), mbox);
static const struct mbox_dt_spec mbox_tx = MBOX_DT_SPEC_GET(DT_NODELABEL(perf_ch6), mbox);

K_MSGQ_DEFINE(echo_q, sizeof(struct perf_msg), 16, 4);

static volatile bool peer_started;
static volatile uint32_t echo_count;

static void mbox_rx_cb(const struct device *dev, mbox_channel_id_t channel,
		       void *user_data, struct mbox_msg *msg)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(user_data);

	if (msg == NULL || msg->data == NULL ||
	    msg->size < sizeof(struct perf_msg)) {
		return;
	}
	(void)k_msgq_put(&echo_q, msg->data, K_NO_WAIT);
}

static void echo_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	peer_started = true;

	for (;;) {
		struct perf_msg m;
		struct mbox_msg out = {
			.data = &m,
			.size = sizeof(m),
		};

		if (k_msgq_get(&echo_q, &m, K_FOREVER) != 0) {
			continue;
		}

		for (int retry = 0; retry < 10; retry++) {
			if (mbox_send_dt(&mbox_tx, &out) == 0) {
				echo_count++;
				break;
			}
			k_msleep(1);
		}
	}
}

static K_THREAD_STACK_DEFINE(echo_stack, 2048);
static struct k_thread echo_td;

/* ------------------------------------------------------- telemetry sender */

static const struct mbox_dt_spec tele_tx = MBOX_DT_SPEC_GET(DT_NODELABEL(perf_ch9), mbox);

static int tele_send(const char *text)
{
	char buf[TELE_FRAME_SIZE] = { 0 };
	struct mbox_msg msg = {
		.data = buf,
		.size = sizeof(buf),
	};

	strncpy(buf, text, sizeof(buf) - 1);
	for (int i = 0; i < 200; i++) {
		if (mbox_send_dt(&tele_tx, &msg) == 0) {
			return 0;
		}
		k_msleep(1);
	}
	return -EIO;
}

static void tele_line(const char *fmt, ...)
{
	char buf[TELE_FRAME_SIZE];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	(void)tele_send(buf);
}

/* --------------------------------------------------------- proxy client */

static const struct mbox_dt_spec proxy_tx = MBOX_DT_SPEC_GET(DT_NODELABEL(perf_ch7), mbox);
static const struct mbox_dt_spec proxy_rx = MBOX_DT_SPEC_GET(DT_NODELABEL(perf_ch8), mbox);

/* One 4 KB read comes back as ~103 frames; hold a whole burst. */
K_MSGQ_DEFINE(proxy_q, sizeof(struct perf_proxy_msg), 128, 4);
static uint8_t proxy_buf[PERF_PROXY_SIZE];
static uint8_t proxy_verify[PERF_PROXY_SIZE];
static bool proxy_inited;

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

static int proxy_send(const struct perf_proxy_msg *m)
{
	struct mbox_msg msg = {
		.data = m,
		.size = sizeof(*m),
	};

	for (int i = 0; i < 200; i++) {
		if (mbox_send_dt(&proxy_tx, &msg) == 0) {
			return 0;
		}
		k_msleep(1);
	}
	return -EIO;
}

static int proxy_wait(struct perf_proxy_msg *m, int timeout_ms)
{
	return k_msgq_get(&proxy_q, m, K_MSEC(timeout_ms));
}

static int proxy_simple(uint8_t op, uint32_t offset, uint32_t len)
{
	struct perf_proxy_msg q = {
		.magic = PERF_PROXY_MAGIC,
		.op = op,
		.flags = PERF_PROXY_FLAG_LAST,
		.offset = offset,
		.len = len,
	};
	struct perf_proxy_msg r;

	k_msgq_purge(&proxy_q);
	if (proxy_send(&q) != 0) {
		return -EIO;
	}
	if (proxy_wait(&r, 2000) != 0) {
		return -ETIMEDOUT;
	}
	if (r.magic != PERF_PROXY_MAGIC) {
		return -EIO;
	}
	return (int)r.status;
}

static int proxy_read(uint32_t offset, uint32_t len, uint8_t *out)
{
	struct perf_proxy_msg q = {
		.magic = PERF_PROXY_MAGIC,
		.op = PERF_PROXY_READ,
		.flags = PERF_PROXY_FLAG_LAST,
		.offset = offset,
		.len = len,
	};
	uint32_t got = 0;

	k_msgq_purge(&proxy_q);
	if (proxy_send(&q) != 0) {
		return -EIO;
	}

	for (;;) {
		struct perf_proxy_msg r;

		if (proxy_wait(&r, 2000) != 0) {
			return -ETIMEDOUT;
		}
		if (r.magic != PERF_PROXY_MAGIC || r.op != PERF_PROXY_READ) {
			return -EIO;
		}
		if (r.status != 0) {
			return (int)r.status;
		}
		if (r.len > sizeof(r.data) || got + r.len > len) {
			return -EINVAL;
		}
		memcpy(&out[got], r.data, r.len);
		got += r.len;
		if (r.flags & PERF_PROXY_FLAG_LAST) {
			break;
		}
	}

	return got == len ? 0 : -EIO;
}

static int proxy_write(uint32_t offset, uint32_t len, const uint8_t *data)
{
	struct perf_proxy_msg r;

	k_msgq_purge(&proxy_q);
	for (uint32_t off = 0; off < len; off += PERF_PROXY_CHUNK) {
		uint32_t n = MIN(PERF_PROXY_CHUNK, len - off);
		struct perf_proxy_msg q = {
			.magic = PERF_PROXY_MAGIC,
			.op = PERF_PROXY_WRITE,
			.flags = (off + n >= len) ? PERF_PROXY_FLAG_LAST : 0,
			.offset = offset + off,
			.len = n,
		};

		memcpy(q.data, &data[off], n);
		if (proxy_send(&q) != 0) {
			return -EIO;
		}
	}

	if (proxy_wait(&r, 2000) != 0) {
		return -ETIMEDOUT;
	}
	if (r.magic != PERF_PROXY_MAGIC) {
		return -EIO;
	}
	return (int)r.status;
}

static void proxy_init_once(void)
{
	if (proxy_inited) {
		return;
	}

	(void)mbox_register_callback(proxy_rx.dev, proxy_rx.channel_id,
				     proxy_rx_cb, NULL);
	(void)mbox_set_enabled(proxy_rx.dev, proxy_rx.channel_id, 1);

	for (int i = 0; i < 50; i++) {
		if (proxy_simple(PERF_PROXY_PING, 0, 0) == 0) {
			proxy_inited = true;
			break;
		}
		k_msleep(100);
	}
}

/* -------------------------------------------------------------- test flow */

static void tele_perf(const char *module, const char *op,
		      uint32_t bytes, uint64_t us)
{
	double mb_s;

	if (us == 0) {
		us = 1;
	}
	mb_s = (double)bytes / (double)us;
	tele_line("PERF %s | %s | bytes=%u | us=%llu | MB/s=%.4f | KB/s=%.2f",
		  module, op, (unsigned)bytes, (unsigned long long)us,
		  mb_s, mb_s * 1000.0);
}

static void tele_ops(const char *module, const char *op,
		     uint32_t ops, uint64_t us)
{
	if (us == 0) {
		us = 1;
	}
	tele_line("PERF %s | %s | ops=%u | us=%llu | ops/s=%.2f",
		  module, op, (unsigned)ops, (unsigned long long)us,
		  (double)ops * 1000000.0 / (double)us);
}

static bool run_proxy_erase(void)
{
	uint64_t t0, t1;
	int rc;

	proxy_init_once();
	if (!proxy_inited) {
		return false;
	}

	t0 = k_cycle_get_64();
	rc = proxy_simple(PERF_PROXY_ERASE, PERF_PROXY_OFFSET, PERF_PROXY_SIZE);
	t1 = k_cycle_get_64();

	tele_perf("flash_proxy", "erase", PERF_PROXY_SIZE, k_cyc_to_us_floor64(t1 - t0));
	return rc == 0;
}

static bool run_proxy_read(void)
{
	uint64_t t0, t1;
	int rc;

	proxy_init_once();
	if (!proxy_inited) {
		return false;
	}

	t0 = k_cycle_get_64();
	rc = proxy_read(PERF_PROXY_OFFSET, PERF_PROXY_SIZE, proxy_buf);
	t1 = k_cycle_get_64();

	tele_perf("flash_proxy", "read", PERF_PROXY_SIZE, k_cyc_to_us_floor64(t1 - t0));
	return rc == 0;
}

static bool run_proxy_write(void)
{
	uint64_t t0, t1;
	int rc;

	proxy_init_once();
	if (!proxy_inited) {
		return false;
	}

	for (size_t i = 0; i < PERF_PROXY_SIZE; i++) {
		proxy_buf[i] = (uint8_t)(i * 7u);
	}

	if (proxy_simple(PERF_PROXY_ERASE, PERF_PROXY_OFFSET,
			 PERF_PROXY_SIZE) != 0) {
		return false;
	}

	t0 = k_cycle_get_64();
	rc = proxy_write(PERF_PROXY_OFFSET, PERF_PROXY_SIZE, proxy_buf);
	t1 = k_cycle_get_64();

	tele_perf("flash_proxy", "write", PERF_PROXY_SIZE, k_cyc_to_us_floor64(t1 - t0));

	if (rc != 0) {
		return false;
	}
	if (proxy_read(PERF_PROXY_OFFSET, PERF_PROXY_SIZE, proxy_verify) != 0) {
		return false;
	}
	return memcmp(proxy_buf, proxy_verify, PERF_PROXY_SIZE) == 0;
}

static bool run_peer_alive(void)
{
	int64_t deadline = k_uptime_get() + 30000;

	if (!peer_started) {
		return false;
	}

	while (echo_count < PERF_FRAMES && k_uptime_get() < deadline) {
		k_msleep(10);
	}
	return echo_count == PERF_FRAMES;
}

/* parse_ztest's native token form requires the duration suffix. */
static void report(const char *name, bool ok, int64_t elapsed_ms, bool *all_ok)
{
	tele_line("%s - %s in %.3f seconds", ok ? "PASS" : "FAIL", name,
		  (double)elapsed_ms / 1000.0);
	if (!ok) {
		*all_ok = false;
	}
}

/* ------------------------------------------------- OTBN crypto (cpu2) */

#define CRYPTO_BUF_SIZE (KB(32) + 32)
static uint8_t crypto_buf[CRYPTO_BUF_SIZE] __aligned(32);
static const size_t crypto_sizes[] = { KB(1), KB(4), KB(16), KB(32) };

static bool digest_sha256(const uint8_t *in, size_t len, uint8_t out[32])
{
	mbedtls_sha256_context c;
	int rc;

	mbedtls_sha256_init(&c);
	rc = mbedtls_sha256_starts(&c, 0);
	rc |= mbedtls_sha256_update(&c, in, len);
	rc |= mbedtls_sha256_finish(&c, out);
	mbedtls_sha256_free(&c);
	return rc == 0;
}

static bool digest_sha512(const uint8_t *in, size_t len, uint8_t out[64])
{
	mbedtls_sha512_context c;
	int rc;

	mbedtls_sha512_init(&c);
	rc = mbedtls_sha512_starts(&c, 0);
	rc |= mbedtls_sha512_update(&c, in, len);
	rc |= mbedtls_sha512_finish(&c, out);
	mbedtls_sha512_free(&c);
	return rc == 0;
}

static bool digest_sm3(const uint8_t *in, size_t len, uint8_t out[32])
{
	mbedtls_sm3_context c;
	int rc;

	mbedtls_sm3_init(&c);
	rc = mbedtls_sm3_starts(&c);
	rc |= mbedtls_sm3_update(&c, in, len);
	rc |= mbedtls_sm3_finish(&c, out);
	mbedtls_sm3_free(&c);
	return rc == 0;
}

static bool run_sha256_otbn(bool unaligned)
{
	uint8_t ref[32], out[32];
	const char *name = unaligned ? "sha256_otbn_unaligned" : "sha256_otbn";

	memset(crypto_buf, 0x5A, sizeof(crypto_buf));
	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		const uint8_t *in = unaligned ? crypto_buf + 1 : crypto_buf;
		mbedtls_sha256_context c;
		uint64_t t0, t1;
		int rc;

		if (!digest_sha256(crypto_buf, size, ref)) {
			return false;
		}
		mbedtls_sha256_init(&c);
		rc = mbedtls_sha256_starts(&c, 0);
		t0 = k_cycle_get_64();
		rc |= mbedtls_sha256_update(&c, in, size);
		rc |= mbedtls_sha256_finish(&c, out);
		t1 = k_cycle_get_64();
		mbedtls_sha256_free(&c);
		if (rc != 0 || memcmp(ref, out, sizeof(out)) != 0) {
			return false;
		}
		tele_perf("crypto", name, size, k_cyc_to_us_floor64(t1 - t0));
	}
	return true;
}

static bool run_sha512_otbn(bool unaligned)
{
	uint8_t ref[64], out[64];
	const char *name = unaligned ? "sha512_otbn_unaligned" : "sha512_otbn";

	memset(crypto_buf, 0x5A, sizeof(crypto_buf));
	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		const uint8_t *in = unaligned ? crypto_buf + 1 : crypto_buf;
		mbedtls_sha512_context c;
		uint64_t t0, t1;
		int rc;

		if (!digest_sha512(crypto_buf, size, ref)) {
			return false;
		}
		mbedtls_sha512_init(&c);
		rc = mbedtls_sha512_starts(&c, 0);
		t0 = k_cycle_get_64();
		rc |= mbedtls_sha512_update(&c, in, size);
		rc |= mbedtls_sha512_finish(&c, out);
		t1 = k_cycle_get_64();
		mbedtls_sha512_free(&c);
		if (rc != 0 || memcmp(ref, out, sizeof(out)) != 0) {
			return false;
		}
		tele_perf("crypto", name, size, k_cyc_to_us_floor64(t1 - t0));
	}
	return true;
}

static bool run_sm3_otbn(bool unaligned)
{
	uint8_t ref[32], out[32];
	const char *name = unaligned ? "sm3_otbn_unaligned" : "sm3_otbn";

	memset(crypto_buf, 0x5A, sizeof(crypto_buf));
	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		const uint8_t *in = unaligned ? crypto_buf + 1 : crypto_buf;
		mbedtls_sm3_context c;
		uint64_t t0, t1;
		int rc;

		if (!digest_sm3(crypto_buf, size, ref)) {
			return false;
		}
		mbedtls_sm3_init(&c);
		rc = mbedtls_sm3_starts(&c);
		t0 = k_cycle_get_64();
		rc |= mbedtls_sm3_update(&c, in, size);
		rc |= mbedtls_sm3_finish(&c, out);
		t1 = k_cycle_get_64();
		mbedtls_sm3_free(&c);
		if (rc != 0 || memcmp(ref, out, sizeof(out)) != 0) {
			return false;
		}
		tele_perf("crypto", name, size, k_cyc_to_us_floor64(t1 - t0));
	}
	return true;
}

static bool run_rsa_modexp_otbn(void)
{
	static uint8_t out[256];
	uint64_t t0, t1;
	int rc;

	t0 = k_cycle_get_64();
	rc = mbedtls_ls_otbn_rsa_modexp(rsa2048_ciphertext, out,
					rsa2048_d, rsa2048_n, 256, false);
	t1 = k_cycle_get_64();
	if (rc != 0) {
		return false;
	}
	tele_ops("crypto", "rsa2048_modexp", 1, k_cyc_to_us_floor64(t1 - t0));
	return true;
}

void test_main(void)
{
	bool all_ok = true;

	printk("[CPU2 PERF_PEER] mbox echo + flash proxy peer started\n");

	/* The boot marker is relayed too, so the harness sees a CTRL stream on
	 * cpu1's console. */
	tele_line("ZT_BOOT CTRL");

	(void)mbox_register_callback(mbox_rx.dev, mbox_rx.channel_id,
				     mbox_rx_cb, NULL);
	(void)mbox_set_enabled(mbox_rx.dev, mbox_rx.channel_id, 1);

	k_thread_create(&echo_td, echo_stack, K_THREAD_STACK_SIZEOF(echo_stack),
			echo_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_name_set(&echo_td, "perf_echo");

	k_msleep(PERF_READY_DELAY_MS);

	struct perf_msg ready = {
		.magic = PERF_READY_MAGIC,
		.seq = 0,
	};
	struct mbox_msg msg = {
		.data = &ready,
		.size = sizeof(ready),
	};

	(void)mbox_send_dt(&mbox_tx, &msg);

	int64_t t0;

	t0 = k_uptime_get();
	report("sha256_otbn", run_sha256_otbn(false), k_uptime_get() - t0, &all_ok);
	t0 = k_uptime_get();
	report("sha256_otbn_unaligned", run_sha256_otbn(true), k_uptime_get() - t0, &all_ok);
	t0 = k_uptime_get();
	report("sha512_otbn", run_sha512_otbn(false), k_uptime_get() - t0, &all_ok);
	t0 = k_uptime_get();
	report("sha512_otbn_unaligned", run_sha512_otbn(true), k_uptime_get() - t0, &all_ok);
	t0 = k_uptime_get();
	report("sm3_otbn", run_sm3_otbn(false), k_uptime_get() - t0, &all_ok);
	t0 = k_uptime_get();
	report("sm3_otbn_unaligned", run_sm3_otbn(true), k_uptime_get() - t0, &all_ok);
	t0 = k_uptime_get();
	report("rsa2048_modexp", run_rsa_modexp_otbn(), k_uptime_get() - t0, &all_ok);

	t0 = k_uptime_get();
	report("proxy_erase", run_proxy_erase(), k_uptime_get() - t0, &all_ok);
	t0 = k_uptime_get();
	report("proxy_read", run_proxy_read(), k_uptime_get() - t0, &all_ok);
	t0 = k_uptime_get();
	report("proxy_write", run_proxy_write(), k_uptime_get() - t0, &all_ok);
	t0 = k_uptime_get();
	report("peer_alive", run_peer_alive(), k_uptime_get() - t0, &all_ok);

	tele_line("PROJECT EXECUTION %s", all_ok ? "SUCCESSFUL" : "FAILED");

	k_sleep(K_FOREVER);
}
