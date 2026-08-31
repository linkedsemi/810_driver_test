/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * cipher 套件：AES-128-CBC 加解密 + SM4-ECB/CBC 加解密（标准向量）。
 *
 * 判定 = 标准向量逐字节比对 + 解密回环（expected/cipher_vectors.h）。
 * AES 走 mbedtls 通用 cipher API；SM4 走 LINKEDSEMI sm4_alt 硬件接口
 * （仅 CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT=y 的 case 编译：
 * case_hw_otbn_mix）。sm4_alt 只有 ECB/CTR 原语、无 CBC API（共享树现状，
 * 未改动），SM4-CBC 在测试层用 ECB 原语组合（CBC 语义标准链接），并与
 * OpenSSL 生成的标准 CBC 向量比对——ECB 原语 + 链接逻辑一起被 KAT 覆盖。
 */

#include "mbedtls_test_protocol.h"
#include "mbedtls_test.h"
#include "ztest_glue.h"

#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <mbedtls/aes.h>
#include <mbedtls/gcm.h>
#include "cipher_vectors.h"
#include "cipher_vectors_extra.h"

#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
#include <mbedtls/sm4_alt.h>
#define MT_SM4_BLOCK 16
#endif

ZTEST_SUITE(cipher, NULL, NULL, NULL, NULL, NULL);

/* ---- AES-128-CBC：NIST SP800-38A F.2.1（4 块，加密向量 + 解密回环） ---- */
ZTEST(cipher, aes128_cbc)
{
	mbedtls_aes_context aes;
	unsigned char key[16], iv0[16], iv[16], pt[64], ct[64], dec[64];
	uint32_t t0, us;
	int ret;

	/* mbedtls_aes_crypt_cbc 原地更新 IV → 加/解密各用一份拷贝，
	 * iv0 保持初始向量原件 */
	ret = mt_unhexify(key, MT_AES128CBC_KEY);
	if (ret == 16) {
		ret = mt_unhexify(iv0, MT_AES128CBC_IV);
	}
	if (ret == 16) {
		ret = mt_unhexify(pt, MT_AES128CBC_PT);
	}
	zassert_equal(ret, 64, "aes vector unhexify ret=%d", ret);
	memcpy(iv, iv0, sizeof(iv));

	t0 = k_cycle_get_32();

	mbedtls_aes_init(&aes);
	ret = mbedtls_aes_setkey_enc(&aes, key, 128);
	if (ret == 0) {
		ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT,
					    sizeof(pt), iv, pt, ct);
	}
	if (ret == 0) {
		uint8_t exp_ct[64];

		zassert_equal(mt_unhexify(exp_ct, MT_AES128CBC_CT), 64,
			      "aes ct hex invalid");
		mt_assert_mem_eq(ct, exp_ct, sizeof(pt), "aes128-cbc enc");
	}

	/* 解密回环：setkey_dec（AES 解密轮密钥路径），IV 用初始向量原件拷贝 */
	if (ret == 0) {
		memcpy(iv, iv0, sizeof(iv));
		ret = mbedtls_aes_setkey_dec(&aes, key, 128);
		if (ret == 0) {
			ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT,
						    sizeof(ct), iv, ct, dec);
		}
		if (ret == 0) {
			mt_assert_mem_eq(dec, pt, sizeof(pt), "aes128-cbc dec");
		}
	}
	mbedtls_aes_free(&aes);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("aes128_cbc", "algo=aes-128-cbc,bytes=%u,us=%u",
		(unsigned int)sizeof(pt), us);
	zassert_equal(ret, 0, "aes128_cbc ret=%d", ret);
}

#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
/* ---- SM4-ECB：draft-ribose-cfrg-sm4-10 A.2.1.1（加密向量 + 解密回环） ----
 * sm4_alt 硬件路径按 uint32_t 访问缓冲 → 缓冲 4 字节对齐。 */
