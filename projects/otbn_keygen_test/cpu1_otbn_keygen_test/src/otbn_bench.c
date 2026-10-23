/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * On-board measurements for the TPM routing decision.
 *
 *   Goal 1: single-call latency of mbedtls_ls_otbn_rsa_modexp() at 128 B / 192 B
 *           (N = 20 timed calls per width, median / p90 / min / max).
 *           Timed with the 64-bit hardware cycle counter, NOT ztest's own
 *           duration report (which wraps above ~7.158 s).
 *
 *   Goal 2: how many RND / URND words the RSA keygen firmware pulls through
 *           the OTBN EDN handshake during one otbn_rsa_keygen(). The EDN ISR
 *           fills 8 words per request, so words == callback invocations.
 *
 *   Goal 3: with a fixed EDN word stream and a fixed candidate RNG stream, run
 *           keygen twice and compare p/q/n/d byte for byte. Identical output =
 *           the firmware is a pure consumer of the supplied streams (no
 *           internal DRBG feeding the key material).
 *
 * Notes
 *   - The random callbacks are global state; every measurement restores them
 *     with ls_otbn_random_callback_register(NULL, NULL) BEFORE asserting, so a
 *     failed assertion cannot leak the fake source into later OTBN work.
 *   - All buffers are file scope: the ztest thread stack must not carry them.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "ztest_glue.h"

#include "otbn_rsa_keygen.h"
#include "ls_otbn_config.h"
#include "mbedtls_otbn_rsa_mbox.h"

#include "modexp_vectors.h"
#include "bench_small_primes.h"

/* ------------------------------------------------------------------ timing */

#define BENCH_SAMPLES   20
#define CYCLES_PER_US   ((uint64_t)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000ULL)

static uint64_t cycles_to_us(uint64_t cycles)
{
	return (cycles + CYCLES_PER_US / 2) / CYCLES_PER_US;
}

static void sort_u64(uint64_t *v, size_t n)
{
	for (size_t i = 1; i < n; i++) {
		uint64_t x = v[i];
		size_t j = i;

		while (j > 0 && v[j - 1] > x) {
			v[j] = v[j - 1];
			j--;
		}
		v[j] = x;
	}
}

/* ---------------------------------------------------------- shared buffers */

#define BENCH_MAX_BYTES 512
#define BENCH_MAX_PRIME OTBN_RSA_KEYGEN_PRIME_BYTES_MAX

static uint8_t b_in[BENCH_MAX_BYTES] __aligned(4);
static uint8_t b_exp[BENCH_MAX_BYTES] __aligned(4);
static uint8_t b_n[BENCH_MAX_BYTES] __aligned(4);
static uint8_t b_out[BENCH_MAX_BYTES] __aligned(4);

static uint8_t b_p[BENCH_MAX_PRIME] __aligned(4);
static uint8_t b_q[BENCH_MAX_PRIME] __aligned(4);
static uint8_t b_mod[2 * BENCH_MAX_PRIME] __aligned(4);
static uint8_t b_d[2 * BENCH_MAX_PRIME] __aligned(4);

static uint8_t b_p2[BENCH_MAX_PRIME] __aligned(4);
static uint8_t b_q2[BENCH_MAX_PRIME] __aligned(4);
static uint8_t b_mod2[2 * BENCH_MAX_PRIME] __aligned(4);
static uint8_t b_d2[2 * BENCH_MAX_PRIME] __aligned(4);

/* ================================ Goal 1 ================================= */

