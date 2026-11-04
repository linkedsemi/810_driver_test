/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN ECDSA delegation test bodies -- shared by BOTH cores.
 *
 * The SAME test code is compiled into both cpu2 (delegation CLIENT) and cpu1
 * (delegation SERVER). The only difference is the build config:
 *   - cpu2: CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT=y
 *           The image always builds the delegation stubs in
 *           zephyr/soc/linkedsemi/lsqsh/otbn/mbedtls/mbedtls_otbn_crypto_mbox.c
 *           (mbedtls_ls_otbn_ecdsa_*). WHICH local name reaches them is set by
 *           the ECDSA alternative choice:
 *             * entry = local OTBN or off  -> mbedtls_ecdsa_* is NOT the proxy,
 *               so DELEG_* maps to the explicit names (cases default/
 *               dead_channel/no_server/explicit_api);
 *             * entry = via delegation (CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_
 *               ALT_VIA_DELEGATION=y) -> mbedtls_ecdsa_* IS the proxy, and
 *               DELEG_* maps to the plain names (case_alt_via_proxy).
 *           Either way each delegated request crosses the otbn_crypto mbox
 *           channel and is answered by cpu1.
 *   - cpu1: CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER=y
 *           => mbedtls_ecdsa_* are the real OTBN-accelerated implementations;
 *              the core also services cpu2's mbox requests from a dedicated
 *              delegation thread.
 *
 * Judgement (810 dual-core, same-name items on both cores) = item PASS on
 * CTRL(cpu2) AND TGT(cpu1). A delegated operation succeeds on cpu2 only if
 * cpu1 answered it, so a double PASS means "channel up + proxy round-trip
 * correct"; cpu1's own PASS means "local OTBN ECDSA correct".
 *
 * Test order is the numeric prefix order (link order = name order). The
 * negative cases were once forced last because the client-side genkey stub
 * leaked client_sem on an out-of-whitelist curve (took the sem, then returned
 * -EIO through a path without the matching give). That is fixed: the curve
 * whitelist is now checked before the bus takes client_sem, and the bus
 * releases it on every exit path (see otbn_delegation.c). Order no longer
 * matters for channel health -- do not "fix" a leak that is not there.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/sys_clock.h>

#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"

/* Error-domain contract: driver codes vs the mbedtls boundary (t91). */
#include "ls_otbn_err_mbedtls.h"

/*
 * Interface redirection -- selected by ROLE and by which ECDSA surface the
 * client's build routes to the proxy.
 *
 * The delegation CLIENT has two ECDSA surfaces:
 *   - the explicit mbedtls_ls_otbn_ecdsa_* entry points (mbox.c), which the
 *     client image always builds, and
 *   - the mbedtls_ecdsa_* names, which reach the proxy ONLY when the OTBN ECDSA
 *     alternative is routed through the delegation
 *     (CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_VIA_DELEGATION); in every other
 *     client config ecdsa_alt.c or ecdsa.c owns them instead.
 *
 * So the redirect has two client modes:
 *   - client WITHOUT ALT_VIA_DELEGATION (cases default/dead_channel/no_server/
 *     explicit_api): the plain names are software/ecdsa_alt.c, NOT the proxy, so
 *     the matrix must call the explicit names to exercise the proxy.
 *   - client WITH ALT_VIA_DELEGATION (case_alt_via_proxy): the plain names ARE
 *     the proxy (mbedtls_ecdsa_* forwards to the mbox stub), so the matrix calls
 *     the plain names -- that is the whole point of that case: prove the plain
 *     API an application/TLS/pk would use is really forwarded to the peer.
 *
 * The SERVER builds no delegation client, so it keeps the mbedtls_ecdsa_* names:
 * on that image they are the local OTBN implementation (ecdsa_alt.c), i.e. the
 * independent reference the client's proxied results are compared against.
 */
/* Channel bring-up entry points (ls_otbn_delegation_client_init /
 * _server_init), used by t16c_repeat_channel_init on BOTH roles. Keep this
 * include OUTSIDE the DELEG_* selection below, which is the client
 * explicit-API-only branch: the alt_via_proxy client and every server build
 * take the #else arm and still need these declarations. */
#include "ls_otbn_delegation.h"

#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT) && \
	!defined(CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_VIA_DELEGATION)
#include "mbedtls_otbn_ecdsa_mbox.h"
#define DELEG_GENKEY  mbedtls_ls_otbn_ecdsa_genkey
#define DELEG_SIGN    mbedtls_ls_otbn_ecdsa_sign
#define DELEG_VERIFY  mbedtls_ls_otbn_ecdsa_verify
#else
#define DELEG_GENKEY  mbedtls_ecdsa_genkey
#define DELEG_SIGN    mbedtls_ecdsa_sign
#define DELEG_VERIFY  mbedtls_ecdsa_verify
#endif

/* ZT_INFO comes from the 810 common ztest glue; this TU must include it. */
#include "ztest_glue.h"

/*
 * Deterministic 32-byte digest. Same value for both cores; the delegated and
 * the local backend sign exactly these bytes, so the two remain comparable.
 */
static const uint8_t test_hash[32] = {
	0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
	0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
	0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
	0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD,
};

/*
 * Deterministic pseudo-random generator. Required by the local (server)
 * mbedtls_ecdsa_genkey/sign; the delegation client stubs ignore it, so a
 * non-cryptographic stream is sufficient and keeps the test reproducible.
 */
static int test_rng(void *ctx, unsigned char *out, size_t len)
{
	static uint32_t s = 0x12345678u;

	(void)ctx;
	for (size_t i = 0; i < len; i++) {
		s = s * 1103515245u + 12345u;
		out[i] = (uint8_t)(s >> 16);
	}
	return 0;
}

static void ctx_init(mbedtls_ecdsa_context *ctx)
{
	mbedtls_ecp_group_init(&ctx->private_grp);
	mbedtls_ecp_point_init(&ctx->private_Q);
	mbedtls_mpi_init(&ctx->private_d);
}

static void ctx_free(mbedtls_ecdsa_context *ctx)
{
	mbedtls_ecp_group_free(&ctx->private_grp);
	mbedtls_ecp_point_free(&ctx->private_Q);
	mbedtls_mpi_free(&ctx->private_d);
}

