/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN ECDSA delegation SEQUENCE-WRAP soak (case_seq_soak only).
 *
 * Compiled INSTEAD OF otbn_delegation_test.c when OTBN_DELEG_SOAK is set (see
 * the cpu<N>/CMakeLists.txt), so this case runs exactly one item.
 *
 * Purpose: exercise the client's request/response sequence number PAST its
 * 8-bit wrap (the product skips 0 on wrap so a request seq never collides with
 * the "no reply yet" sentinel). SOAK_ITERS*4 proxy calls per core; only the
 * CLIENT core's calls go through the seq'd proxy path, so the client count is
 * what matters.
 *
 * Every iteration makes 4 proxy calls WITH correctness assertions:
 *   1. genkey                         -> rc == 0
 *   2. sign (same ctx)                -> rc == 0 and r,s in (1, n-1)
 *   3. verify(valid signature)        -> MUST == 0
 *   4. verify(tampered r=(r+1) mod n) -> MUST != 0
 * If a stale reply is ever accepted as this call's reply (silent cross-talk),
 * assertions 2-4 must fire.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"

#include "ztest_glue.h"

/*
 * Interface redirection -- selected by ROLE (same rule as the shared body
 * otbn_delegation_test.c). The CLIENT soaks the PROXY, so it must call the
 * explicit mbedtls_ls_otbn_ecdsa_* names: the plain mbedtls_ecdsa_* names only
 * reach the proxy in the ALT_VIA_DELEGATION config and otherwise resolve to
 * software/ecdsa_alt.c, which the sequence-wrap soak would not exercise at all.
 * The SERVER keeps the mbedtls_* names (its local OTBN path).
 */
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

#ifndef SOAK_ITERS
#define SOAK_ITERS 160          /* 160 * 4 = 640 proxy calls (>= 256 wrap point) */
#endif
#define SOAK_MIN_CALLS 600

static const uint8_t test_hash[32] = {
	0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
	0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
	0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
	0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD,
};

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

ZTEST(otbn_deleg, s01_seq_soak)
{
	int calls = 0;

	for (int i = 0; i < SOAK_ITERS; i++) {
		mbedtls_ecdsa_context ctx;
		mbedtls_mpi r, s, rbad, one, n;

		ctx_init(&ctx);
		mbedtls_mpi_init(&r);
		mbedtls_mpi_init(&s);
		mbedtls_mpi_init(&rbad);
		mbedtls_mpi_init(&one);
		mbedtls_mpi_init(&n);

		/* No caller-side curve pre-load: DELEG_GENKEY loads ctx->grp. */
		zassert_equal(DELEG_GENKEY(&ctx, MBEDTLS_ECP_DP_SECP256R1,
				test_rng, NULL), 0, "iter %d: genkey rc!=0", i);
		calls++;

		zassert_equal(DELEG_SIGN(&ctx.private_grp, &r, &s,
				&ctx.private_d, test_hash, sizeof(test_hash),
				test_rng, NULL), 0, "iter %d: sign rc!=0", i);
		calls++;
		zassert_true(mbedtls_mpi_cmp_int(&r, 0) > 0 &&
			     mbedtls_mpi_cmp_mpi(&r, &ctx.private_grp.N) < 0 &&
			     mbedtls_mpi_cmp_int(&s, 0) > 0 &&
			     mbedtls_mpi_cmp_mpi(&s, &ctx.private_grp.N) < 0,
			     "iter %d: r/s out of range (silent cross-talk?)", i);

		/* valid signature MUST verify */
		zassert_equal(DELEG_VERIFY(&ctx.private_grp, test_hash,
				sizeof(test_hash), &ctx.private_Q, &r, &s), 0,
			"iter %d: valid signature REJECTED (wrong reply accepted?)", i);
		calls++;

		/* tampered r MUST be rejected */
		mbedtls_mpi_lset(&one, 1);
		mbedtls_mpi_copy(&n, &ctx.private_grp.N);
		mbedtls_mpi_add_mpi(&rbad, &r, &one);
		mbedtls_mpi_mod_mpi(&rbad, &rbad, &n);
		zassert_true(mbedtls_mpi_cmp_mpi(&rbad, &r) != 0, "iter %d: tamper noop", i);
		zassert_true(DELEG_VERIFY(&ctx.private_grp, test_hash,
				sizeof(test_hash), &ctx.private_Q, &rbad, &s) != 0,
			"iter %d: tampered signature ACCEPTED (stale reply?)", i);
		calls++;

		mbedtls_mpi_free(&r);
		mbedtls_mpi_free(&s);
		mbedtls_mpi_free(&rbad);
		mbedtls_mpi_free(&one);
		mbedtls_mpi_free(&n);
		ctx_free(&ctx);
	}

	ZT_INFO("seq_soak", "iterations=%d,proxy_calls=%d", SOAK_ITERS, calls);
	zassert_true(calls >= SOAK_MIN_CALLS,
		     "only %d proxy calls (< %d): sequence wrap not crossed",
		     calls, SOAK_MIN_CALLS);
}

ZTEST_SUITE(otbn_deleg, NULL, NULL, NULL, NULL, NULL);