ZTEST(cipher, sm4_ecb)
{
	mbedtls_sm4_context sm4;
	unsigned char key[16] __attribute__((aligned(4)));
	unsigned char pt[32] __attribute__((aligned(4)));
	unsigned char enc[32] __attribute__((aligned(4)));
	unsigned char dec[32] __attribute__((aligned(4)));
	uint32_t t0, us;
	int ret;

	zassert_equal(mt_unhexify(key, MT_SM4_KEY), 16, "sm4 key hex invalid");
	zassert_equal(mt_unhexify(pt, MT_SM4_PT), 32, "sm4 pt hex invalid");

	t0 = k_cycle_get_32();

	mbedtls_sm4_init(&sm4);
	ret = mbedtls_sm4_setkey(key);
	if (ret == 0) {
		ret = mbedtls_sm4_ecb_encrypt(&sm4, enc, pt, sizeof(pt));
	}
	if (ret == 0) {
		uint8_t exp_ct[32];

		zassert_equal(mt_unhexify(exp_ct, MT_SM4_ECB_CT), 32,
			      "sm4 ecb ct hex invalid");
		mt_assert_mem_eq(enc, exp_ct, sizeof(pt), "sm4-ecb enc");
	}
	if (ret == 0) {
		ret = mbedtls_sm4_ecb_decrypt(&sm4, dec, enc, sizeof(enc));
		if (ret == 0) {
			mt_assert_mem_eq(dec, pt, sizeof(pt), "sm4-ecb dec");
		}
	}
	mbedtls_sm4_free(&sm4);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("sm4_ecb", "algo=sm4-ecb,bytes=%u,us=%u",
		(unsigned int)sizeof(pt), us);
	zassert_equal(ret, 0, "sm4_ecb ret=%d", ret);
}

/* ---- SM4-CBC：CBC 语义用 sm4_alt 的 ECB 原语在测试层组合（见文件头），
 * 与 OpenSSL 生成的标准 CBC 向量比对 + 解密回环。 ---- */
static int sm4_cbc_crypt(mbedtls_sm4_context *sm4, unsigned char *out,
			 const unsigned char *in, size_t len,
			 const unsigned char *iv, bool encrypt)
{
	unsigned char chain[MT_SM4_BLOCK] __attribute__((aligned(4)));
	int ret = 0;

	memcpy(chain, iv, sizeof(chain));

	for (size_t off = 0; off < len && ret == 0; off += MT_SM4_BLOCK) {
		unsigned char block[MT_SM4_BLOCK] __attribute__((aligned(4)));
		unsigned char x[MT_SM4_BLOCK] __attribute__((aligned(4)));

		memcpy(block, in + off, MT_SM4_BLOCK);
		if (encrypt) {
			for (int i = 0; i < MT_SM4_BLOCK; i++) {
				x[i] = block[i] ^ chain[i];
			}
			ret = mbedtls_sm4_ecb_encrypt(sm4, out + off, x, MT_SM4_BLOCK);
			memcpy(chain, out + off, MT_SM4_BLOCK);
		} else {
			ret = mbedtls_sm4_ecb_decrypt(sm4, x, in + off, MT_SM4_BLOCK);
			if (ret == 0) {
				for (int i = 0; i < MT_SM4_BLOCK; i++) {
					out[off + i] = x[i] ^ chain[i];
				}
				memcpy(chain, in + off, MT_SM4_BLOCK);
			}
		}
	}
	return ret;
}