/*
 * Is our own proxy channel usable? Decided at COMPILE time via the case-level
 * flags that test.sh passes (the vnd,mbox-consumer node must stay enabled --
 * disabling it breaks the mbedtls_otbn_crypto_mbox.c build itself):
 *   - case_dead_channel (-DOTBN_DELEG_DEAD_CHANNEL): t16 disables the channel at
 *     run time (mbox_set_enabled(false)) so the send itself fails (ignored).
 *   - case_no_server    (-DOTBN_DELEG_NO_SERVER):   peer built without the
 *     delegation server, so the send succeeds but no reply ever comes.
 * In both the functional items are skipped and only t16 runs. N/A on the server.
 */
static bool client_proxy_ready(void)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT) && \
	(defined(OTBN_DELEG_NO_SERVER) || defined(OTBN_DELEG_DEAD_CHANNEL))
	return false;
#else
	return true;
#endif
}

#define DELEG_GATE()                                    \
	do {                                            \
		if (!client_proxy_ready()) {            \
			ztest_test_skip();              \
			return;                         \
		}                                       \
	} while (0)

/*
 * Generate a fresh key pair on the backend in use. No caller-side pre-load:
 * every genkey entry point loads ctx->grp from gid itself -- the software one
 * (crypto/mbedtls/library/ecdsa.c), the LOCAL OTBN alternative (ecdsa_alt.c),
 * and the delegation proxy (mbedtls_otbn_crypto_mbox.c) -- so ctx->private_grp
 * is populated right after this call on every backend (the N / nbits / check_*
 * reads below rely on it).
 */
static void genkey_or_fail(mbedtls_ecdsa_context *ctx, mbedtls_ecp_group_id gid)
{
	int rc = DELEG_GENKEY(ctx, gid, test_rng, NULL);

	zassert_equal(rc, 0, "ecdsa_genkey gid=%d rc=%d", (int)gid, rc);
}

/* Sign test_hash; returns r/s in the caller's mpis (caller owns/init them). */
static void sign_or_fail(mbedtls_ecdsa_context *ctx, mbedtls_mpi *r, mbedtls_mpi *s)
{
	int rc = DELEG_SIGN(&ctx->private_grp, r, s, &ctx->private_d,
				    test_hash, sizeof(test_hash), test_rng, NULL);

	zassert_equal(rc, 0, "ecdsa_sign rc=%d", rc);
}

/* --- shared assertions ------------------------------------------------- */

static void assert_genkey_ok(mbedtls_ecp_group_id gid)
{
	mbedtls_ecdsa_context ctx;
	mbedtls_ecp_point q2;
	unsigned char pub[1 + 2 * 66];
	size_t nsize, olen = 0;

	DELEG_GATE();
	ctx_init(&ctx);
	genkey_or_fail(&ctx, gid);
	nsize = (ctx.private_grp.nbits + 7) / 8;

	/* d must be a non-zero scalar strictly below the group order n. */
	zassert_true(mbedtls_mpi_cmp_int(&ctx.private_d, 0) > 0,
		     "gid=%d private_d == 0", (int)gid);
	zassert_true(mbedtls_mpi_cmp_mpi(&ctx.private_d, &ctx.private_grp.N) < 0,
		     "gid=%d private_d >= n", (int)gid);

	/* Standard contract A: d is accepted by the standard private-key check. */
	zassert_equal(mbedtls_ecp_check_privkey(&ctx.private_grp, &ctx.private_d), 0,
		      "gid=%d private key rejected by mbedtls_ecp_check_privkey", (int)gid);

	/* Standard contract B: Q is a valid point on the curve. */
	zassert_equal(mbedtls_ecp_check_pubkey(&ctx.private_grp, &ctx.private_Q), 0,
		      "gid=%d public key not on curve", (int)gid);

	/* Standard contract C: Q round-trips through the canonical point codec.
	 * With the pre-fix Q.Z == 0 (mbedtls' "point at infinity") write_binary()
	 * still returns 0 but emits the 1-byte infinity encoding, so the encoded
	 * length check below is the guard that pins the genkey Z=1 fix.
	 */
	zassert_equal(mbedtls_ecp_point_write_binary(&ctx.private_grp, &ctx.private_Q,
			MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, pub, sizeof(pub)), 0,
		"gid=%d public key not exportable", (int)gid);
	zassert_equal(olen, 1 + 2 * nsize,
		"gid=%d exported point len %u != %u (Z==0 -> infinity point?)",
		(int)gid, (unsigned)olen, (unsigned)(1 + 2 * nsize));

	mbedtls_ecp_point_init(&q2);
	zassert_equal(mbedtls_ecp_point_read_binary(&ctx.private_grp, &q2, pub, olen), 0,
		"gid=%d public key re-import failed", (int)gid);
	zassert_equal(mbedtls_mpi_cmp_mpi(&q2.private_X, &ctx.private_Q.private_X), 0,
		"gid=%d re-imported X mismatch", (int)gid);
	zassert_equal(mbedtls_mpi_cmp_mpi(&q2.private_Y, &ctx.private_Q.private_Y), 0,
		"gid=%d re-imported Y mismatch", (int)gid);
	zassert_equal(mbedtls_mpi_cmp_mpi(&q2.private_Z, &ctx.private_Q.private_Z), 0,
		"gid=%d re-imported Z mismatch", (int)gid);
	mbedtls_ecp_point_free(&q2);

	ZT_INFO("genkey", "gid=%d,nbits=%u,pt_enc_len=%u", (int)gid,
		(unsigned)ctx.private_grp.nbits, (unsigned)olen);

	ctx_free(&ctx);
}

static void assert_sign_in_range(mbedtls_ecp_group_id gid)
{
	mbedtls_ecdsa_context ctx;
	mbedtls_mpi r, s;

	DELEG_GATE();
	ctx_init(&ctx);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	genkey_or_fail(&ctx, gid);
	sign_or_fail(&ctx, &r, &s);

	/* Signature scalars must lie in [1, n-1]. */
	zassert_true(mbedtls_mpi_cmp_int(&r, 0) > 0, "gid=%d r == 0", (int)gid);
	zassert_true(mbedtls_mpi_cmp_mpi(&r, &ctx.private_grp.N) < 0,
		     "gid=%d r >= n", (int)gid);
	zassert_true(mbedtls_mpi_cmp_int(&s, 0) > 0, "gid=%d s == 0", (int)gid);
	zassert_true(mbedtls_mpi_cmp_mpi(&s, &ctx.private_grp.N) < 0,
		     "gid=%d s >= n", (int)gid);

	ZT_INFO("sign", "gid=%d", (int)gid);

	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	ctx_free(&ctx);
}