static void bench_modexp_width(const char *tag, const modexp_vec_t *vec)
{
	uint64_t samples[BENCH_SAMPLES];
	char list[256];
	size_t len = vec->num_bytes;
	size_t pos = 0;
	int err = 0;
	int ret;

	zassert_true(len <= BENCH_MAX_BYTES, "vector too wide: %u", (unsigned)len);

	memcpy(b_in, vec->base, len);
	memcpy(b_exp, vec->exp, len);
	memcpy(b_n, vec->n, len);
	memset(b_out, 0, sizeof(b_out));

	/* Warm-up: the first call may load the MODEXP image into IMEM. The
	 * measurement below must see the steady-state cost only. */
	ret = mbedtls_ls_otbn_rsa_modexp(b_in, b_out, b_exp, b_n, len, true);
	zassert_equal(ret, 0, "warm-up modexp(%u B) ret=%d", (unsigned)len, ret);

	for (size_t i = 0; i < BENCH_SAMPLES; i++) {
		uint64_t t0, t1;

		memcpy(b_in, vec->base, len);
		memcpy(b_exp, vec->exp, len);
		memcpy(b_n, vec->n, len);
		memset(b_out, 0xA5, sizeof(b_out));   /* reset output each call */

		t0 = k_cycle_get_64();
		ret = mbedtls_ls_otbn_rsa_modexp(b_in, b_out, b_exp, b_n, len, true);
		t1 = k_cycle_get_64();

		if (ret != 0) {
			err++;
		}
		samples[i] = cycles_to_us(t1 - t0);
	}

	/* Keep the per-sample list so the numbers can be re-derived from the
	 * serial archive alone. */
	for (size_t i = 0; i < BENCH_SAMPLES; i++) {
		int n = snprintf(list + pos, sizeof(list) - pos, "%s%llu",
				 i ? "," : "", (unsigned long long)samples[i]);

		if (n < 0 || (size_t)n >= sizeof(list) - pos) {
			break;
		}
		pos += (size_t)n;
	}

	sort_u64(samples, BENCH_SAMPLES);
	uint64_t sum = 0;

	for (size_t i = 0; i < BENCH_SAMPLES; i++) {
		sum += samples[i];
	}

	/* Nearest-rank p90: ceil(0.9*N)-th smallest (1-based) => index 17 of 20. */
	size_t p90_idx = (BENCH_SAMPLES * 9 + 9) / 10 - 1;
	uint64_t median = (samples[BENCH_SAMPLES / 2 - 1] +
			   samples[BENCH_SAMPLES / 2] + 1) / 2;
	int byte_equal = 0;

	if (err == 0) {
		byte_equal = (memcmp(b_out, vec->out, len) == 0);
	}

	ZT_INFO(tag,
		"num_bytes=%u,samples=%d,median_us=%llu,p90_us=%llu,"
		"min_us=%llu,max_us=%llu,mean_us=%llu,byte_equal=%d,err=%d,us_list=%s",
		(unsigned)len, BENCH_SAMPLES,
		(unsigned long long)median, (unsigned long long)samples[p90_idx],
		(unsigned long long)samples[0],
		(unsigned long long)samples[BENCH_SAMPLES - 1],
		(unsigned long long)(sum / BENCH_SAMPLES),
		byte_equal, err, list);
}

ZTEST(keygen, zzz_bench_modexp_128b)
{
	bench_modexp_width("zzz_bench_modexp_128b", &modexp_vectors[0]);
}

ZTEST(keygen, zzz_bench_modexp_192b)
{
	bench_modexp_width("zzz_bench_modexp_192b", &modexp_vectors[1]);
}

/* ============================ deterministic RNG ========================== */

typedef struct {
	uint64_t state;
} bench_rng_t;

static uint64_t splitmix64(uint64_t *state)
{
	uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);

	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

static int bench_rng(void *ctx, uint8_t *buf, size_t len)
{
	bench_rng_t *r = (bench_rng_t *)ctx;
	uint64_t cur = 0;

	for (size_t i = 0; i < len; i++) {
		if ((i & 7) == 0) {
			cur = splitmix64(&r->state);
		}
		buf[i] = (uint8_t)(cur >> ((i & 7) * 8));
	}
	return 0;
}

/* Mirror otbn_rsa_has_small_factor() from the module under test. Kept local
 * because that helper is static; the table is the same generated copy. */
static bool has_small_factor(const uint8_t *candidate_le, size_t prime_bytes)
{
	for (size_t idx = 0; idx < BENCH_NUM_SMALL_PRIMES; idx++) {
		uint32_t divisor = bench_small_primes[idx];
		uint32_t rem = 0;

		for (int i = (int)(prime_bytes - 1); i >= 0; i--) {
			rem = ((rem << 8) | candidate_le[i]) % divisor;
		}
		if (rem == 0) {
			return true;
		}
	}
	return false;
}

static void req_init(otbn_rsa_keygen_req_t *req, otbn_rsa_rng_cb rng, void *ctx,
		     uint8_t *p, uint8_t *q, uint8_t *n, uint8_t *d)
{
	req->size = OTBN_RSA_KEYGEN_2048;
	req->rng = rng;
	req->rng_ctx = ctx;
	req->p_le = p;
	req->q_le = q;
	req->n_le = n;
	req->d_le = d;
}

/* ================================ Goal 2 ================================= */

static volatile uint32_t g_rnd_calls;
static volatile uint32_t g_urnd_calls;
static uint64_t g_edn_state;

static uint32_t counting_rnd_cb(void)
{
	g_rnd_calls++;
	return (uint32_t)splitmix64(&g_edn_state);
}

