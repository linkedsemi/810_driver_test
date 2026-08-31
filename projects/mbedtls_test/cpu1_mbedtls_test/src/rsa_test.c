/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * rsa 套件：RSA-2048（软件路径，Phase 1 所有 case 均不开 RSA OTBN ALT）。
 *   - rsa2048_modexp：导入预置密钥对 → public modexp（encrypt）+ private
 *     modexp（decrypt）+ PKCS#1 v1.5 sign/verify + OpenSSL 黄金密文/签名 KAT
 *   - rsa2048_keygen：F4 / 2048 一档 keygen → check_privkey → sign/verify
 *     （3072/4096 Phase 2 再加；keygen 用 TRNG 真随机源）
 *
 * modexp KAT（expected/rsa_vectors.h，OpenSSL 生成，otbn_test 同源）保证
 * 判定向量化：decrypt(黄金密文)==黄金明文、sign(黄金摘要)==黄金签名。
 */

#include "mbedtls_test_protocol.h"
#include "mbedtls_test.h"
#include "ztest_glue.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>

#include "rsa_vectors.h"
#include "rsa_vectors_3072_4096.h"

ZTEST_SUITE(rsa, NULL, NULL, NULL, NULL, NULL);

int rsa_import_test_key(mbedtls_rsa_context *rsa)
{
	int ret;

	mbedtls_rsa_init(rsa);

	ret = mbedtls_rsa_import_raw(rsa,
				     rsa2048_n, sizeof(rsa2048_n),
				     NULL, 0,
				     NULL, 0,
				     rsa2048_d, sizeof(rsa2048_d),
				     rsa2048_e, sizeof(rsa2048_e));
	if (ret != 0) {
		return ret;
	}

	return mbedtls_rsa_complete(rsa);
}

const unsigned char rsa_msg[] = "hello rsa otbn test";

/* ---- modexp：public+private 指数运算 + PKCS#1 v1.5 KAT ---- */
ZTEST(rsa, rsa2048_modexp)
{
	mbedtls_rsa_context rsa;
	unsigned char hash[32];
	unsigned char enc[256];
	unsigned char dec[256];
	unsigned char sig[512];
	size_t dec_len;
	uint32_t t0, us;
	int ret;

	ret = rsa_import_test_key(&rsa);
	if (ret == 0) {
		t0 = k_cycle_get_32();
		/* public modexp：LCG 确定性 padding 加密 → private modexp 解密回环 */
		ret = mbedtls_rsa_pkcs1_encrypt(&rsa, mt_rng_lcg, NULL,
						sizeof(rsa_msg) - 1, rsa_msg, enc);
		if (ret == 0) {
			ret = mbedtls_rsa_pkcs1_decrypt(&rsa, mt_rng_lcg, NULL,
							&dec_len, enc, dec, sizeof(dec));
		}
		if (ret == 0) {
			zassert_equal(dec_len, sizeof(rsa_msg) - 1,
				      "rsa decrypt_len %u != %u",
				      (unsigned int)dec_len,
				      (unsigned int)(sizeof(rsa_msg) - 1));
			mt_assert_mem_eq(dec, rsa_msg, dec_len, "rsa enc/dec roundtrip");
		}
		us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
		ZT_INFO("rsa2048_modexp", "algo=rsa2048,stage=encdec,us=%u", us);
	}

	/* SHA-256(msg) → sign → 黄金签名比对 → verify */
	if (ret == 0) {
		mbedtls_sha256_context sha;

		mbedtls_sha256_init(&sha);
		ret = mbedtls_sha256_starts(&sha, 0);
		if (ret == 0) {
			ret = mbedtls_sha256_update(&sha, rsa_msg, sizeof(rsa_msg) - 1);
		}
		if (ret == 0) {
			ret = mbedtls_sha256_finish(&sha, hash);
		}
		mbedtls_sha256_free(&sha);
	}

	if (ret == 0) {
		t0 = k_cycle_get_32();
		/* RSASSA-PKCS1-v1_5 确定性：签名与 OpenSSL 黄金值逐字节可比 */
		ret = mbedtls_rsa_pkcs1_sign(&rsa, mt_rng_lcg, NULL,
					     MBEDTLS_MD_SHA256, 32, hash, sig);
		if (ret == 0) {
			mt_assert_mem_eq(sig, rsa2048_signature,
					 sizeof(rsa2048_signature), "rsa kat_sign");
		}
		if (ret == 0) {
			ret = mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256, 32,
						       hash, sig);
		}
		us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
		ZT_INFO("rsa2048_modexp", "algo=rsa2048,stage=signverify,us=%u", us);
	}

	/* KAT：解密 OpenSSL 黄金密文 → 黄金明文 */
	if (ret == 0) {
		ret = mbedtls_rsa_pkcs1_decrypt(&rsa, mt_rng_lcg, NULL,
						&dec_len, rsa2048_ciphertext,
						dec, sizeof(dec));
		if (ret == 0) {
			zassert_equal(dec_len, sizeof(rsa_msg) - 1,
				      "rsa kat_decrypt_len %u != %u",
				      (unsigned int)dec_len,
				      (unsigned int)(sizeof(rsa_msg) - 1));
			mt_assert_mem_eq(dec, rsa_msg, dec_len, "rsa kat_decrypt");
		}
	}

	/* KAT：验证 OpenSSL 黄金签名 */
	if (ret == 0) {
		ret = mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256, 32,
					       hash, rsa2048_signature);
	}

	mbedtls_rsa_free(&rsa);
	zassert_equal(ret, 0, "rsa2048_modexp ret=%d", ret);
}