/*
 * Verify round-trip. The tampered branch is the important one: a delegated
 * verify() that always reports success (e.g. because the server status never
 * reaches the client) would still return 0 for a valid signature, so only the
 * tampered case can tell a working proxy from a no-op one. It must be a hard
 * assertion, never a soft/INFO.
 */
static void assert_verify_roundtrip(mbedtls_ecp_group_id gid)
{
	mbedtls_ecdsa_context ctx;
	mbedtls_mpi r, s, n, one, r_bad;

	DELEG_GATE();
	ctx_init(&ctx);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);
	mbedtls_mpi_init(&n);
	mbedtls_mpi_init(&one);
	mbedtls_mpi_init(&r_bad);

	genkey_or_fail(&ctx, gid);
	sign_or_fail(&ctx, &r, &s);

	zassert_equal(DELEG_VERIFY(&ctx.private_grp, test_hash,
					    sizeof(test_hash), &ctx.private_Q, &r, &s),
		      0, "gid=%d valid signature rejected", (int)gid);

	/* Tamper r -> r_bad = (r + 1) mod n, guaranteed different from r. */
	mbedtls_mpi_lset(&one, 1);
	mbedtls_mpi_copy(&n, &ctx.private_grp.N);
	mbedtls_mpi_add_mpi(&r_bad, &r, &one);
	mbedtls_mpi_mod_mpi(&r_bad, &r_bad, &n);

	zassert_true(mbedtls_mpi_cmp_mpi(&r_bad, &r) != 0, "tamper made no change");

	zassert_true(DELEG_VERIFY(&ctx.private_grp, test_hash,
					   sizeof(test_hash), &ctx.private_Q,
					   &r_bad, &s) != 0,
		     "gid=%d tampered signature accepted (verify is a no-op)",
		     (int)gid);

	ZT_INFO("verify", "gid=%d,tamper_rejected=1", (int)gid);

	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	mbedtls_mpi_free(&n);
	mbedtls_mpi_free(&one);
	mbedtls_mpi_free(&r_bad);
	ctx_free(&ctx);
}

/* --- interface-usage helpers ------------------------------------------- */

/*
 * Sign a message of a caller-chosen length. Standard contract: mbedtls signs
 * the leftmost min(blen, n_size) bits, so any blen is accepted and r/s still
 * land in [1, n-1].
 */
static void assert_sign_with_blen(mbedtls_ecp_group_id gid, size_t blen)
{
	mbedtls_ecdsa_context ctx;
	mbedtls_mpi r, s;
	unsigned char buf[96];

	DELEG_GATE();
	memset(buf, 0x5A, sizeof(buf));
	if (blen > sizeof(buf)) {
		blen = sizeof(buf);
	}

	ctx_init(&ctx);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);
	genkey_or_fail(&ctx, gid);

	zassert_equal(DELEG_SIGN(&ctx.private_grp, &r, &s, &ctx.private_d,
			buf, blen, test_rng, NULL), 0,
		"gid=%d sign blen=%u failed", (int)gid, (unsigned)blen);
	zassert_true(mbedtls_mpi_cmp_int(&r, 0) > 0 &&
		     mbedtls_mpi_cmp_mpi(&r, &ctx.private_grp.N) < 0,
		"gid=%d r out of range (blen=%u)", (int)gid, (unsigned)blen);
	zassert_true(mbedtls_mpi_cmp_int(&s, 0) > 0 &&
		     mbedtls_mpi_cmp_mpi(&s, &ctx.private_grp.N) < 0,
		"gid=%d s out of range (blen=%u)", (int)gid, (unsigned)blen);

	ZT_INFO("sign_blen", "gid=%d,blen=%u", (int)gid, (unsigned)blen);

	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	ctx_free(&ctx);
}

/*
 * Standard contract: verify() must reject a signature scalar outside [1, n-1]
 * (r = n, or s = 0) even for an otherwise valid signature.
 */
static void assert_verify_rejects_bad_scalar(mbedtls_ecp_group_id gid, bool bad_r)
{
	mbedtls_ecdsa_context ctx;
	mbedtls_mpi r, s;

	DELEG_GATE();
	ctx_init(&ctx);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);
	genkey_or_fail(&ctx, gid);
	sign_or_fail(&ctx, &r, &s);

	if (bad_r) {
		mbedtls_mpi_copy(&r, &ctx.private_grp.N);   /* r = n */
	} else {
		mbedtls_mpi_lset(&s, 0);                    /* s = 0 */
	}

	zassert_true(DELEG_VERIFY(&ctx.private_grp, test_hash,
			sizeof(test_hash), &ctx.private_Q, &r, &s) != 0,
		"gid=%d verify accepted out-of-range %s", (int)gid, bad_r ? "r=n" : "s=0");

	ZT_INFO("verify_bad_scalar", "gid=%d,bad=%s", (int)gid, bad_r ? "r=n" : "s=0");

	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	ctx_free(&ctx);
}

/* Concurrent genkey: client_sem serialises proxy calls, so two threads must
 * both complete (serially), never deadlock. */
struct genkey_job {
	struct k_sem done;
	mbedtls_ecp_group_id gid;
	int rc;
};

static void genkey_job_thread(void *p1, void *p2, void *p3)
{
	struct genkey_job *j = p1;
	mbedtls_ecdsa_context ctx;

	ctx_init(&ctx);
	j->rc = DELEG_GENKEY(&ctx, j->gid, test_rng, NULL);
	ctx_free(&ctx);
	k_sem_give(&j->done);
}

#define CONC_STACK 8192
static K_THREAD_STACK_DEFINE(conc_stk_a, CONC_STACK);
static K_THREAD_STACK_DEFINE(conc_stk_b, CONC_STACK);

