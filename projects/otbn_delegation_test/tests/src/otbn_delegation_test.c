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
 *           => mbedtls_ecdsa_{genkey,sign,verify} resolve to the delegation
 *              stubs in zephyr/soc/linkedsemi/lsqsh/otbn/mbedtls/
 *                 mbedtls_otbn_crypto_mbox.c -- the request is sent over the
 *              otbn_crypto mbox channel and answered by cpu1.
 *   - cpu1: CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER=y
 *           => mbedtls_ecdsa_* are the real OTBN-accelerated implementations;
 *              the core also services cpu2's mbox requests from a work queue.
 *
 * Judgement (810 dual-core, same-name items on both cores) = item PASS on
 * CTRL(cpu2) AND TGT(cpu1). A delegated operation succeeds on cpu2 only if
 * cpu1 answered it, so a double PASS means "channel up + proxy round-trip
 * correct"; cpu1's own PASS means "local OTBN ECDSA correct".
 *
 * Test order is intentionally the numeric prefix order (link order = name
 * order), with the negative case LAST. Reason: the client-side genkey stub
 * leaks client_sem on an out-of-whitelist curve (mbedtls_ecdsa_genkey returns
 * -EIO at mbedtls_otbn_crypto_mbox.c:112 after taking the sem at :101 without
 * the matching k_sem_give at the shared exit). Running it last keeps the
 * channel usable for the positive cases and still exercises the negative path.
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

/* Load the curve then generate a fresh key pair on the backend in use. */
static void genkey_or_fail(mbedtls_ecdsa_context *ctx, mbedtls_ecp_group_id gid)
{
	int rc = mbedtls_ecp_group_load(&ctx->private_grp, gid);

	zassert_equal(rc, 0, "ecp_group_load gid=%d rc=%d", (int)gid, rc);

	rc = mbedtls_ecdsa_genkey(ctx, gid, test_rng, NULL);
	zassert_equal(rc, 0, "ecdsa_genkey gid=%d rc=%d", (int)gid, rc);
}

