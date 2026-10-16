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

/*
 * Interface redirection -- selected by ROLE and by the client's ECDSA routing
 * (same rule as the shared body otbn_delegation_test.c). The CLIENT benchmarks
 * the PROXY: in the default client config the plain mbedtls_ecdsa_* names resolve
 * to software/ecdsa_alt.c (NOT the proxy), so it calls the explicit
 * mbedtls_ls_otbn_ecdsa_* names; when the client routes its alternative through
 * the delegation (ALT_VIA_DELEGATION) the plain names ARE the proxy and it calls
 * those. The SERVER keeps the mbedtls_* names -- on that image they are the local
 * OTBN path.
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

#define BENCH_N 20
#define BENCH_WARMUP 3

static const uint8_t test_hash[32] = {
	0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
	0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
	0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
	0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD,
};

/*
 * Entropy source for the measured ops.
 *
 * Default: a deterministic in-process LCG. It costs ~nothing, so the measured
 * time is the pure OTBN compute -- but it is NOT what the delegation SERVER uses
 * when it serves a proxied genkey/sign: there the handler passes the hardware
 * entropy device (see ecdsa_server_get_random() in mbedtls_otbn_ecdsa_mbox.c),
 * and ecdsa_alt.c pulls curve_size bytes from it on every genkey/sign. That
 * makes a plain proxy-vs-local diff meaningless for genkey/sign (it would mostly
 * measure the TRNG, not the bus).
 *
 * OTBN_DELEG_BENCH_HWRNG=1 (case_throughput_iso_local_hwrng) makes this bench
 * use the SAME hardware entropy the server handler uses, so a proxy-minus-local
 * diff at that setting isolates the transport. verify() takes no rng on either
 * side, so it is already apples-to-apples.
 */
#if defined(OTBN_DELEG_BENCH_HWRNG) && OTBN_DELEG_BENCH_HWRNG
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/entropy.h>

static const struct device *bench_entropy = DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));

static int test_rng(void *ctx, unsigned char *out, size_t len)
{
	ARG_UNUSED(ctx);
	return entropy_get_entropy(bench_entropy, out, len);
}
#else
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
#endif

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

#if defined(OTBN_DELEG_BENCH_ENTROPY) && OTBN_DELEG_BENCH_ENTROPY

/*
 * Entropy-API bench (case_entropy_iso).
 *
 * Measures the ONE device chosen as zephyr,entropy (trng0) through the two
 * driver fetch paths, on one core while the peer holds:
 *
 *   entropy_block = entropy_get_entropy()                 (mutex + pipe; sleeps
 *                                                         for the ISR to feed)
 *   entropy_busy  = entropy_get_entropy_isr(..., ENTROPY_BUSYWAIT) (irq off,
 *                                                         register poll)
 *
 * This is the contrast the delegation SERVER handler hits: it calls the
 * BLOCKING path (ecdsa_server_get_random -> entropy_get_entropy), while the
 * OTBN throughput sample used the BUSYWAIT one. The point is to decide whether
 * the ~1.9 ms/byte the handler shows is an API-selection cost or the raw TRNG
 * output rate. This TU touches NO driver code.
 *
 * Layout: {1,16,32,48} bytes x BENCH_N iters per cell, BENCH_WARMUP pre-calls
 * per cell. The whole sweep runs TWICE with reversed API order (pass 1:
 * block then busy, pass 2: busy then block); the per-cell warmup absorbs the
 * cold start and the two passes expose any order bias (the busywait path
 * drains the pipe before polling, so whichever API runs second can inherit a
 * few leftover bytes).
 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/entropy.h>

#if defined(OTBN_DELEG_BENCH_HOLD) && OTBN_DELEG_BENCH_HOLD
/* Hold core: same item name, measures nothing (dual-core matrix symmetry). */
ZTEST(otbn_deleg, b02_entropy)
{
	ZT_INFO("b02_entropy", "role=hold,bench=0");
}
#else

#define ENT_CELLS 4
#define ENT_BUF   64

#define ENT_BLOCK 0
#define ENT_BUSY  1
#define ENT_NB    2

static const uint16_t ent_len[ENT_CELLS] = {1u, 16u, 32u, 48u};