static void assert_concurrent_genkey(mbedtls_ecp_group_id gid)
{
	static struct genkey_job ja, jb;
	static struct k_thread ta, tb;
	int ok_a, ok_b;

	DELEG_GATE();
	k_sem_init(&ja.done, 0, 1);
	k_sem_init(&jb.done, 0, 1);
	ja.gid = jb.gid = gid;
	ja.rc = jb.rc = -1;

	k_thread_create(&ta, conc_stk_a, CONC_STACK, genkey_job_thread, &ja,
			NULL, NULL, K_PRIO_PREEMPT(6), 0, K_NO_WAIT);
	k_thread_create(&tb, conc_stk_b, CONC_STACK, genkey_job_thread, &jb,
			NULL, NULL, K_PRIO_PREEMPT(6), 0, K_NO_WAIT);

	ok_a = k_sem_take(&ja.done, K_MSEC(8000));
	ok_b = k_sem_take(&jb.done, K_MSEC(8000));

	ZT_INFO("concurrent_genkey", "gid=%d,a_done=%d,b_done=%d,rc=%d/%d",
		(int)gid, ok_a == 0, ok_b == 0, ja.rc, jb.rc);

	zassert_equal(ok_a, 0, "gid=%d thread A did not finish (deadlock?)", (int)gid);
	zassert_equal(ok_b, 0, "gid=%d thread B did not finish (deadlock?)", (int)gid);
	zassert_equal(ja.rc, 0, "gid=%d thread A genkey rc=%d", (int)gid, ja.rc);
	zassert_equal(jb.rc, 0, "gid=%d thread B genkey rc=%d", (int)gid, jb.rc);
}

/*
 * --- test matrix: {P256, P384, SM2} x {genkey, sign, verify} + negative ---
 * Names carry a numeric prefix so the execution order is deterministic and
 * matches link order (the negative case must run last -- see file header).
 */

ZTEST(otbn_deleg, t01_genkey_p256)
{
	assert_genkey_ok(MBEDTLS_ECP_DP_SECP256R1);
}

ZTEST(otbn_deleg, t02_genkey_p384)
{
	assert_genkey_ok(MBEDTLS_ECP_DP_SECP384R1);
}

ZTEST(otbn_deleg, t03_genkey_sm2)
{
	assert_genkey_ok(MBEDTLS_ECP_DP_SM2);
}

ZTEST(otbn_deleg, t04_sign_p256)
{
	assert_sign_in_range(MBEDTLS_ECP_DP_SECP256R1);
}

ZTEST(otbn_deleg, t05_sign_p384)
{
	assert_sign_in_range(MBEDTLS_ECP_DP_SECP384R1);
}

ZTEST(otbn_deleg, t06_sign_sm2)
{
	assert_sign_in_range(MBEDTLS_ECP_DP_SM2);
}

ZTEST(otbn_deleg, t07_verify_p256)
{
	assert_verify_roundtrip(MBEDTLS_ECP_DP_SECP256R1);
}

ZTEST(otbn_deleg, t08_verify_p384)
{
	assert_verify_roundtrip(MBEDTLS_ECP_DP_SECP384R1);
}

ZTEST(otbn_deleg, t09_verify_sm2)
{
	assert_verify_roundtrip(MBEDTLS_ECP_DP_SM2);
}

/*
 * Negative case: a curve outside the client whitelist (P256/P384/SM2) must be
 * rejected, not silently accepted. MBEDTLS_ECP_DP_NONE is the "no curve"
 * sentinel: the client stub rejects it before any mbox traffic, and the local
 * backend fails mbedtls_ecp_group_load().
 *
 * On the CLIENT this also covers the sign/verify buffer-sizing fix: those
 * stubs size their fixed MAX_OTBN_CURVE_LEN (48-byte) buffers from the curve
 * whitelist, not from grp->nbits. A real, loadable non-whitelist curve --
 * P-521, compiled in via CONFIG_MBEDTLS_ECP_ALL_ENABLED, nbits 521 => a
 * 66-byte n_size -- used to overflow those buffers; it must now be rejected
 * with -EIO before any buffer is written. The server keeps its own whitelist
 * (ecdsa_curve_supported) and is not exercised by this extra block.
 */
ZTEST(otbn_deleg, t10_reject_unsupported_curve)
{
	mbedtls_ecdsa_context ctx;
	int rc;

	ctx_init(&ctx);
	rc = DELEG_GENKEY(&ctx, MBEDTLS_ECP_DP_NONE, test_rng, NULL);
	zassert_true(rc != 0, "unsupported curve accepted rc=%d", rc);

	ZT_INFO("reject_curve", "rc=%d", rc);
	ctx_free(&ctx);

#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	{
		/* A caller may legitimately ask for a curve the proxy does not
		 * serve. Load it for real so grp.nbits == 521 (the pre-fix code
		 * would then derive n_size = 66 and write past the 48-byte
		 * buffers): the stub must key off grp->id and return -EIO
		 * without touching them, and must not crash. */
		mbedtls_ecp_group grp;
		mbedtls_mpi d, r, s;
		mbedtls_ecp_point q;

		mbedtls_ecp_group_init(&grp);
		mbedtls_mpi_init(&d);
		mbedtls_mpi_init(&r);
		mbedtls_mpi_init(&s);
		mbedtls_ecp_point_init(&q);
		mbedtls_mpi_lset(&d, 1);

		zassert_equal(mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP521R1), 0,
			      "P-521 group failed to load");
		zassert_equal(grp.nbits, 521, "P-521 nbits=%u", (unsigned)grp.nbits);

		rc = DELEG_SIGN(&grp, &r, &s, &d, test_hash, sizeof(test_hash),
				test_rng, NULL);
		zassert_equal(rc, MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE,
			      "non-whitelist curve sign rc=%d", rc);

		rc = DELEG_VERIFY(&grp, test_hash, sizeof(test_hash), &q, &r, &s);
		zassert_equal(rc, MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE,
			      "non-whitelist curve verify rc=%d", rc);

		mbedtls_ecp_point_free(&q);
		mbedtls_mpi_free(&s);
		mbedtls_mpi_free(&r);
		mbedtls_mpi_free(&d);
		mbedtls_ecp_group_free(&grp);
	}
#endif
}

/* --- interface-usage variants ------------------------------------------ */

ZTEST(otbn_deleg, t11_sign_blen_lt_n)
{
	assert_sign_with_blen(MBEDTLS_ECP_DP_SECP256R1, 20);   /* 20 < n_size 32 */
}

ZTEST(otbn_deleg, t12_sign_blen_gt_n)
{
	assert_sign_with_blen(MBEDTLS_ECP_DP_SECP384R1, 58);   /* 58 > n_size 48 */
}

ZTEST(otbn_deleg, t13_verify_r_out_of_range)
{
	assert_verify_rejects_bad_scalar(MBEDTLS_ECP_DP_SECP256R1, true);
}

