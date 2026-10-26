/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Crypto throughput: accelerated SHA256/SHA512/SM3 and AES/SM4 ciphers.
 *
 * Each algorithm is measured at 1/4/16/32 KB.  The engine (hardware vs OTBN
 * for SHA/SM3) is selected by the per-case prj.conf; this source is shared.
 * Rates are informational; every path still asserts the mbedtls return value.
 */
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <string.h>

#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/sm3_alt.h"
#include "mbedtls/aes.h"
#include "mbedtls/sm4_alt.h"

#include "perf_common.h"
#include "perf_buf.h"

static const size_t crypto_sizes[] = { KB(1), KB(4), KB(16), KB(32) };

static const uint8_t aes_key[16] = { 0 };
static const uint8_t aes_iv[16] = { 0 };
static const uint8_t sm4_key[16] = { 0 };

/* ---------------------------------------------------------------- hashes */

ZTEST(perf_crypto, sha256)
{
	mbedtls_sha256_context ctx;
	uint8_t digest[32];

	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		uint64_t t0, t1;

		mbedtls_sha256_init(&ctx);
		zassert_equal(mbedtls_sha256_starts(&ctx, 0), 0, "sha256 starts");

		t0 = k_cycle_get_64();
		zassert_equal(mbedtls_sha256_update(&ctx, perf_buf, size), 0,
			      "sha256 update %u", (unsigned)size);
		zassert_equal(mbedtls_sha256_finish(&ctx, digest), 0,
			      "sha256 finish");
		t1 = k_cycle_get_64();

		mbedtls_sha256_free(&ctx);
		perf_bytes("crypto", "sha256", size, k_cyc_to_us_floor64(t1 - t0));
	}
}

ZTEST(perf_crypto, sha512)
{
	mbedtls_sha512_context ctx;
	uint8_t digest[64];

	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		uint64_t t0, t1;

		mbedtls_sha512_init(&ctx);
		zassert_equal(mbedtls_sha512_starts(&ctx, 0), 0, "sha512 starts");

		t0 = k_cycle_get_64();
		zassert_equal(mbedtls_sha512_update(&ctx, perf_buf, size), 0,
			      "sha512 update %u", (unsigned)size);
		zassert_equal(mbedtls_sha512_finish(&ctx, digest), 0,
			      "sha512 finish");
		t1 = k_cycle_get_64();

		mbedtls_sha512_free(&ctx);
		perf_bytes("crypto", "sha512", size, k_cyc_to_us_floor64(t1 - t0));
	}
}

ZTEST(perf_crypto, sm3)
{
	mbedtls_sm3_context ctx;
	uint8_t digest[32];

	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		uint64_t t0, t1;

		mbedtls_sm3_init(&ctx);
		zassert_equal(mbedtls_sm3_starts(&ctx), 0, "sm3 starts");

		t0 = k_cycle_get_64();
		zassert_equal(mbedtls_sm3_update(&ctx, perf_buf, size), 0,
			      "sm3 update %u", (unsigned)size);
		zassert_equal(mbedtls_sm3_finish(&ctx, digest), 0, "sm3 finish");
		t1 = k_cycle_get_64();

		mbedtls_sm3_free(&ctx);
		perf_bytes("crypto", "sm3", size, k_cyc_to_us_floor64(t1 - t0));
	}
}

/* ---------------------------------------------------------------- ciphers */

ZTEST(perf_crypto, aes128_ecb)
{
	mbedtls_aes_context ctx;

	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		uint64_t t0, t1;

		mbedtls_aes_init(&ctx);
		zassert_equal(mbedtls_aes_setkey_enc(&ctx, aes_key, 128), 0,
			      "aes setkey");

		t0 = k_cycle_get_64();
		for (size_t off = 0; off < size; off += 16) {
			zassert_equal(mbedtls_aes_crypt_ecb(&ctx,
							    MBEDTLS_AES_ENCRYPT,
							    perf_buf + off,
							    perf_buf + off), 0,
				      "aes ecb at %u", (unsigned)off);
		}
		t1 = k_cycle_get_64();

		mbedtls_aes_free(&ctx);
		perf_bytes("crypto", "aes128_ecb", size,
			   k_cyc_to_us_floor64(t1 - t0));
	}
}