/*
 * Device A = the board-default zephyr,entropy (trng0, ls-apb-trng).
 * Device B = trng1 (ls-dwtrng), measured only when OTBN_DELEG_BENCH_TRNG1 is
 * set AND its node has been enabled by an overlay (the board dts leaves trng1
 * status="disabled"). Kept behind the macro so the default entropy case is
 * unchanged. Both devices are timed in the SAME run -- same core, same clock,
 * same background load -- so the two are directly comparable.
 */
static const struct device *const ent_dev_a =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));
#if defined(OTBN_DELEG_BENCH_TRNG1) && OTBN_DELEG_BENCH_TRNG1
static const struct device *const ent_dev_b =
	DEVICE_DT_GET(DT_NODELABEL(trng1));
#endif

static int ent_call(const struct device *dev, int mode, uint8_t *buf,
		    uint16_t len)
{
	if (mode == ENT_BLOCK) {
		return entropy_get_entropy(dev, buf, len);           /* 0 on success */
	}
	if (mode == ENT_BUSY) {
		return entropy_get_entropy_isr(dev, buf, len, ENTROPY_BUSYWAIT);
	}
	return entropy_get_entropy_isr(dev, buf, len, 0);            /* bytes ready */
}

static void entropy_emit(const char *dev_tag, const char *api, int pass,
			 uint16_t bytes, uint32_t *v, int n,
			 int got_min, int got_max)
{
	uint32_t s[BENCH_N];
	uint64_t sum = 0;
	int i, j;
	uint32_t cyc_per_us =
		(uint32_t)(sys_clock_hw_cycles_per_sec() / 1000000U);

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
	ZT_INFO("bench",
		"dev=%s,op=%s,pass=%d,bytes=%u,n=%d,got_min=%d,got_max=%d,"
		"min_us=%u,med_us=%u,max_us=%u,mean_us=%u",
		dev_tag, api, pass, bytes, n, got_min, got_max,
		(unsigned)(s[0] / cyc_per_us),
		(unsigned)(s[n / 2] / cyc_per_us),
		(unsigned)(s[n - 1] / cyc_per_us),
		(unsigned)((uint32_t)(sum / (uint64_t)n) / cyc_per_us));
}

static void ent_run_dev(const struct device *dev, const char *dev_tag,
			int mode, int pass)
{
	static uint8_t buf[ENT_BUF];
	static uint32_t v[BENCH_N];
	const char *api = mode == ENT_BLOCK ? "block"
			: mode == ENT_BUSY ? "busy" : "nb";
	int got_min = -1, got_max = -1;
	int n, i;

	for (n = 0; n < ENT_CELLS; n++) {
		uint16_t len = ent_len[n];

		for (i = 0; i < BENCH_WARMUP; i++) {
			int rc = ent_call(dev, mode, buf, len);

			if (mode == ENT_BLOCK) {
				zassert_true(rc == 0, "warmup %s block len=%u rc=%d",
					     dev_tag, len, rc);
			} else if (mode == ENT_BUSY) {
				zassert_true(rc == (int)len,
					     "warmup %s busy len=%u rc=%d",
					     dev_tag, len, rc);
			}
			/* nb: rc may be -ENODATA (empty pool) -- not an error */
		}
		for (i = 0; i < BENCH_N; i++) {
			uint32_t t0 = k_cycle_get_32();
			int rc = ent_call(dev, mode, buf, len);

			v[i] = k_cycle_get_32() - t0;
			if (mode == ENT_BLOCK) {
				zassert_true(rc == 0, "%s block len=%u rc=%d",
					     dev_tag, len, rc);
			} else if (mode == ENT_BUSY) {
				zassert_true(rc == (int)len, "%s busy len=%u rc=%d",
					     dev_tag, len, rc);
			} else {
				/*
				 * Non-blocking: return value = bytes immediately
				 * available; a driver may report "none" as a
				 * negative code (trng0 uses -ENODATA). Clamp that
				 * to zero so got_min/got_max answer "how many
				 * bytes can I grab right now".
				 */
				int avail = rc < 0 ? 0 : rc;

				if (got_min < 0 || avail < got_min) {
					got_min = avail;
				}
				if (avail > got_max) {
					got_max = avail;
				}
			}
		}
		entropy_emit(dev_tag, api, pass, len, v, BENCH_N, got_min, got_max);
	}
}