ZTEST(otbn_deleg, t14_verify_s_zero)
{
	assert_verify_rejects_bad_scalar(MBEDTLS_ECP_DP_SM2, false);
}

ZTEST(otbn_deleg, t15_concurrent_genkey)
{
	assert_concurrent_genkey(MBEDTLS_ECP_DP_SECP256R1);
}

/* --- contract negatives (invalid inputs must be rejected) --------------- */

enum neg_op {
	NEG_SIGN_D_ZERO,       /* sign with d == 0            -> must fail */
	NEG_SIGN_D_N,          /* sign with d == n            -> must fail */
	NEG_VERIFY_Q_OFF,      /* verify with Q.X+1 (off curve) -> must fail */
	NEG_VERIFY_Q_INF,      /* verify with Q = infinity (Z=0) -> must fail */
	NEG_VERIFY_WRONG_KEY,  /* verify a sig with another key pair -> must fail */
};

/* Runs one invalid-input op; returns the mbedtls rc (0 == wrongly accepted). */
static int run_neg_op(enum neg_op op, mbedtls_ecp_group_id gid)
{
	mbedtls_ecdsa_context ctx;
	mbedtls_mpi r, s, d, one;
	mbedtls_ecp_point qbad;
	int rc = -1;

	ctx_init(&ctx);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);
	mbedtls_mpi_init(&d);
	mbedtls_mpi_init(&one);
	mbedtls_ecp_point_init(&qbad);

	if (mbedtls_ecp_group_load(&ctx.private_grp, gid) != 0) {
		goto out;
	}

	if (op == NEG_SIGN_D_ZERO || op == NEG_SIGN_D_N) {
		if (op == NEG_SIGN_D_ZERO) {
			mbedtls_mpi_lset(&d, 0);
		} else {
			mbedtls_mpi_copy(&d, &ctx.private_grp.N);
		}
		rc = DELEG_SIGN(&ctx.private_grp, &r, &s, &d, test_hash,
					sizeof(test_hash), test_rng, NULL);
	} else {
		/* Valid key + signature first, then corrupt the verification input. */
		if (DELEG_GENKEY(&ctx, gid, test_rng, NULL) != 0) {
			goto out;
		}
		if (DELEG_SIGN(&ctx.private_grp, &r, &s, &ctx.private_d,
				       test_hash, sizeof(test_hash), test_rng, NULL) != 0) {
			goto out;
		}
		if (op == NEG_VERIFY_WRONG_KEY) {
			mbedtls_ecdsa_context ctx2;

			ctx_init(&ctx2);
			/* No curve pre-load: DELEG_GENKEY below loads ctx2.grp. */
			if (DELEG_GENKEY(&ctx2, gid, test_rng, NULL) != 0) {
				ctx_free(&ctx2);
				goto out;
			}
			rc = DELEG_VERIFY(&ctx.private_grp, test_hash,
					sizeof(test_hash), &ctx2.private_Q, &r, &s);
			ctx_free(&ctx2);
		} else {
			if (op == NEG_VERIFY_Q_OFF) {
				mbedtls_mpi_lset(&one, 1);
				mbedtls_mpi_add_mpi(&qbad.private_X,
						    &ctx.private_Q.private_X, &one);
				mbedtls_mpi_copy(&qbad.private_Y,
						 &ctx.private_Q.private_Y);
				mbedtls_mpi_lset(&qbad.private_Z, 1);
			} else {
				(void)mbedtls_ecp_set_zero(&qbad);
			}
			rc = DELEG_VERIFY(&ctx.private_grp, test_hash,
					sizeof(test_hash), &qbad, &r, &s);
		}
	}

out:
	mbedtls_ecp_point_free(&qbad);
	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	mbedtls_mpi_free(&d);
	mbedtls_mpi_free(&one);
	ctx_free(&ctx);
	return rc;
}

/*
 * Run a negative op on a worker thread with a timeout: if the vendor backend
 * wedges on the invalid input the suite still finishes (the item FAILs) instead
 * of hanging. The result code is deliberately NOT asserted to a specific value,
 * only "must be non-zero"; the exact rc is in the INFO line.
 */
struct neg_job {
	struct k_sem done;
	enum neg_op op;
	mbedtls_ecp_group_id gid;
	int rc;
};

static void neg_job_thread(void *p1, void *p2, void *p3)
{
	struct neg_job *j = p1;

	j->rc = run_neg_op(j->op, j->gid);
	k_sem_give(&j->done);
}

/*
 * Each negative item gets its OWN k_thread object and stack (no reuse): reusing
 * a single static thread across items was suspected of the sched.c:486
 * "aborted _current back from dead" assert; this removes the test-side suspect.
 */
#define NEG_JOBS 8
static struct neg_job neg_jobs[NEG_JOBS];
static struct k_thread neg_thr[NEG_JOBS];
static K_THREAD_STACK_ARRAY_DEFINE(neg_stks, NEG_JOBS, 8192);
static unsigned neg_seq;

static void assert_neg_bounded(enum neg_op op, mbedtls_ecp_group_id gid,
			       const char *what, const char *test_name)
{
	unsigned i = neg_seq++;
	struct neg_job *j;
	int got;

	DELEG_GATE();
	zassert_true(i < NEG_JOBS, "too many negative jobs (%u)", i);
	j = &neg_jobs[i];
	k_sem_init(&j->done, 0, 1);
	j->op = op;
	j->gid = gid;
	j->rc = 0x7FFFFFFF;

	k_thread_create(&neg_thr[i], neg_stks[i], 8192, neg_job_thread, j,
			NULL, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);

	got = k_sem_take(&j->done, K_MSEC(5000));

	/* Tag the line with the test name (ZT_INFO contract) so the report and the
	 * independent-expectation checker get rc for this item. */
	ZT_INFO(test_name, "what=%s,gid=%d,finished=%d,rc=%d",
		what, (int)gid, got == 0, j->rc);

	zassert_equal(got, 0,
		"%s gid=%d: proxy call did not return within 5000 ms (hang)",
		what, (int)gid);
	zassert_true(j->rc != 0,
		"%s gid=%d: invalid input ACCEPTED (rc=0)", what, (int)gid);
}