/* ---- keygen（F4, 2048 一档）：keygen → check_privkey → sign/verify ---- */
ZTEST(rsa, rsa2048_keygen)
{
	mbedtls_rsa_context rsa;
	unsigned char hash[32];
	unsigned char sig[512];
	mbedtls_sha256_context sha;
	int64_t keygen_t0;
	uint32_t keygen_ms;
	int ret;

	keygen_t0 = k_uptime_get();
	mbedtls_rsa_init(&rsa);
	ret = mbedtls_rsa_gen_key(&rsa, mt_rng_trng, NULL, 2048, 65537);
	keygen_ms = (uint32_t)k_uptime_delta(&keygen_t0);
	zassert_equal(ret, 0, "rsa_keygen ret=%d", ret);
	if (ret != 0) {
		return;
	}

	ret = mbedtls_rsa_check_privkey(&rsa);
	if (ret == 0) {
		mbedtls_sha256_init(&sha);
		ret = mbedtls_sha256_starts(&sha, 0);
		if (ret == 0) {
			ret = mbedtls_sha256_update(&sha, rsa_msg, sizeof(rsa_msg) - 1);
		}
		if (ret == 0) {
			ret = mbedtls_sha256_finish(&sha, hash);
		}
		mbedtls_sha256_free(&sha);
	}
	if (ret == 0) {
		ret = mbedtls_rsa_pkcs1_sign(&rsa, mt_rng_lcg, NULL,
					     MBEDTLS_MD_SHA256, 32, hash, sig);
	}
	if (ret == 0) {
		ret = mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256, 32,
					       hash, sig);
	}

	ZT_INFO("rsa2048_keygen", "algo=rsa2048,keylen=2048,exponent=65537,time_ms=%u",
		keygen_ms);
	mbedtls_rsa_free(&rsa);
	zassert_equal(ret, 0, "rsa2048_keygen ret=%d", ret);
}

/* ---- modexp helper for fixed n/d/e vectors ---- */
static int rsa_modexp_kat(int bits,
			  const unsigned char *n, size_t n_len,
			  const unsigned char *d, size_t d_len,
			  const unsigned char *e, size_t e_len,
			  const unsigned char *ciphertext, size_t ct_len,
			  const unsigned char *signature, size_t sig_len)
{
	mbedtls_rsa_context rsa;
	unsigned char hash[32];
	unsigned char dec[512];
	unsigned char sig[512];
	size_t dec_len;
	uint32_t t0, us;
	int ret;

	mbedtls_rsa_init(&rsa);
	ret = mbedtls_rsa_import_raw(&rsa,
				     n, n_len,
				     NULL, 0,
				     NULL, 0,
				     d, d_len,
				     e, e_len);
	if (ret == 0) {
		ret = mbedtls_rsa_complete(&rsa);
	}

	if (ret == 0) {
		t0 = k_cycle_get_32();
		ret = mbedtls_rsa_pkcs1_decrypt(&rsa, mt_rng_lcg, NULL,
						  &dec_len, ciphertext,
						  dec, sizeof(dec));
		if (ret == 0) {
			zassert_equal(dec_len, sizeof(rsa_msg) - 1,
				      "rsa%d kat_decrypt_len %u != %u", bits,
				      (unsigned int)dec_len,
				      (unsigned int)(sizeof(rsa_msg) - 1));
			mt_assert_mem_eq(dec, rsa_msg, dec_len,
					 "rsa%d kat_decrypt", bits);
		}
		us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
		ZT_INFO("rsa%d_modexp", "algo=rsa%d,stage=dec,us=%u", bits, us);
	}

	if (ret == 0) {
		mbedtls_sha256_context sha;

		mbedtls_sha256_init(&sha);
		ret = mbedtls_sha256_starts(&sha, 0);
		if (ret == 0) {
			ret = mbedtls_sha256_update(&sha, rsa_msg, sizeof(rsa_msg) - 1);
		}
		if (ret == 0) {
			ret = mbedtls_sha256_finish(&sha, hash);
		}
		mbedtls_sha256_free(&sha);
	}

	if (ret == 0) {
		t0 = k_cycle_get_32();
		ret = mbedtls_rsa_pkcs1_sign(&rsa, mt_rng_lcg, NULL,
					     MBEDTLS_MD_SHA256, 32, hash, sig);
		if (ret == 0) {
			mt_assert_mem_eq(sig, signature, sig_len,
					 "rsa%d kat_sign", bits);
		}
		if (ret == 0) {
			ret = mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256, 32,
						       hash, sig);
		}
		us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
		ZT_INFO("rsa%d_modexp", "algo=rsa%d,stage=signverify,us=%u",
			bits, us);
	}

	mbedtls_rsa_free(&rsa);
	return ret;
}

/* ---- RSA-3072 modexp KAT ---- */
ZTEST(rsa, rsa3072_modexp)
{
	int ret = rsa_modexp_kat(3072,
				 rsa3072_n, sizeof(rsa3072_n),
				 rsa3072_d, sizeof(rsa3072_d),
				 rsa3072_e, sizeof(rsa3072_e),
				 rsa3072_ciphertext, sizeof(rsa3072_ciphertext),
				 rsa3072_signature, sizeof(rsa3072_signature));

	zassert_equal(ret, 0, "rsa3072_modexp ret=%d", ret);
}

/* ---- RSA-4096 modexp KAT ---- */
ZTEST(rsa, rsa4096_modexp)
{
	int ret = rsa_modexp_kat(4096,
				 rsa4096_n, sizeof(rsa4096_n),
				 rsa4096_d, sizeof(rsa4096_d),
				 rsa4096_e, sizeof(rsa4096_e),
				 rsa4096_ciphertext, sizeof(rsa4096_ciphertext),
				 rsa4096_signature, sizeof(rsa4096_signature));

	zassert_equal(ret, 0, "rsa4096_modexp ret=%d", ret);
}