static uint32_t counting_urnd_cb(void)
{
	g_urnd_calls++;
	return (uint32_t)splitmix64(&g_edn_state);
}

static uint32_t g_cand_calls;
static uint32_t g_sieve_pass;

static int counting_candidate_rng(void *ctx, uint8_t *buf, size_t len)
{
	uint8_t tmp[BENCH_MAX_PRIME];
	int ret = bench_rng(ctx, buf, len);

	if (ret != 0 || len > sizeof(tmp)) {
		return ret;
	}

	g_cand_calls++;

	/* Apply the same two fixups otbn_rsa_gen_candidate() applies after the
	 * callback returns, so the local verdict matches what CHECK_PRIME sees. */
	memcpy(tmp, buf, len);
	tmp[0] |= 0x03U;
	tmp[len - 1] |= 0xC0U;
	if (!has_small_factor(tmp, len)) {
		g_sieve_pass++;
	}
	return 0;
}

ZTEST(keygen, zzz_bench_random_counts)
{
	bench_rng_t rng = { .state = 0x0123456789ABCDEFULL };
	otbn_rsa_keygen_req_t req;
	int64_t t0, t1;
	int ret;

	g_rnd_calls = 0;
	g_urnd_calls = 0;
	g_edn_state = 0xDEADBEEFCAFEF00DULL;
	g_cand_calls = 0;
	g_sieve_pass = 0;

	req_init(&req, counting_candidate_rng, &rng, b_p, b_q, b_mod, b_d);

	ls_otbn_random_callback_register(counting_rnd_cb, counting_urnd_cb);
	t0 = k_uptime_get();
	ret = otbn_rsa_keygen(&req);
	t1 = k_uptime_get();
	/* Restore the built-in source before anything else (global state). */
	ls_otbn_random_callback_register(NULL, NULL);

	/* Runtime proof that the restore took effect: CHECK_PRIME is an EDN
	 * consumer (that is what the counters above measure). Run one after the
	 * restore; the counting callbacks must NOT fire again. */
	uint32_t rnd_after = g_rnd_calls;
	uint32_t urnd_after = g_urnd_calls;
	bool post_prime = false;
	int post_ret = otbn_rsa_check_prime(OTBN_RSA_KEYGEN_2048, b_p, &post_prime);

	ZT_INFO("zzz_bench_random_counts",
		"keygen_ret=%d,keygen_ms=%lld,rnd_cb_calls=%u,urnd_cb_calls=%u,"
		"rnd_words=%u,urnd_words=%u,rnd_isr_requests=%u,urnd_isr_requests=%u,"
		"candidates=%u,sieve_pass=%u,post_restore_check_prime_ret=%d,"
		"post_restore_prime=%d,post_rnd_delta=%u,post_urnd_delta=%u",
		ret, (long long)(t1 - t0),
		(unsigned)g_rnd_calls, (unsigned)g_urnd_calls,
		(unsigned)g_rnd_calls, (unsigned)g_urnd_calls,
		(unsigned)(g_rnd_calls / 8), (unsigned)(g_urnd_calls / 8),
		(unsigned)g_cand_calls, (unsigned)g_sieve_pass,
		post_ret, post_prime ? 1 : 0,
		(unsigned)(g_rnd_calls - rnd_after),
		(unsigned)(g_urnd_calls - urnd_after));

	zassert_equal(ret, 0, "keygen ret=%d", ret);
	zassert_true(g_cand_calls >= 2, "candidate RNG called %u times", g_cand_calls);
	zassert_equal(post_ret, 0, "post-restore check_prime ret=%d", post_ret);
	zassert_true(post_prime, "generated prime rejected after restore");
	zassert_equal(g_rnd_calls, rnd_after,
		      "RND counting callback still registered after restore");
	zassert_equal(g_urnd_calls, urnd_after,
		      "URND counting callback still registered after restore");
}

/* ================================ Goal 3 ================================= */

static uint64_t g_fixed_edn;
static uint32_t g_fixed_rnd_calls;
static uint32_t g_fixed_urnd_calls;

static uint32_t fixed_rnd_cb(void)
{
	g_fixed_rnd_calls++;
	return (uint32_t)splitmix64(&g_fixed_edn);
}

static uint32_t fixed_urnd_cb(void)
{
	g_fixed_urnd_calls++;
	return (uint32_t)splitmix64(&g_fixed_edn);
}