ZTEST(otbn_deleg, t17_sign_bad_d_zero)
{
	assert_neg_bounded(NEG_SIGN_D_ZERO, MBEDTLS_ECP_DP_SECP256R1, "sign_d0", "t17_sign_bad_d_zero");
}

ZTEST(otbn_deleg, t88_sign_bad_d_ge_n)
{
	assert_neg_bounded(NEG_SIGN_D_N, MBEDTLS_ECP_DP_SECP256R1, "sign_d_n", "t88_sign_bad_d_ge_n");
}

ZTEST(otbn_deleg, t19_verify_bad_q_off_curve)
{
	assert_neg_bounded(NEG_VERIFY_Q_OFF, MBEDTLS_ECP_DP_SECP256R1, "verify_q_off", "t19_verify_bad_q_off_curve");
}

ZTEST(otbn_deleg, t89_verify_bad_q_infinity)
{
	assert_neg_bounded(NEG_VERIFY_Q_INF, MBEDTLS_ECP_DP_SECP256R1, "verify_q_inf", "t89_verify_bad_q_infinity");
}

ZTEST(otbn_deleg, t21_verify_wrong_keypair)
{
	assert_neg_bounded(NEG_VERIFY_WRONG_KEY, MBEDTLS_ECP_DP_SECP256R1, "verify_wrongkey", "t21_verify_wrong_keypair");
}

ZTEST(otbn_deleg, t16a_sign_blen_zero)
{
	assert_sign_with_blen(MBEDTLS_ECP_DP_SECP256R1, 0);    /* boundary: blen == 0 */
}

ZTEST(otbn_deleg, t16b_sign_blen_eq_n)
{
	assert_sign_with_blen(MBEDTLS_ECP_DP_SECP384R1, 48);   /* boundary: blen == n_size */
}

/*
 * Repeated channel init: calling the delegation channel init a second time must
 * not break later proxy calls. Runs on whichever role this core builds.
 */
ZTEST(otbn_deleg, t16c_repeat_channel_init)
{
	mbedtls_ecdsa_context ctx;
	int rc;

	DELEG_GATE();

	/* Channel bring-up is idempotent: call it a second time. The entry points
	 * come from ls_otbn_delegation.h, included at the top of this file. */
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	ls_otbn_delegation_client_init();
#elif defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER)
	ls_otbn_delegation_server_init();
#endif

	ctx_init(&ctx);
	rc = DELEG_GENKEY(&ctx, MBEDTLS_ECP_DP_SECP256R1, test_rng, NULL);
	ZT_INFO("repeat_init", "genkey_after_double_init_rc=%d", rc);
	zassert_equal(rc, 0, "proxy broken after repeated channel init (rc=%d)", rc);
	ctx_free(&ctx);
}

/*
 * t16d -- same-image cross-entry agreement (case_explicit_api only; see the
 * redirection block at the top of the file).
 *
 * The client of this case calls the explicit mbedtls_ls_otbn_ecdsa_* names; this
 * item additionally drives the mbedtls_ecdsa_* name and requires BOTH calls to
 * succeed and to yield a valid on-curve key -- i.e. both entry points an
 * application can reach are usable in one image.
 *
 * CAVEAT -- this is NOT a "the forwarder is alive" proof. The client image of
 * THIS case is in the config where the OTBN ECDSA alternative is off
 * (cell 3: ..._OTBN_ALT=n), so mbedtls_ecdsa_genkey() here is the SOFTWARE
 * implementation (libmbedtls ecdsa.c), not the mbedtls_otbn_crypto_mbox.c
 * forwarder. That forwarder is only DEFINED when the client routes its OTBN
 * ECDSA alternative through the delegation (..._ALT_VIA_DELEGATION=y, cell 5);
 * only then would the first call reach the proxy through the forwarder. Cell 5
 * is compile-verified (see the 2026-10-13 migration report) but deliberately not
 * enabled here: it selects CONFIG_LINKEDSEMI_OTBN, which runs otbn_common.c's
 * POST_KERNEL SYS_INIT on cpu2 -- a runtime change that cannot be validated with
 * the board offline, so it is reported rather than assumed safe.
 *
 * Registered under OTBN_DELEG_EXPLICIT_API so the other cases' item sets are
 * unchanged. On the server the image compiles no proxy, so
 * mbedtls_ls_otbn_ecdsa_genkey does not exist: the item is registered for name
 * symmetry but SKIPs (precedent: the client gate in case_dead_channel).
 */
#ifdef OTBN_DELEG_EXPLICIT_API
ZTEST(otbn_deleg, t16d_mbedtls_and_explicit_agree)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	mbedtls_ecdsa_context ctx_fwd, ctx_exp;
	int rc_fwd, rc_exp;

	DELEG_GATE();
	ctx_init(&ctx_fwd);
	ctx_init(&ctx_exp);

	/* No pre-load here: mbedtls_ls_otbn_ecdsa_genkey() loads ctx->private_grp
	 * itself, exactly like mbedtls_ecdsa_genkey(). That contract gap is fixed
	 * in mbedtls_otbn_crypto_mbox.c; this item is the regression guard for it --
	 * a pre-load would hide the regression the item exists to catch.
	 */

	/* Same request first through the mbedtls name (the forwarder)... */
	rc_fwd = mbedtls_ecdsa_genkey(&ctx_fwd, MBEDTLS_ECP_DP_SECP256R1,
				      test_rng, NULL);
	/* ...then through the explicit entry point that forwarder calls. */
	rc_exp = mbedtls_ls_otbn_ecdsa_genkey(&ctx_exp, MBEDTLS_ECP_DP_SECP256R1,
					      test_rng, NULL);

	ZT_INFO("equiv", "mbedtls_rc=%d,explicit_rc=%d", rc_fwd, rc_exp);

	zassert_equal(rc_fwd, 0, "mbedtls_ecdsa_genkey (forwarder) rc=%d", rc_fwd);
	zassert_equal(rc_exp, 0, "mbedtls_ls_otbn_ecdsa_genkey (explicit) rc=%d", rc_exp);

	/* Each must yield a usable, on-curve public key. */
	zassert_equal(mbedtls_ecp_check_pubkey(&ctx_fwd.private_grp, &ctx_fwd.private_Q),
		      0, "forwarder produced an invalid public key");
	zassert_equal(mbedtls_ecp_check_pubkey(&ctx_exp.private_grp, &ctx_exp.private_Q),
		      0, "explicit entry produced an invalid public key");

	ctx_free(&ctx_fwd);
	ctx_free(&ctx_exp);