ZTEST(cipher, sm4_cbc)
{
	mbedtls_sm4_context sm4;
	unsigned char key[16] __attribute__((aligned(4)));
	unsigned char iv[16] __attribute__((aligned(4)));
	unsigned char pt[32] __attribute__((aligned(4)));
	unsigned char enc[32] __attribute__((aligned(4)));
	unsigned char dec[32] __attribute__((aligned(4)));
	uint32_t t0, us;
	int ret;

	zassert_equal(mt_unhexify(key, MT_SM4_KEY), 16, "sm4 key hex invalid");
	zassert_equal(mt_unhexify(pt, MT_SM4_PT), 32, "sm4 pt hex invalid");
	memcpy(iv, key, sizeof(iv));   /* 本向量 key=IV */

	t0 = k_cycle_get_32();

	mbedtls_sm4_init(&sm4);
	ret = mbedtls_sm4_setkey(key);
	if (ret == 0) {
		ret = sm4_cbc_crypt(&sm4, enc, pt, sizeof(pt), iv, true);
	}
	if (ret == 0) {
		uint8_t exp_ct[32];

		zassert_equal(mt_unhexify(exp_ct, MT_SM4_CBC_CT), 32,
			      "sm4 cbc ct hex invalid");
		mt_assert_mem_eq(enc, exp_ct, sizeof(pt), "sm4-cbc enc");
	}
	if (ret == 0) {
		ret = sm4_cbc_crypt(&sm4, dec, enc, sizeof(enc), iv, false);
		if (ret == 0) {
			mt_assert_mem_eq(dec, pt, sizeof(pt), "sm4-cbc dec");
		}
	}
	mbedtls_sm4_free(&sm4);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("sm4_cbc", "algo=sm4-cbc,bytes=%u,us=%u",
		(unsigned int)sizeof(pt), us);
	zassert_equal(ret, 0, "sm4_cbc ret=%d", ret);
}

/* ---- SM4-CTR：draft-ribose-cfrg-sm4-10 A.2.5.1 风格向量 ---- */
ZTEST(cipher, sm4_ctr)
{
	mbedtls_sm4_context sm4;
	unsigned char key[16] __attribute__((aligned(4)));
	unsigned char iv[16] __attribute__((aligned(4)));
	unsigned char pt[32] __attribute__((aligned(4)));
	unsigned char enc[32] __attribute__((aligned(4)));
	unsigned char dec[32] __attribute__((aligned(4)));
	uint32_t t0, us;
	int ret;

	zassert_equal(mt_unhexify(key, MT_SM4CTR_KEY), 16, "sm4 ctr key hex invalid");
	zassert_equal(mt_unhexify(iv, MT_SM4CTR_IV), 16, "sm4 ctr iv hex invalid");
	zassert_equal(mt_unhexify(pt, MT_SM4CTR_PT), 32, "sm4 ctr pt hex invalid");

	t0 = k_cycle_get_32();

	mbedtls_sm4_init(&sm4);
	ret = mbedtls_sm4_setkey(key);
	if (ret == 0) {
		ret = mbedtls_sm4_setiv(&sm4, iv);
	}
	if (ret == 0) {
		ret = mbedtls_sm4_ctr_crypto(&sm4, enc, pt, sizeof(pt));
	}
	if (ret == 0) {
		uint8_t exp_ct[32];

		zassert_equal(mt_unhexify(exp_ct, MT_SM4CTR_CT), 32,
			      "sm4 ctr ct hex invalid");
		mt_assert_mem_eq(enc, exp_ct, sizeof(pt), "sm4-ctr enc");
	}
	if (ret == 0) {
		ret = mbedtls_sm4_setiv(&sm4, iv);
		if (ret == 0) {
			ret = mbedtls_sm4_ctr_crypto(&sm4, dec, enc, sizeof(enc));
		}
		if (ret == 0) {
			mt_assert_mem_eq(dec, pt, sizeof(pt), "sm4-ctr dec");
		}
	}
	mbedtls_sm4_free(&sm4);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("sm4_ctr", "algo=sm4-ctr,bytes=%u,us=%u",
		(unsigned int)sizeof(pt), us);
	zassert_equal(ret, 0, "sm4_ctr ret=%d", ret);
}
#endif /* CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT */

/* ---- AES-128-CTR：NIST SP800-38A F.5.1（4 块，加密向量 + 解密回环） ---- */
ZTEST(cipher, aes128_ctr)
{
	mbedtls_aes_context aes;
	unsigned char key[16], iv0[16], iv[16], pt[64], ct[64], dec[64];
	unsigned char stream_block[16];
	size_t iv_off = 0;
	uint32_t t0, us;
	int ret;

	ret = mt_unhexify(key, MT_AES128CTR_KEY);
	if (ret == 16) {
		ret = mt_unhexify(iv0, MT_AES128CTR_IV);
	}
	if (ret == 16) {
		ret = mt_unhexify(pt, MT_AES128CTR_PT);
	}
	zassert_equal(ret, 64, "aes ctr vector unhexify ret=%d", ret);
	memcpy(iv, iv0, sizeof(iv));

	t0 = k_cycle_get_32();

	mbedtls_aes_init(&aes);
	ret = mbedtls_aes_setkey_enc(&aes, key, 128);
	if (ret == 0) {
		iv_off = 0;
		memset(stream_block, 0, sizeof(stream_block));
		ret = mbedtls_aes_crypt_ctr(&aes, sizeof(pt), &iv_off,
					    iv, stream_block, pt, ct);
	}
	if (ret == 0) {
		uint8_t exp_ct[64];

		zassert_equal(mt_unhexify(exp_ct, MT_AES128CTR_CT), 64,
			      "aes ctr ct hex invalid");
		mt_assert_mem_eq(ct, exp_ct, sizeof(pt), "aes128-ctr enc");
	}

	if (ret == 0) {
		memcpy(iv, iv0, sizeof(iv));
		ret = mbedtls_aes_setkey_enc(&aes, key, 128);
		if (ret == 0) {
			iv_off = 0;
			memset(stream_block, 0, sizeof(stream_block));
			ret = mbedtls_aes_crypt_ctr(&aes, sizeof(ct), &iv_off,
						    iv, stream_block, ct, dec);
		}
		if (ret == 0) {
			mt_assert_mem_eq(dec, pt, sizeof(pt), "aes128-ctr dec");
		}
	}
	mbedtls_aes_free(&aes);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("aes128_ctr", "algo=aes-128-ctr,bytes=%u,us=%u",
		(unsigned int)sizeof(pt), us);
	zassert_equal(ret, 0, "aes128_ctr ret=%d", ret);
}

/* ---- AES-128-GCM：NIST GCMVS-like，2 块 + AAD，tag 验证 ---- */
ZTEST(cipher, aes128_gcm)
{
	mbedtls_gcm_context gcm;
	unsigned char key[16], iv[16], pt[32], aad[16], ct[32], tag[16], dec[32];
	uint32_t t0, us;
	int ret;

	ret = mt_unhexify(key, MT_AES128GCM_KEY);
	if (ret == 16) {
		ret = mt_unhexify(iv, MT_AES128GCM_IV);
	}
	if (ret == 16) {
		ret = mt_unhexify(pt, MT_AES128GCM_PT);
	}
	if (ret == 32) {
		ret = mt_unhexify(aad, MT_AES128GCM_AAD);
	}
	zassert_equal(ret, 16, "aes gcm vector unhexify ret=%d", ret);

	t0 = k_cycle_get_32();

	mbedtls_gcm_init(&gcm);
	ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
	if (ret == 0) {
		ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
						sizeof(pt), iv, sizeof(iv),
						aad, sizeof(aad), pt, ct,
						sizeof(tag), tag);
	}
	if (ret == 0) {
		uint8_t exp_ct[32], exp_tag[16];

		zassert_equal(mt_unhexify(exp_ct, MT_AES128GCM_CT), 32,
			      "aes gcm ct hex invalid");
		zassert_equal(mt_unhexify(exp_tag, MT_AES128GCM_TAG), 16,
			      "aes gcm tag hex invalid");
		mt_assert_mem_eq(ct, exp_ct, sizeof(pt), "aes128-gcm enc");
		mt_assert_mem_eq(tag, exp_tag, sizeof(tag), "aes128-gcm tag");
	}
	if (ret == 0) {
		ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_DECRYPT,
						sizeof(ct), iv, sizeof(iv),
						aad, sizeof(aad), ct, dec,
						sizeof(tag), tag);
		if (ret == 0) {
			mt_assert_mem_eq(dec, pt, sizeof(pt), "aes128-gcm dec");
		}
	}
	mbedtls_gcm_free(&gcm);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("aes128_gcm", "algo=aes-128-gcm,bytes=%u,us=%u",
		(unsigned int)sizeof(pt), us);
	zassert_equal(ret, 0, "aes128_gcm ret=%d", ret);
}
