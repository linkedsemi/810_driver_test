/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * KAT bring-up for the extended OTBN modexp firmware.
 *
 * The rsa_imem image now implements two extra modulus widths (128 B / 192 B,
 * i.e. 1024 / 1536 bits) so that Miller-Rabin can run on prime candidates.
 * This suite drives the exported byte-level entry point directly and pins the
 * result to externally generated big-endian KATs:
 *
 *   mbedtls_ls_otbn_rsa_modexp(input, output, exp, n, num_bytes, is_public)
 *
 * Each vector asserts:
 *   1. the call did not return -ENOTSUP (the length whitelist accepted it),
 *   2. the result is byte-identical to the KAT,
 *   3. the OTBN firmware state is RSA_MODEXP afterwards (the local OTBN path
 *      really ran; there is no software fallback in this entry point).
 *
 * The vectors live in tests/expected/modexp_vectors.h, generated from
 * rsa_keygen/tpm_otbn_hook/modexp_test_vectors.txt.
 *
 * Test names carry the "zzz_" prefix on purpose: ztest executes in link order
 * (SORT_BY_NAME), so they run after the keygen suite and cannot perturb it.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "ztest_glue.h"

#include "ls_otbn_config.h"          /* ls_otbn_imem_firmware_get() */
#include "mbedtls_otbn_rsa_mbox.h"   /* mbedtls_ls_otbn_rsa_modexp() */

#include "modexp_vectors.h"

#define MODEXP_KAT_MAX_BYTES 512

/* File scope: the ztest thread stack must not carry 4 x 512 B buffers. */
static uint8_t g_in[MODEXP_KAT_MAX_BYTES] __aligned(4);
static uint8_t g_exp[MODEXP_KAT_MAX_BYTES] __aligned(4);
static uint8_t g_n[MODEXP_KAT_MAX_BYTES] __aligned(4);
static uint8_t g_out[MODEXP_KAT_MAX_BYTES] __aligned(4);

static void run_modexp_kat(const char *name, const modexp_vec_t *vec)
{
	int64_t t0, t1;
	size_t first_diff = (size_t)-1;
	int ret, eq = 0;
	unsigned fw;

	zassert_true(vec->num_bytes <= MODEXP_KAT_MAX_BYTES,
		     "%s: vector width %u exceeds buffer", name,
		     (unsigned)vec->num_bytes);

	memcpy(g_in, vec->base, vec->num_bytes);
	memcpy(g_exp, vec->exp, vec->num_bytes);
	memcpy(g_n, vec->n, vec->num_bytes);
	memset(g_out, 0xA5, sizeof(g_out));

	t0 = k_uptime_get();
	ret = mbedtls_ls_otbn_rsa_modexp(g_in, g_out, g_exp, g_n,
					 vec->num_bytes, true);
	t1 = k_uptime_get();
	fw = (unsigned)ls_otbn_imem_firmware_get();

	if (ret == 0) {
		for (size_t i = 0; i < vec->num_bytes; i++) {
			if (g_out[i] != vec->out[i]) {
				first_diff = i;
				break;
			}
		}
		eq = (first_diff == (size_t)-1);
	}

	/* Emit the measurement before asserting so the INFO line is always
	 * archived, including on failure. */
	ZT_INFO(name, "ret=%d,ms=%lld,byte_equal=%d,first_diff=%d,fw=%u",
		ret, (long long)(t1 - t0), eq,
		(first_diff == (size_t)-1) ? -1 : (int)first_diff, fw);

	zassert_true(ret != -ENOTSUP,
		     "%s: -ENOTSUP (length whitelist rejected %u bytes)",
		     name, (unsigned)vec->num_bytes);
	zassert_equal(ret, 0, "%s: modexp ret=%d", name, ret);
	zassert_true(eq, "%s: output differs from KAT (first_diff=%d)",
		     name, (first_diff == (size_t)-1) ? -1 : (int)first_diff);

	/* No software fallback exists in this entry point; the shared firmware
	 * state proves the local OTBN modexp image executed. */
	zassert_equal(fw, (unsigned)OTBN_FIRMWARE_RSA_MODEXP,
		      "%s: firmware state %u != RSA_MODEXP(%u)", name, fw,
		      (unsigned)OTBN_FIRMWARE_RSA_MODEXP);
}

ZTEST(keygen, zzz_modexp_kat_n128b_random)
{
	run_modexp_kat("zzz_modexp_kat_n128b_random", &modexp_vectors[0]);
}

ZTEST(keygen, zzz_modexp_kat_n192b_random)
{
	run_modexp_kat("zzz_modexp_kat_n192b_random", &modexp_vectors[1]);
}

ZTEST(keygen, zzz_modexp_kat_n256b_random)
{
	run_modexp_kat("zzz_modexp_kat_n256b_random", &modexp_vectors[2]);
}

ZTEST(keygen, zzz_modexp_kat_n4b_base1)
{
	run_modexp_kat("zzz_modexp_kat_n4b_base1", &modexp_vectors[3]);
}

ZTEST(keygen, zzz_modexp_kat_n4b_base0)
{
	run_modexp_kat("zzz_modexp_kat_n4b_base0", &modexp_vectors[4]);
}

ZTEST(keygen, zzz_modexp_kat_n4b_exp0)
{
	run_modexp_kat("zzz_modexp_kat_n4b_exp0", &modexp_vectors[5]);
}

ZTEST(keygen, zzz_modexp_kat_n4b_exp1)
{
	run_modexp_kat("zzz_modexp_kat_n4b_exp1", &modexp_vectors[6]);
}

ZTEST(keygen, zzz_modexp_kat_n4b_basenm1)
{
	run_modexp_kat("zzz_modexp_kat_n4b_basenm1", &modexp_vectors[7]);
}

/* Regression of the pre-existing 384/512 B entry points. Host-generated KATs
 * (Python pow); N512B_f4 uses e=65537, i.e. the F4 mode branch. */
ZTEST(keygen, zzz_modexp_kat_n384b_hostkat)
{
	run_modexp_kat("zzz_modexp_kat_n384b_hostkat", &modexp_vectors[8]);
}

ZTEST(keygen, zzz_modexp_kat_n512b_hostkat)
{
	run_modexp_kat("zzz_modexp_kat_n512b_hostkat", &modexp_vectors[9]);
}

ZTEST(keygen, zzz_modexp_kat_n512b_f4_hostkat)
{
	run_modexp_kat("zzz_modexp_kat_n512b_f4_hostkat", &modexp_vectors[10]);
}