#else
	/* Server: this image has no explicit entry point to compare against. */
	ztest_test_skip();
#endif
}
#endif /* OTBN_DELEG_EXPLICIT_API */


/*
 * t80 -- "no responder" guard (must be the LAST item; see the file header).
 *
 * The proxy call runs on a SEPARATE thread and is awaited with a timeout: the
 * client stubs block on k_sem_take(..., K_FOREVER) with no timeout and ignore
 * mbox_send_dt() errors, so with no responder the call blocks forever. Issuing
 * it off the test thread keeps the suite alive, so the hang surfaces as an item
 * FAIL (with the elapsed evidence in the INFO line) instead of wedging the run.
 *
 * In case_default the server answers, so this PASSES; in case_dead_channel the
 * client's channel is disabled (and in case_no_server the peer is absent), so
 * it FAILS -- which is exactly the "client blocks forever" defect.
 */
#define PROXY_PROBE_TIMEOUT_MS 3000

struct proxy_probe {
	struct k_sem done;
	int rc;
};

static void proxy_probe_thread(void *p1, void *p2, void *p3)
{
	struct proxy_probe *pp = p1;
	mbedtls_ecdsa_context ctx;

	ctx_init(&ctx);
	pp->rc = DELEG_GENKEY(&ctx, MBEDTLS_ECP_DP_SECP256R1, test_rng, NULL);
	ctx_free(&ctx);
	k_sem_give(&pp->done);
}

static K_THREAD_STACK_DEFINE(probe_stk, 8192);

ZTEST(otbn_deleg, t80_proxy_responds_within_timeout)
{
	static struct proxy_probe pp;
	static struct k_thread pt;
	int got;

	k_sem_init(&pp.done, 0, 1);
	pp.rc = -1;

#if defined(OTBN_DELEG_DEAD_CHANNEL)
	/*
	 * case_dead_channel: disable our own otbn_crypto channel first. Note the
	 * driver's mbox_set_enabled(false) only masks the mbox IRQ for the whole
	 * device -- it does NOT make mbox_send_dt() fail (that still returns 0), so
	 * the send is silently accepted while no reply can ever be delivered.
	 * (We deliberately do NOT send a probe message here: a stray frame would be
	 * delivered to our own consumer callback and corrupt the channel state.)
	 */
	{
		static const struct mbox_dt_spec sp =
			MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_consumer_otbn_crypto), mbox);

		(void)mbox_set_enabled(sp.dev, sp.channel_id, false);
		ZT_INFO("dead_channel", "channel_disabled=1");
	}
#endif

	k_thread_create(&pt, probe_stk, 8192, proxy_probe_thread, &pp, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);

	got = k_sem_take(&pp.done, K_MSEC(PROXY_PROBE_TIMEOUT_MS));

	ZT_INFO("proxy_probe", "responded=%d,rc=%d,timeout_ms=%d",
		got == 0, pp.rc, PROXY_PROBE_TIMEOUT_MS);

	zassert_equal(got, 0,
		"proxy genkey did not return within %d ms: no responder and "
		"mbox_send_dt() error ignored -> the call blocks forever "
		"(client_sem is held, every later proxy call blocks too)",
		PROXY_PROBE_TIMEOUT_MS);
}

/*
 * t90 -- the plain mbedtls_ecdsa_* API is really bound to the mailbox
 * (case_alt_via_proxy only; see the redirection block at the top).
 *
 * case_alt_via_proxy builds the client in the ALT_VIA_DELEGATION config, where
 * mbedtls_ecdsa_genkey()/sign()/verify() ARE the delegation forwarders. The rest
 * of the matrix calling DELEG_* (== the plain names there) already proves the
 * forwarded RESULT is correct, but a correct result alone cannot tell a real
 * round-trip from a client that silently fell back to local software -- software
 * ECDSA returns a valid key pair too.
 *
 * This item closes that gap with an observable only the mailbox path has:
 *   1. The client core builds NO local OTBN in this config (CONFIG_LINKEDSEMI_OTBN
 *      is off; asserted at compile time below), so the crypto cannot be local OTBN.
 *   2. With the client's otbn_crypto channel masked, the plain-name genkey must
 *      NOT succeed: the forwarder blocks on the reply until
 *      DELEGATE_REPLY_TIMEOUT_MS and then returns non-zero, whereas software
 *      ECDSA would return a valid key at once and PASS this item -- so a silent
 *      software fallback is caught here.
 *
 * Runs LAST (name order t90 > every other item) because masking the channel
 * leaves the proxy unusable for the rest of the suite. Registered under
 * OTBN_DELEG_VIA_PROXY so only case_alt_via_proxy carries it; the server
 * registers it for name symmetry and SKIPs.
 */
#if defined(OTBN_DELEG_VIA_PROXY)
/* The client of this case must NOT drag the local OTBN driver in -- that is the
 * whole point of routing the ECDSA alternative through the delegation instead of
 * the local-OTBN entry. A stray CONFIG_LINKEDSEMI_OTBN here would mean the
 * Kconfig choice leaked the local driver into the client, which this case exists
 * to disprove. (The server has the local OTBN on purpose, so the check is
 * client-only.) */
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT) && \
	defined(CONFIG_LINKEDSEMI_OTBN)
#error "case_alt_via_proxy: client pulled in CONFIG_LINKEDSEMI_OTBN (local OTBN) -- the delegation entry must not select it"
#endif