ZTEST(perf_crypto, aes128_cbc)
{
	mbedtls_aes_context ctx;

	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		uint8_t iv[16];
		uint64_t t0, t1;

		memcpy(iv, aes_iv, sizeof(iv));
		mbedtls_aes_init(&ctx);
		zassert_equal(mbedtls_aes_setkey_enc(&ctx, aes_key, 128), 0,
			      "aes setkey");

		t0 = k_cycle_get_64();
		zassert_equal(mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT,
						    size, iv, perf_buf, perf_buf),
			      0, "aes cbc %u", (unsigned)size);
		t1 = k_cycle_get_64();

		mbedtls_aes_free(&ctx);
		perf_bytes("crypto", "aes128_cbc", size,
			   k_cyc_to_us_floor64(t1 - t0));
	}
}

ZTEST(perf_crypto, aes128_ctr)
{
	mbedtls_aes_context ctx;

	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		uint8_t nonce[16];
		uint8_t stream[16];
		size_t nc_off = 0;
		uint64_t t0, t1;

		memcpy(nonce, aes_iv, sizeof(nonce));
		mbedtls_aes_init(&ctx);
		zassert_equal(mbedtls_aes_setkey_enc(&ctx, aes_key, 128), 0,
			      "aes setkey");

		t0 = k_cycle_get_64();
		zassert_equal(mbedtls_aes_crypt_ctr(&ctx, size, &nc_off,
						    nonce, stream,
						    perf_buf, perf_buf), 0,
			      "aes ctr %u", (unsigned)size);
		t1 = k_cycle_get_64();

		mbedtls_aes_free(&ctx);
		perf_bytes("crypto", "aes128_ctr", size,
			   k_cyc_to_us_floor64(t1 - t0));
	}
}

ZTEST(perf_crypto, sm4_ecb)
{
	mbedtls_sm4_context ctx;

	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		uint64_t t0, t1;

		mbedtls_sm4_init(&ctx);
		zassert_equal(mbedtls_sm4_setkey(sm4_key), 0, "sm4 setkey");

		t0 = k_cycle_get_64();
		zassert_equal(mbedtls_sm4_ecb_encrypt(&ctx, perf_buf, perf_buf,
						      size), 0,
			      "sm4 ecb %u", (unsigned)size);
		t1 = k_cycle_get_64();

		mbedtls_sm4_free(&ctx);
		perf_bytes("crypto", "sm4_ecb", size,
			   k_cyc_to_us_floor64(t1 - t0));
	}
}

ZTEST(perf_crypto, sm4_ctr)
{
	mbedtls_sm4_context ctx;

	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		uint8_t iv[16];
		uint64_t t0, t1;

		memcpy(iv, aes_iv, sizeof(iv));
		mbedtls_sm4_init(&ctx);
		zassert_equal(mbedtls_sm4_setkey(sm4_key), 0, "sm4 setkey");
		zassert_equal(mbedtls_sm4_setiv(&ctx, iv), 0, "sm4 setiv");

		t0 = k_cycle_get_64();
		zassert_equal(mbedtls_sm4_ctr_crypto(&ctx, perf_buf, perf_buf,
						     size), 0,
			      "sm4 ctr %u", (unsigned)size);
		t1 = k_cyc_to_us_floor64(k_cycle_get_64() - t0);

		mbedtls_sm4_free(&ctx);
		perf_bytes("crypto", "sm4_ctr", size, t1);
	}
}

/* ==================================================================
 * Unaligned inputs (perf_buf + 1): the hardware engines have a fast path
 * only for aligned buffers, so the aligned/unaligned delta is itself a
 * result worth measuring.  The buffer is filled with a constant byte so the
 * aligned reference and the unaligned run hash identical content.
 * ================================================================== */

static void ref_sha256(const uint8_t *in, size_t len, uint8_t out[32])
{
	mbedtls_sha256_context c;

	mbedtls_sha256_init(&c);
	zassert_equal(mbedtls_sha256_starts(&c, 0), 0, "sha256 ref starts");
	zassert_equal(mbedtls_sha256_update(&c, in, len), 0, "sha256 ref update");
	zassert_equal(mbedtls_sha256_finish(&c, out), 0, "sha256 ref finish");
	mbedtls_sha256_free(&c);
}

static void ref_sha512(const uint8_t *in, size_t len, uint8_t out[64])
{
	mbedtls_sha512_context c;

	mbedtls_sha512_init(&c);
	zassert_equal(mbedtls_sha512_starts(&c, 0), 0, "sha512 ref starts");
	zassert_equal(mbedtls_sha512_update(&c, in, len), 0, "sha512 ref update");
	zassert_equal(mbedtls_sha512_finish(&c, out), 0, "sha512 ref finish");
	mbedtls_sha512_free(&c);
}