/* Run one keygen with reset stream states and copy out p/q/n/d. */
static int fixed_stream_keygen(uint64_t cand_seed, uint64_t edn_seed,
			       uint8_t *p, uint8_t *q, uint8_t *n, uint8_t *d,
			       uint32_t *rnd_calls, uint32_t *urnd_calls)
{
	bench_rng_t cand = { .state = cand_seed };
	otbn_rsa_keygen_req_t req;
	int ret;

	g_fixed_edn = edn_seed;
	g_fixed_rnd_calls = 0;
	g_fixed_urnd_calls = 0;

	req_init(&req, bench_rng, &cand, p, q, n, d);

	ls_otbn_random_callback_register(fixed_rnd_cb, fixed_urnd_cb);
	ret = otbn_rsa_keygen(&req);
	ls_otbn_random_callback_register(NULL, NULL);

	*rnd_calls = g_fixed_rnd_calls;
	*urnd_calls = g_fixed_urnd_calls;
	return ret;
}

static bool key_diffs(const uint8_t *p, const uint8_t *q, const uint8_t *n,
		      const uint8_t *d)
{
	return (memcmp(b_p, p, 128) != 0) || (memcmp(b_q, q, 128) != 0) ||
	       (memcmp(b_mod, n, 256) != 0) || (memcmp(b_d, d, 256) != 0);
}

ZTEST(keygen, zzz_bench_no_internal_drbg)
{
	const uint64_t cand_seed = 0x0BADF00D12345678ULL;
	const uint64_t edn_seed = 0x0000000011112222ULL;
	uint32_t rnd1, urnd1, rnd2, urnd2, rnd3, urnd3;
	int ret1, ret2, ret_ctrl, ret_edn;
	int same_p, same_q, same_n, same_d;
	int ctrl_diff, edn_diff;

	/* Run 1 and run 2: identical candidate stream and identical EDN stream. */
	ret1 = fixed_stream_keygen(cand_seed, edn_seed, b_p, b_q, b_mod, b_d,
				   &rnd1, &urnd1);
	ret2 = fixed_stream_keygen(cand_seed, edn_seed, b_p2, b_q2, b_mod2, b_d2,
				   &rnd2, &urnd2);
	same_p = (memcmp(b_p, b_p2, 128) == 0);
	same_q = (memcmp(b_q, b_q2, 128) == 0);
	same_n = (memcmp(b_mod, b_mod2, 256) == 0);
	same_d = (memcmp(b_d, b_d2, 256) == 0);

	/* Positive control: a different candidate seed must produce a different
	 * key, otherwise the byte comparison above would be vacuous. */
	ret_ctrl = fixed_stream_keygen(cand_seed ^ 0xFFFFFFFFFFFFFFFFULL, edn_seed,
				       b_p2, b_q2, b_mod2, b_d2, &rnd3, &urnd3);
	ctrl_diff = key_diffs(b_p2, b_q2, b_mod2, b_d2);

	/* Different EDN words, same candidate stream: shows whether the EDN
	 * stream participates in the key material at all. */
	ret_edn = fixed_stream_keygen(cand_seed, edn_seed ^ 0xA5A5A5A5A5A5A5A5ULL,
				      b_p2, b_q2, b_mod2, b_d2, &rnd3, &urnd3);
	edn_diff = key_diffs(b_p2, b_q2, b_mod2, b_d2);

	ZT_INFO("zzz_bench_no_internal_drbg",
		"ret=%d/%d/ctrl=%d,edn=%d,same_p=%d,same_q=%d,same_n=%d,same_d=%d,"
		"rnd_calls=%u/%u,urnd_calls=%u/%u,ctrl_diff=%d,edn_diff=%d",
		ret1, ret2, ret_ctrl, ret_edn,
		same_p, same_q, same_n, same_d,
		(unsigned)rnd1, (unsigned)rnd2,
		(unsigned)urnd1, (unsigned)urnd2,
		ctrl_diff, edn_diff);

	zassert_equal(ret1, 0, "run1 ret=%d", ret1);
	zassert_equal(ret2, 0, "run2 ret=%d", ret2);
	zassert_equal(ret_ctrl, 0, "control ret=%d", ret_ctrl);
	zassert_equal(ret_edn, 0, "edn-variant ret=%d", ret_edn);
	/* Same input streams => byte-identical key material. */
	zassert_true(same_p && same_q && same_n && same_d,
		     "identical streams produced different keys "
		     "(p=%d q=%d n=%d d=%d)", same_p, same_q, same_n, same_d);
	/* The comparison must be able to tell keys apart at all. */
	zassert_true(ctrl_diff, "different candidate seed produced the same key");
}
