/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Goal 4: CPU-side small-prime trial division throughput.
 *
 * Pure C benchmark; it neither calls the OTBN driver nor touches driver
 * sources. The prime table is the same one embedded in
 * zephyr/soc/linkedsemi/lsqsh/otbn/otbn_rsa_keygen.c (1228 primes, 3..9973),
 * regenerated into bench_small_primes.h so both use identical values.
 *
 * The reduction loop is byte-for-byte the algorithm used by
 * otbn_rsa_has_small_factor(): little-endian candidate, folded from the most
 * significant byte, one 32-bit modulo per byte:
 *
 *     for (i = prime_bytes - 1; i >= 0; i--)
 *             rem = ((rem << 8) | candidate_le[i]) % p;
 *
 * 1000 random 128-byte candidates are generated (same masking as
 * otbn_rsa_gen_candidate: odd, 3 mod 4, top two bits set) and each candidate
 * stops at the first prime that divides it, exactly like the real search.
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "ztest_glue.h"
#include "bench_small_primes.h"

#define TD_CANDIDATES   1000
#define TD_CAND_BYTES   128

static uint8_t td_cand[TD_CANDIDATES][TD_CAND_BYTES];
static uint64_t td_ns[TD_CANDIDATES];
static uint16_t td_mods[TD_CANDIDATES];

static uint64_t td_splitmix64(uint64_t *state)
{
	uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);

	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

static void td_fill_random(uint8_t *buf, size_t len, uint64_t *state)
{
	uint64_t cur = 0;

	for (size_t i = 0; i < len; i++) {
		if ((i & 7) == 0) {
			cur = td_splitmix64(state);
		}
		buf[i] = (uint8_t)(cur >> ((i & 7) * 8));
	}
}

static uint64_t td_cycles_to_ns(uint64_t cycles)
{
	return (cycles * 1000000000ULL) / (uint64_t)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
}

static void td_sort_u64(uint64_t *v, size_t n)
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

ZTEST(keygen, zzz_bench_trial_division)
{
	uint64_t state = 0x5EED1234ABCD5678ULL;
	uint64_t total_ns = 0;
	uint64_t total_mods = 0;
	uint16_t max_mods = 0;
	uint32_t survivors = 0;
	uint64_t survivor_ns = 0;

	for (size_t c = 0; c < TD_CANDIDATES; c++) {
		uint8_t *cand = td_cand[c];
		uint64_t t0, t1;
		uint16_t nmod = 0;

		td_fill_random(cand, TD_CAND_BYTES, &state);
		/* Same fixups as otbn_rsa_gen_candidate(). */
		cand[0] |= 0x03U;
		cand[TD_CAND_BYTES - 1] |= 0xC0U;

		t0 = k_cycle_get_64();
		for (size_t idx = 0; idx < BENCH_NUM_SMALL_PRIMES; idx++) {
			uint32_t divisor = bench_small_primes[idx];
			uint32_t rem = 0;

			for (int i = TD_CAND_BYTES - 1; i >= 0; i--) {
				rem = ((rem << 8) | cand[i]) % divisor;
			}
			nmod++;
			if (rem == 0) {
				break;
			}
		}
		t1 = k_cycle_get_64();

		td_ns[c] = td_cycles_to_ns(t1 - t0);
		td_mods[c] = nmod;
		total_ns += td_ns[c];
		total_mods += nmod;
		if (nmod > max_mods) {
			max_mods = nmod;
		}
		if (nmod == BENCH_NUM_SMALL_PRIMES) {
			survivors++;
			survivor_ns += td_ns[c];
		}
	}

	size_t p90_idx = (TD_CANDIDATES * 9 + 9) / 10 - 1;
	uint64_t median;

	/* td_ns is sorted in place after the average is fixed. */
	uint64_t avg_ns = total_ns / TD_CANDIDATES;
	uint64_t min_ns = td_ns[0];
	uint64_t max_ns = td_ns[0];

	for (size_t c = 1; c < TD_CANDIDATES; c++) {
		if (td_ns[c] < min_ns) {
			min_ns = td_ns[c];
		}
		if (td_ns[c] > max_ns) {
			max_ns = td_ns[c];
		}
	}
	td_sort_u64(td_ns, TD_CANDIDATES);
	median = (td_ns[TD_CANDIDATES / 2 - 1] + td_ns[TD_CANDIDATES / 2] + 1) / 2;
	uint64_t p90_ns = td_ns[p90_idx];
	uint64_t ns_per_mod = total_mods ? (total_ns / total_mods) : 0;

	ZT_INFO("zzz_bench_trial_division",
		"candidates=%u,prime_bytes=%u,primes=%u,avg_ns=%llu,median_ns=%llu,"
		"p90_ns=%llu,min_ns=%llu,max_ns=%llu,avg_us_x1000=%llu,"
		"median_us_x1000=%llu,ns_per_mod=%llu,us_per_mod_x1000=%llu,"
		"total_mods=%llu,avg_mods=%llu,max_mods=%u,survivors=%u,"
		"survivor_avg_ns=%llu",
		TD_CANDIDATES, TD_CAND_BYTES, (unsigned)BENCH_NUM_SMALL_PRIMES,
		(unsigned long long)avg_ns, (unsigned long long)median,
		(unsigned long long)p90_ns, (unsigned long long)min_ns,
		(unsigned long long)max_ns,
		(unsigned long long)(avg_ns / 1000),
		(unsigned long long)(median / 1000),
		(unsigned long long)ns_per_mod,
		(unsigned long long)(ns_per_mod / 1000),
		(unsigned long long)total_mods,
		(unsigned long long)(total_mods / TD_CANDIDATES),
		(unsigned)max_mods, (unsigned)survivors,
		(unsigned long long)(survivors ? survivor_ns / survivors : 0));

	zassert_equal(BENCH_NUM_SMALL_PRIMES, 1228, "prime table size %u",
		      (unsigned)BENCH_NUM_SMALL_PRIMES);
	zassert_true(total_mods > 0, "no modulo operations measured");
}
