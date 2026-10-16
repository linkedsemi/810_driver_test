/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN RSA delegation test body (case_rsa_delegation only).
 *
 * Compiled INSTEAD OF otbn_delegation_test.c when OTBN_DELEG_RSA is set (see
 * the cpu<N>/CMakeLists.txt), so the RSA item set runs on its own and its
 * channel-masking item cannot wedge the ECDSA matrix (the ECDSA items would be
 * linked too, and the link order is by name -- 'r' < 't').
 *
 * Items run on BOTH cores, each on its own backend:
 *   cpu2 (CLIENT, uart5): the explicit proxy mbedtls_ls_otbn_rsa_modexp_
 *                         encrypt()/_decrypt() -- every call is a real mbox
 *                         round-trip to cpu1.
 *   cpu1 (SERVER, uart1): the local byte-level backend
 *                         mbedtls_ls_otbn_rsa_modexp() (rsa_alt.c) -- the
 *                         independent reference the proxied result is checked
 *                         against.
 * So a double PASS on r01/r02/r03 means "channel up + proxy round-trip correct
 * AND local backend correct".
 *
 * r90 is client-only: masking the client's otbn_crypto channel, the explicit
 * RSA call must give up (bus timeout) rather than return a locally computed
 * result -- software RSA would return rc=0 at once, so no correct result alone
 * could tell a real round-trip from a local fallback. It runs LAST because it
 * leaves the channel unusable; the server registers it for name symmetry and
 * takes the SKIP branch.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/sys_clock.h>

#include "ztest_glue.h"

#include "mbedtls_otbn_rsa_mbox.h"
#include "otbn_delegation_rsa_vectors.h"

#define RSA_MAX_BYTES 512	/* 4096-bit modulus */

/*
 * Backend dispatch. The two entry points have different shapes: the client's
 * _encrypt/_decrypt take (in, exp-or-d, n, n_size_bits, out); the server's
 * byte-level backend takes (in, out, exp-or-d, n, num_bytes, is_public). Both
 * work on big-endian buffers of n_size_bits/8 bytes.
 */
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
#define RSA_HAVE_BACKEND 1
#define RSA_ENCRYPT(in, exp, n, bits, out) \
	mbedtls_ls_otbn_rsa_modexp_encrypt((in), (exp), (n), (bits), (out))
#define RSA_DECRYPT(in, d, n, bits, out) \
	mbedtls_ls_otbn_rsa_modexp_decrypt((in), (d), (n), (bits), (out))
#elif defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)
#define RSA_HAVE_BACKEND 1
#define RSA_ENCRYPT(in, exp, n, bits, out) \
	mbedtls_ls_otbn_rsa_modexp((in), (out), (exp), (n), (bits) / 8U, true)
#define RSA_DECRYPT(in, d, n, bits, out) \
	mbedtls_ls_otbn_rsa_modexp((in), (out), (d), (n), (bits) / 8U, false)
#else
#define RSA_HAVE_BACKEND 0
#endif

/* Wall-clock ms timing: a modexp is slow enough that k_cycle_get_32()'s 32-bit
 * delta would wrap (2^32 / CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC, ~7.16 s on this
 * board), and r90 deliberately waits ~10 s. k_uptime_get() is 64-bit ms. */
static void rsa_roundtrip(const uint8_t *n, const uint8_t *e, const uint8_t *d,
			  const uint8_t *m, uint32_t n_bits, const char *tag)
{
#if RSA_HAVE_BACKEND
	static __attribute__((aligned(32))) uint8_t c[RSA_MAX_BYTES];
	static __attribute__((aligned(32))) uint8_t m2[RSA_MAX_BYTES];
	const size_t nb = n_bits / 8U;
	size_t diff = nb;
	int64_t t0;
	int rc_e, rc_d;

	t0 = k_uptime_get();
	rc_e = RSA_ENCRYPT(m, e, n, n_bits, c);
	ZT_INFO("rsa_enc", "%s,rc=%d,ms=%d", tag, rc_e, (int)(k_uptime_get() - t0));
	zassert_equal(rc_e, 0, "%s: RSA modexp (public) rc=%d", tag, rc_e);

	t0 = k_uptime_get();
	rc_d = RSA_DECRYPT(c, d, n, n_bits, m2);
	ZT_INFO("rsa_dec", "%s,rc=%d,ms=%d", tag, rc_d, (int)(k_uptime_get() - t0));
	zassert_equal(rc_d, 0, "%s: RSA modexp (private) rc=%d", tag, rc_d);

	for (size_t i = 0; i < nb; i++) {
		if (m2[i] != m[i]) {
			diff = i;
			break;
		}
	}
	zassert_true(diff == nb,
		     "%s: modexp round-trip mismatch at byte %u (m' != m)",
		     tag, (unsigned)diff);
	ZT_INFO("rsa_roundtrip", "%s,bits=%u,ok=1", tag, (unsigned)n_bits);
#else
	ARG_UNUSED(n); ARG_UNUSED(e); ARG_UNUSED(d); ARG_UNUSED(m);
	ARG_UNUSED(n_bits); ARG_UNUSED(tag);
	ztest_test_skip();
#endif
}