/* Sign test_hash; returns r/s in the caller's mpis (caller owns/init them). */
static void sign_or_fail(mbedtls_ecdsa_context *ctx, mbedtls_mpi *r, mbedtls_mpi *s)
{
	int rc = mbedtls_ecdsa_sign(&ctx->private_grp, r, s, &ctx->private_d,
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

	zassert_equal(mbedtls_ecdsa_verify(&ctx.private_grp, test_hash,
					    sizeof(test_hash), &ctx.private_Q, &r, &s),
		      0, "gid=%d valid signature rejected", (int)gid);

	/* Tamper r -> r_bad = (r + 1) mod n, guaranteed different from r. */
	mbedtls_mpi_lset(&one, 1);
	mbedtls_mpi_copy(&n, &ctx.private_grp.N);
	mbedtls_mpi_add_mpi(&r_bad, &r, &one);
	mbedtls_mpi_mod_mpi(&r_bad, &r_bad, &n);

	zassert_true(mbedtls_mpi_cmp_mpi(&r_bad, &r) != 0, "tamper made no change");

	zassert_true(mbedtls_ecdsa_verify(&ctx.private_grp, test_hash,
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

	zassert_equal(mbedtls_ecdsa_sign(&ctx.private_grp, &r, &s, &ctx.private_d,
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

	zassert_true(mbedtls_ecdsa_verify(&ctx.private_grp, test_hash,
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
	j->rc = mbedtls_ecdsa_genkey(&ctx, j->gid, test_rng, NULL);
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
 */
ZTEST(otbn_deleg, t10_reject_unsupported_curve)
{
	mbedtls_ecdsa_context ctx;
	int rc;

	ctx_init(&ctx);
	rc = mbedtls_ecdsa_genkey(&ctx, MBEDTLS_ECP_DP_NONE, test_rng, NULL);
	zassert_true(rc != 0, "unsupported curve accepted rc=%d", rc);

	ZT_INFO("reject_curve", "rc=%d", rc);
	ctx_free(&ctx);
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
		rc = mbedtls_ecdsa_sign(&ctx.private_grp, &r, &s, &d, test_hash,
					sizeof(test_hash), test_rng, NULL);
	} else {
		/* Valid key + signature first, then corrupt the verification input. */
		if (mbedtls_ecdsa_genkey(&ctx, gid, test_rng, NULL) != 0) {
			goto out;
		}
		if (mbedtls_ecdsa_sign(&ctx.private_grp, &r, &s, &ctx.private_d,
				       test_hash, sizeof(test_hash), test_rng, NULL) != 0) {
			goto out;
		}
		if (op == NEG_VERIFY_WRONG_KEY) {
			mbedtls_ecdsa_context ctx2;

			ctx_init(&ctx2);
			if (mbedtls_ecp_group_load(&ctx2.private_grp, gid) != 0 ||
			    mbedtls_ecdsa_genkey(&ctx2, gid, test_rng, NULL) != 0) {
				ctx_free(&ctx2);
				goto out;
			}
			rc = mbedtls_ecdsa_verify(&ctx.private_grp, test_hash,
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
			rc = mbedtls_ecdsa_verify(&ctx.private_grp, test_hash,
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
			       const char *what)
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

	ZT_INFO("neg", "what=%s,gid=%d,finished=%d,rc=%d",
		what, (int)gid, got == 0, j->rc);

	zassert_equal(got, 0,
		"%s gid=%d: proxy call did not return within 5000 ms (hang)",
		what, (int)gid);
	zassert_true(j->rc != 0,
		"%s gid=%d: invalid input ACCEPTED (rc=0)", what, (int)gid);
}

ZTEST(otbn_deleg, t17_sign_bad_d_zero)
{
	assert_neg_bounded(NEG_SIGN_D_ZERO, MBEDTLS_ECP_DP_SECP256R1, "sign_d0");
}

ZTEST(otbn_deleg, t88_sign_bad_d_ge_n)
{
	assert_neg_bounded(NEG_SIGN_D_N, MBEDTLS_ECP_DP_SECP256R1, "sign_d_n");
}

ZTEST(otbn_deleg, t19_verify_bad_q_off_curve)
{
	assert_neg_bounded(NEG_VERIFY_Q_OFF, MBEDTLS_ECP_DP_SECP256R1, "verify_q_off");
}

ZTEST(otbn_deleg, t89_verify_bad_q_infinity)
{
	assert_neg_bounded(NEG_VERIFY_Q_INF, MBEDTLS_ECP_DP_SECP256R1, "verify_q_inf");
}

ZTEST(otbn_deleg, t21_verify_wrong_keypair)
{
	assert_neg_bounded(NEG_VERIFY_WRONG_KEY, MBEDTLS_ECP_DP_SECP256R1, "verify_wrongkey");
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

#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	void mbedtls_ls_otbn_delegation_client_chanels_init(void);

	mbedtls_ls_otbn_delegation_client_chanels_init();   /* 2nd time */
#elif defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER)
	void mbedtls_ls_otbn_delegation_server_chanels_init(void);

	mbedtls_ls_otbn_delegation_server_chanels_init();   /* 2nd time */
#endif

	ctx_init(&ctx);
	rc = mbedtls_ecdsa_genkey(&ctx, MBEDTLS_ECP_DP_SECP256R1, test_rng, NULL);
	ZT_INFO("repeat_init", "genkey_after_double_init_rc=%d", rc);
	zassert_equal(rc, 0, "proxy broken after repeated channel init (rc=%d)", rc);
	ctx_free(&ctx);
}


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
	pp->rc = mbedtls_ecdsa_genkey(&ctx, MBEDTLS_ECP_DP_SECP256R1, test_rng, NULL);
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

/* 6 explicit params: setup/teardown are suite-level, before/after per-test --
 * all NULL here (no shared fixture). Do NOT move a teardown into the `after`
 * slot (KNOWN_ISSUES #56). */
ZTEST_SUITE(otbn_deleg, NULL, NULL, NULL, NULL, NULL);