ZTEST(otbn_deleg, b02_entropy)
{
	zassert_true(device_is_ready(ent_dev_a), "entropy A (trng0) not ready");

	/*
	 * Order per device: non-blocking FIRST (a blocking/busy pass would leave
	 * a full pool and inflate the "immediately available" count), then
	 * blocking, then busy-wait. Same order for both devices.
	 */
	ent_run_dev(ent_dev_a, "trng0", ENT_NB, 1);
	ent_run_dev(ent_dev_a, "trng0", ENT_BLOCK, 1);
	ent_run_dev(ent_dev_a, "trng0", ENT_BUSY, 1);
#if defined(OTBN_DELEG_BENCH_TRNG1) && OTBN_DELEG_BENCH_TRNG1
	zassert_true(device_is_ready(ent_dev_b), "entropy B (trng1) not ready");
	ent_run_dev(ent_dev_b, "trng1", ENT_NB, 1);
	ent_run_dev(ent_dev_b, "trng1", ENT_BLOCK, 1);
	ent_run_dev(ent_dev_b, "trng1", ENT_BUSY, 1);
#endif
}
#endif /* OTBN_DELEG_BENCH_HOLD (entropy bench) */
#elif defined(OTBN_DELEG_BENCH_HOLD) && OTBN_DELEG_BENCH_HOLD
/*
 * HOLD role (build-time, selected by -DOTBN_DELEG_BENCH_HOLD=1): this core does
 * NOT benchmark. Its only job is to stay off the shared OTBN so the PEER can
 * measure that single unit uncontended:
 *   - built as the SERVER (case_throughput_iso_local): the boot-time delegation
 *     thread still answers the client's mbox requests, but this core issues no
 *     bench ops of its own, so the OTBN serves exactly one requester.
 *   - built as the CLIENT (case_throughput_iso_proxy): the channel is up but
 *     nothing is sent.
 * The item name is deliberately the SAME as the active core's (b01_throughput):
 * the dual-core matrix requires every declared item to exist on BOTH cores, and
 * a same-name PASS on the hold core keeps the case green without measuring
 * anything there. The INFO line records which side held.
 */
ZTEST(otbn_deleg, b01_throughput)
{
	ZT_INFO("b01_throughput", "role=hold,bench=0");
}
#else
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
		/* No caller-side curve pre-load: DELEG_GENKEY below loads
		 * ctx->grp from gid itself, so the sign/verify passes that follow
		 * use a loaded group. */

		for (i = 0; i < BENCH_WARMUP; i++) {
			(void)DELEG_GENKEY(&ctx, gids[c], test_rng, NULL);
		}
		for (i = 0; i < BENCH_N; i++) {
			t0 = k_cycle_get_32();
			rc = DELEG_GENKEY(&ctx, gids[c], test_rng, NULL);
			v[i] = k_cycle_get_32() - t0;
			zassert_equal(rc, 0, "genkey %s rc=%d", gnames[c], rc);
		}
		bench_emit("genkey", (int)gids[c], v, BENCH_N);

		for (i = 0; i < BENCH_WARMUP; i++) {
			(void)DELEG_SIGN(&ctx.private_grp, &r, &s, &ctx.private_d,
					 test_hash, sizeof(test_hash), test_rng, NULL);
		}
		for (i = 0; i < BENCH_N; i++) {
			t0 = k_cycle_get_32();
			rc = DELEG_SIGN(&ctx.private_grp, &r, &s, &ctx.private_d,
					test_hash, sizeof(test_hash), test_rng, NULL);
			v[i] = k_cycle_get_32() - t0;
			zassert_equal(rc, 0, "sign %s rc=%d", gnames[c], rc);
		}
		bench_emit("sign", (int)gids[c], v, BENCH_N);

		for (i = 0; i < BENCH_WARMUP; i++) {
			(void)DELEG_VERIFY(&ctx.private_grp, test_hash,
					   sizeof(test_hash), &ctx.private_Q, &r, &s);
		}
		for (i = 0; i < BENCH_N; i++) {
			t0 = k_cycle_get_32();
			rc = DELEG_VERIFY(&ctx.private_grp, test_hash,
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
#endif /* OTBN_DELEG_BENCH_ENTROPY / _HOLD / throughput */

ZTEST_SUITE(otbn_deleg, NULL, NULL, NULL, NULL, NULL);