ZTEST(otbn_deleg, r01_rsa_modexp_roundtrip_2048)
{
	rsa_roundtrip(rsa2048_n, rsa2048_e, rsa2048_d, rsa2048_m, 2048, "rsa2048");
}

ZTEST(otbn_deleg, r02_rsa_modexp_roundtrip_4096)
{
	rsa_roundtrip(rsa4096_n, rsa4096_e, rsa4096_d, rsa4096_m, 4096, "rsa4096");
}

/*
 * r03: modulus sizes the OTBN RSA firmware does not implement (it has 2048 /
 * 3072 / 4096) must be rejected, never silently computed. 1024 and 1536 bits are
 * outside the set: the client rejects before any mbox traffic (-EINVAL), the
 * server rejects in the byte-level backend (-ENOTSUP).
 */
ZTEST(otbn_deleg, r03_reject_unsupported_modulus)
{
#if RSA_HAVE_BACKEND
	static __attribute__((aligned(32))) uint8_t out[RSA_MAX_BYTES];
	int rc_1024, rc_1536;

	rc_1024 = RSA_ENCRYPT(rsa2048_m, rsa2048_e, rsa2048_n, 1024, out);
	rc_1536 = RSA_ENCRYPT(rsa2048_m, rsa2048_e, rsa2048_n, 1536, out);

	ZT_INFO("rsa_bad_modulus", "rc_1024=%d,rc_1536=%d", rc_1024, rc_1536);
	zassert_true(rc_1024 != 0, "1024-bit modulus accepted rc=%d", rc_1024);
	zassert_true(rc_1536 != 0, "1536-bit modulus accepted rc=%d", rc_1536);
#else
	ztest_test_skip();
#endif
}

/*
 * r90 (client only): mailbox-bound proof. Mask the client's otbn_crypto
 * channel, then call the explicit RSA proxy: the call must return non-zero (the
 * bus gives up after OTBN_DELEG_TIMEOUT_RSA_MS) and must have taken most of that
 * bound. A locally computed result returns rc=0 in well under a second, so this
 * observable cannot be produced without the bus path. Runs LAST; masking the
 * channel leaves the proxy unusable for the rest of the suite.
 */
#define RSA_PROBE_TIMEOUT_MS 15000U

#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
struct rsa_probe {
	struct k_sem done;
	int rc;
	uint32_t ms;
};

static void rsa_probe_thread(void *p1, void *p2, void *p3)
{
	struct rsa_probe *pp = p1;
	static __attribute__((aligned(32))) uint8_t out[RSA_MAX_BYTES];
	int64_t t0 = k_uptime_get();

	pp->rc = mbedtls_ls_otbn_rsa_modexp_encrypt(rsa2048_m, rsa2048_e,
						    rsa2048_n, 2048, out);
	pp->ms = (uint32_t)(k_uptime_get() - t0);
	k_sem_give(&pp->done);
}
#endif /* CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT */

ZTEST(otbn_deleg, r90_rsa_mailbox_bound)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	static struct rsa_probe pp;
	static struct k_thread pt;
	static K_THREAD_STACK_DEFINE(rsa_probe_stk, 8192);
	int got;

	/* Mask our own otbn_crypto channel: no reply can reach the proxy now.
	 * mbox_set_enabled(false) masks the IRQ, so the send is still accepted
	 * but no reply is ever delivered (same mechanism as the ECDSA t90). */
	{
		static const struct mbox_dt_spec sp =
			MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_consumer_otbn_crypto), mbox);

		zassert_equal(mbox_set_enabled(sp.dev, sp.channel_id, false), 0,
			      "could not mask the otbn_crypto channel");
	}

	k_sem_init(&pp.done, 0, 1);
	pp.rc = 0x7FFFFFFF;
	pp.ms = 0U;
	k_thread_create(&pt, rsa_probe_stk, 8192, rsa_probe_thread, &pp, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	got = k_sem_take(&pp.done, K_MSEC(RSA_PROBE_TIMEOUT_MS));

	ZT_INFO("rsa_mailbox_bound", "returned=%d,rc=%d,elapsed_ms=%u,timeout_ms=%u",
		got == 0, pp.rc, (unsigned)pp.ms, (unsigned)RSA_PROBE_TIMEOUT_MS);

	zassert_equal(got, 0,
		"RSA proxy call did not return within %u ms after the channel "
		"was masked", (unsigned)RSA_PROBE_TIMEOUT_MS);
	zassert_true(pp.rc != 0,
		"explicit RSA proxy returned rc=%d with the otbn_crypto channel "
		"masked -- it is NOT bound to the mailbox (local?)", pp.rc);
	/* The bus bound is OTBN_DELEG_TIMEOUT_RSA_MS (10 s); a local computation
	 * returns in well under 1 s. Require most of the bound to have elapsed. */
	zassert_true(pp.ms >= 8000U,
		"RSA call returned in %u ms -- too fast for the 10 s bus timeout "
		"(local, not proxied?)", (unsigned)pp.ms);
#else
	ztest_test_skip();
#endif
}

/* 6 explicit params: setup/teardown are suite-level, before/after per-test --
 * all NULL here (no shared fixture). Same name as the ECDSA body's suite so the
 * harness sees one suite regardless of which body a case selects. */
ZTEST_SUITE(otbn_deleg, NULL, NULL, NULL, NULL, NULL);