ZTEST(otbn_deleg, t90_plain_api_mailbox_bound)
{
#if defined(CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_VIA_DELEGATION)
	static struct proxy_probe pp;
	static struct k_thread pt;
	static K_THREAD_STACK_DEFINE(mb_probe_stk, 8192);
	int got;

	DELEG_GATE();

	/* Mask our own otbn_crypto channel: no reply can reach the forwarder now. */
	{
		static const struct mbox_dt_spec sp =
			MBOX_DT_SPEC_GET(DT_NODELABEL(mbox_consumer_otbn_crypto), mbox);

		zassert_equal(mbox_set_enabled(sp.dev, sp.channel_id, false), 0,
			      "could not mask the otbn_crypto channel");
	}

	k_sem_init(&pp.done, 0, 1);
	pp.rc = 0x7FFFFFFF;
	k_thread_create(&pt, mb_probe_stk, 8192, proxy_probe_thread, &pp,
			NULL, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	got = k_sem_take(&pp.done, K_MSEC(PROXY_PROBE_TIMEOUT_MS));

	ZT_INFO("mailbox_bound", "returned=%d,rc=%d,timeout_ms=%d",
		got == 0, pp.rc, PROXY_PROBE_TIMEOUT_MS);

	/* The forwarder must give up (non-zero) once the reply cannot arrive. */
	zassert_equal(got, 0,
		"plain-name genkey did not return within %d ms after the channel "
		"was masked (forwarder should time out, not block forever)",
		PROXY_PROBE_TIMEOUT_MS);
	zassert_true(pp.rc != 0,
		"plain mbedtls_ecdsa_genkey() returned rc=%d with the otbn_crypto "
		"channel masked -- it is NOT bound to the mailbox (software?)",
		pp.rc);
#else
	/* Server: no delegation forwarder here; registered for name symmetry. */
	ztest_test_skip();
#endif
}
#endif /* OTBN_DELEG_VIA_PROXY */

/*
 * Error-domain contract (pure functions, runs on both roles):
 *   - every driver code maps to exactly one mbedtls value;
 *   - a raw MBEDTLS_ERR_* passes through untouched, never into the driver range;
 *   - errno maps into the driver domain, and the mapping is idempotent.
 * This pins the tables in ls_otbn_err.h / ls_otbn_err_mbedtls.h so a future
 * edit cannot silently change what an API family returns.
 */
ZTEST(otbn_deleg, t91_err_domain)
{
	zassert_equal(ls_otbn_err_to_mbedtls(0), 0, "0 must stay 0");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_INVAL),
		      MBEDTLS_ERR_ECP_BAD_INPUT_DATA, "INVAL");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_RANGE),
		      MBEDTLS_ERR_ECP_BAD_INPUT_DATA, "RANGE");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_CURVE),
		      MBEDTLS_ERR_ECP_BAD_INPUT_DATA, "CURVE");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_MSGSIZE),
		      MBEDTLS_ERR_ECP_BAD_INPUT_DATA, "MSGSIZE");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_NOSUPP),
		      MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE, "NOSUPP");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_BUSY),
		      MBEDTLS_ERR_LS_OTBN_BUSY, "BUSY");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_ACCESS),
		      MBEDTLS_ERR_LS_OTBN_BUSY, "ACCESS");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_NOSYS),
		      MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED, "NOSYS");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_RNG),
		      MBEDTLS_ERR_ECP_RANDOM_FAILED, "RNG");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_NOENT),
		      MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED, "NOENT");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_PROTO),
		      MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED, "PROTO");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_IO),
		      MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED, "IO");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_TIMEOUT),
		      MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED, "TIMEOUT");
	zassert_equal(ls_otbn_err_to_mbedtls(LS_OTBN_ERR_MBEDTLS),
		      MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED, "CRYPTO fallback");
	zassert_equal(ls_otbn_err_to_mbedtls(MBEDTLS_ERR_ECP_VERIFY_FAILED),
		      MBEDTLS_ERR_ECP_VERIFY_FAILED, "mbedtls passthrough");

	zassert_equal(ls_otbn_err_from_mbedtls(0), 0, "from_mbedtls(0)");
	zassert_equal(ls_otbn_err_from_mbedtls(MBEDTLS_ERR_ECP_INVALID_KEY),
		      LS_OTBN_ERR_MBEDTLS, "from_mbedtls(INVALID_KEY)");
	zassert_equal(ls_otbn_err_from_mbedtls(LS_OTBN_ERR_BUSY),
		      LS_OTBN_ERR_BUSY, "from_mbedtls idempotent");

	zassert_equal(ls_otbn_err_from_errno(-EINVAL), LS_OTBN_ERR_INVAL, "errno EINVAL");
	zassert_equal(ls_otbn_err_from_errno(-ENOENT), LS_OTBN_ERR_NOENT, "errno ENOENT");
	zassert_equal(ls_otbn_err_from_errno(-EBUSY), LS_OTBN_ERR_BUSY, "errno EBUSY");
	zassert_equal(ls_otbn_err_from_errno(-EIO), LS_OTBN_ERR_IO, "errno EIO");
	zassert_equal(ls_otbn_err_from_errno(-ETIMEDOUT), LS_OTBN_ERR_TIMEOUT,
		      "errno ETIMEDOUT");
	zassert_equal(ls_otbn_err_from_errno(-ENOSYS), LS_OTBN_ERR_NOSYS, "errno ENOSYS");
	zassert_equal(ls_otbn_err_from_errno(-EBADMSG), LS_OTBN_ERR_CURVE, "errno EBADMSG");
	zassert_equal(ls_otbn_err_from_errno(-EPROTO), LS_OTBN_ERR_PROTO, "errno EPROTO");
	zassert_equal(ls_otbn_err_from_errno(-EACCES), LS_OTBN_ERR_ACCESS, "errno EACCES");
	zassert_equal(ls_otbn_err_from_errno(-EMSGSIZE), LS_OTBN_ERR_MSGSIZE,
		      "errno EMSGSIZE");
	zassert_equal(ls_otbn_err_from_errno(-ERANGE), LS_OTBN_ERR_RANGE, "errno ERANGE");
	zassert_equal(ls_otbn_err_from_errno(-ENOTSUP), LS_OTBN_ERR_NOSUPP, "errno ENOTSUP");
	zassert_equal(ls_otbn_err_from_errno(LS_OTBN_ERR_BUSY), LS_OTBN_ERR_BUSY,
		      "from_errno idempotent");

	zassert_true(ls_otbn_err_is_custom(LS_OTBN_ERR_BASE), "BASE is custom");
	zassert_true(ls_otbn_err_is_custom(LS_OTBN_ERR_MBEDTLS), "CRYPTO is custom");
	zassert_false(ls_otbn_err_is_custom(-ENOSYS), "-38 is not custom");
	zassert_false(ls_otbn_err_is_custom(0), "0 is not custom");

	ZT_INFO("t91_err_domain", "ok=1");
}

/* 6 explicit params: setup/teardown are suite-level, before/after per-test --
 * all NULL here (no shared fixture). Do NOT move a teardown into the `after`
 * slot (KNOWN_ISSUES #56). */
ZTEST_SUITE(otbn_deleg, NULL, NULL, NULL, NULL, NULL);
