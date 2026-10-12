/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN ECDSA delegation throughput benchmark (case_throughput only).
 *
 * Compiled INSTEAD OF otbn_delegation_test.c when OTBN_DELEG_BENCH is set (see
 * the cpu<N>/CMakeLists.txt), so the benchmark case runs exactly one item and no
 * functional/negative items run alongside it.
 *
 * Runs the SAME code on both cores: the client measures the PROXY path (each op
 * is an mbox round-trip to cpu1), the server measures the LOCAL OTBN path. So a
 * single run yields both. The client-minus-server delta is the proxy round-trip
 * overhead.
 *
 * Timing = k_cycle_get_32() deltas, reported in us via the hardware cycle rate.
 * BENCH_N=20 measured iterations after BENCH_WARMUP=3; no LOG() inside the
 * loops (would perturb the numbers).
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/sys_clock.h>

#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"

/* ZT_INFO comes from the 810 common ztest glue; this TU must include it. */
#include "ztest_glue.h"

#define BENCH_N 20
#define BENCH_WARMUP 3

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

static void bench_emit(const char *op, int gid, uint32_t *v, int n)
{
	uint32_t s[BENCH_N];
	uint64_t sum = 0;
	int i, j;
	uint32_t cyc_per_us = (uint32_t)(sys_clock_hw_cycles_per_sec() / 1000000U);

	if (cyc_per_us == 0U) {
		cyc_per_us = 1U;
	}
	memcpy(s, v, (size_t)n * sizeof(uint32_t));
	for (i = 1; i < n; i++) {           /* insertion sort, n is tiny */
		uint32_t k = s[i];

		for (j = i - 1; j >= 0 && s[j] > k; j--) {
			s[j + 1] = s[j];
		}
		s[j + 1] = k;
	}
	for (i = 0; i < n; i++) {
		sum += v[i];
	}
	ZT_INFO("bench", "op=%s,gid=%d,n=%d,min_us=%u,med_us=%u,max_us=%u,mean_us=%u",
		op, gid, n, (unsigned)(s[0] / cyc_per_us),
		(unsigned)(s[n / 2] / cyc_per_us), (unsigned)(s[n - 1] / cyc_per_us),
		(unsigned)((uint32_t)(sum / (uint64_t)n) / cyc_per_us));
}

ZTEST(otbn_deleg, b01_throughput)
{
	static uint32_t v[BENCH_N];
	static const mbedtls_ecp_group_id gids[3] = {
		MBEDTLS_ECP_DP_SECP256R1, MBEDTLS_ECP_DP_SECP384R1, MBEDTLS_ECP_DP_SM2
	};
	static const char *gnames[3] = { "secp256r1", "secp384r1", "sm2" };

	for (int c = 0; c < 3; c++) {
		mbedtls_ecdsa_context ctx;
		mbedtls_mpi r, s;
		uint32_t t0;
		int rc, i;

		ctx_init(&ctx);
		mbedtls_mpi_init(&r);
		mbedtls_mpi_init(&s);
		zassert_equal(mbedtls_ecp_group_load(&ctx.private_grp, gids[c]), 0,
			      "group_load %s", gnames[c]);

		for (i = 0; i < BENCH_WARMUP; i++) {
			(void)mbedtls_ecdsa_genkey(&ctx, gids[c], test_rng, NULL);
		}
		for (i = 0; i < BENCH_N; i++) {
			t0 = k_cycle_get_32();
			rc = mbedtls_ecdsa_genkey(&ctx, gids[c], test_rng, NULL);
			v[i] = k_cycle_get_32() - t0;
			zassert_equal(rc, 0, "genkey %s rc=%d", gnames[c], rc);
		}
		bench_emit("genkey", (int)gids[c], v, BENCH_N);

		for (i = 0; i < BENCH_WARMUP; i++) {
			(void)mbedtls_ecdsa_sign(&ctx.private_grp, &r, &s, &ctx.private_d,
						 test_hash, sizeof(test_hash), test_rng, NULL);
		}
		for (i = 0; i < BENCH_N; i++) {
			t0 = k_cycle_get_32();
			rc = mbedtls_ecdsa_sign(&ctx.private_grp, &r, &s, &ctx.private_d,
						test_hash, sizeof(test_hash), test_rng, NULL);
			v[i] = k_cycle_get_32() - t0;
			zassert_equal(rc, 0, "sign %s rc=%d", gnames[c], rc);
		}
		bench_emit("sign", (int)gids[c], v, BENCH_N);

		for (i = 0; i < BENCH_WARMUP; i++) {
			(void)mbedtls_ecdsa_verify(&ctx.private_grp, test_hash,
						   sizeof(test_hash), &ctx.private_Q, &r, &s);
		}
		for (i = 0; i < BENCH_N; i++) {
			t0 = k_cycle_get_32();
			rc = mbedtls_ecdsa_verify(&ctx.private_grp, test_hash,
						  sizeof(test_hash), &ctx.private_Q, &r, &s);
			v[i] = k_cycle_get_32() - t0;
			zassert_equal(rc, 0, "verify %s rc=%d", gnames[c], rc);
		}
		bench_emit("verify", (int)gids[c], v, BENCH_N);

		mbedtls_mpi_free(&r);
		mbedtls_mpi_free(&s);
		ctx_free(&ctx);
	}
}

ZTEST_SUITE(otbn_deleg, NULL, NULL, NULL, NULL, NULL);