static void ref_sm3(const uint8_t *in, size_t len, uint8_t out[32])
{
	mbedtls_sm3_context c;

	mbedtls_sm3_init(&c);
	zassert_equal(mbedtls_sm3_starts(&c), 0, "sm3 ref starts");
	zassert_equal(mbedtls_sm3_update(&c, in, len), 0, "sm3 ref update");
	zassert_equal(mbedtls_sm3_finish(&c, out), 0, "sm3 ref finish");
	mbedtls_sm3_free(&c);
}

ZTEST(perf_crypto, sha256_unaligned)
{
	uint8_t ref[32];
	uint8_t out[32];

	memset(perf_buf, 0x5A, PERF_BUF_SIZE + PERF_BUF_PAD);

	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		mbedtls_sha256_context c;
		uint64_t t0, t1;

		ref_sha256(perf_buf, size, ref);

		mbedtls_sha256_init(&c);
		zassert_equal(mbedtls_sha256_starts(&c, 0), 0, "sha256 u starts");

		t0 = k_cycle_get_64();
		zassert_equal(mbedtls_sha256_update(&c, perf_buf + 1, size), 0,
			      "sha256 u update %u", (unsigned)size);
		zassert_equal(mbedtls_sha256_finish(&c, out), 0, "sha256 u finish");
		t1 = k_cycle_get_64();

		mbedtls_sha256_free(&c);
		zassert_mem_equal(ref, out, sizeof(out),
				  "sha256 unaligned digest mismatch size=%u",
				  (unsigned)size);
		perf_bytes("crypto", "sha256_unaligned", size,
			   k_cyc_to_us_floor64(t1 - t0));
	}
}

ZTEST(perf_crypto, sha512_unaligned)
{
	uint8_t ref[64];
	uint8_t out[64];

	memset(perf_buf, 0x5A, PERF_BUF_SIZE + PERF_BUF_PAD);

	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		mbedtls_sha512_context c;
		uint64_t t0, t1;

		ref_sha512(perf_buf, size, ref);

		mbedtls_sha512_init(&c);
		zassert_equal(mbedtls_sha512_starts(&c, 0), 0, "sha512 u starts");

		t0 = k_cycle_get_64();
		zassert_equal(mbedtls_sha512_update(&c, perf_buf + 1, size), 0,
			      "sha512 u update %u", (unsigned)size);
		zassert_equal(mbedtls_sha512_finish(&c, out), 0, "sha512 u finish");
		t1 = k_cycle_get_64();

		mbedtls_sha512_free(&c);
		zassert_mem_equal(ref, out, sizeof(out),
				  "sha512 unaligned digest mismatch size=%u",
				  (unsigned)size);
		perf_bytes("crypto", "sha512_unaligned", size,
			   k_cyc_to_us_floor64(t1 - t0));
	}
}

ZTEST(perf_crypto, sm3_unaligned)
{
	uint8_t ref[32];
	uint8_t out[32];

	memset(perf_buf, 0x5A, PERF_BUF_SIZE + PERF_BUF_PAD);

	for (size_t s = 0; s < ARRAY_SIZE(crypto_sizes); s++) {
		size_t size = crypto_sizes[s];
		mbedtls_sm3_context c;
		uint64_t t0, t1;

		ref_sm3(perf_buf, size, ref);

		mbedtls_sm3_init(&c);
		zassert_equal(mbedtls_sm3_starts(&c), 0, "sm3 u starts");

		t0 = k_cycle_get_64();
		zassert_equal(mbedtls_sm3_update(&c, perf_buf + 1, size), 0,
			      "sm3 u update %u", (unsigned)size);
		zassert_equal(mbedtls_sm3_finish(&c, out), 0, "sm3 u finish");
		t1 = k_cycle_get_64();

		mbedtls_sm3_free(&c);
		zassert_mem_equal(ref, out, sizeof(out),
				  "sm3 unaligned digest mismatch size=%u",
				  (unsigned)size);
		perf_bytes("crypto", "sm3_unaligned", size,
			   k_cyc_to_us_floor64(t1 - t0));
	}
}

ZTEST_SUITE(perf_crypto, NULL, NULL, NULL, NULL, NULL);
