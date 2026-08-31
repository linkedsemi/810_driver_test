/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ecc 套件：ECDSA P-256 / P-384 / SM2。
 *   - p256_kat：固定 d/Q/k/hash → sign 比对黄金 r/s → verify（标准向量）
 *   - p256/p384/sm2 keygen：genkey（TRNG）→ sign（固定 k）→ verify 回环
 *
 * 曲线/签名实现落在软件还是 OTBN（MBEDTLS_ECDSA_..._LINKEDSEMI_OTBN_ALT）
 * 由 case Kconfig 路由决定；SM2 曲线仅在 CONFIG_MBEDTLS_ECP_DP_SM2_ENABLED
 * 的 case 编译（otbn 两个 case）。genkey 内部经 mbedtls_ecp_mul 语义得到
 * 仿射公钥 Q（OTBN 返回 Montgomery 形式由 ecp 层负责映射，回环 verify
 * 用同一 Q 校验——wolfSSL 侧需手工 map=1 的坑在 mbedtls 侧由 ecp 层消化，
 * otbn_test 同模式已在硬件验证）。
 */

#include "mbedtls_test_protocol.h"
#include "mbedtls_test.h"
#include "ztest_glue.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <mbedtls/bignum.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>

ZTEST_SUITE(ecc, NULL, NULL, NULL, NULL, NULL);

#define MT_ECC_MAX_HASH  66

/* ---- P-256 定 k 签名 KAT：向量见 expected/ecdsa_vectors.h ---- */
static int ecdsa_p256_kat(void)
{
	mbedtls_ecp_group grp;
	mbedtls_ecp_point Q;
	mbedtls_mpi d, r, s, r_exp, s_exp;
	unsigned char k_buf[66];
	unsigned char hash[MT_ECC_MAX_HASH];
	size_t hlen;
	int klen;
	int ret = 0;

	mbedtls_ecp_group_init(&grp);
	mbedtls_ecp_point_init(&Q);
	mbedtls_mpi_init(&d);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);
	mbedtls_mpi_init(&r_exp);
	mbedtls_mpi_init(&s_exp);

	ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
	if (ret == 0) {
		ret = mbedtls_ecp_point_read_string(&Q, 16,
						    MT_P256_QX_HEX, MT_P256_QY_HEX);
	}
	if (ret == 0) {
		ret = mbedtls_mpi_read_string(&d, 16, MT_P256_D_HEX);
	}
	if (ret == 0) {
		ret = mbedtls_mpi_read_string(&r_exp, 16, MT_P256_R_HEX);
	}
	if (ret == 0) {
		ret = mbedtls_mpi_read_string(&s_exp, 16, MT_P256_S_HEX);
	}
	if (ret != 0) {
		goto exit;
	}

	memset(k_buf, 0, sizeof(k_buf));
	klen = mt_unhexify(k_buf, MT_P256_K_HEX);
	if (klen <= 0) {
		zassert_true(false, "p256 kat k hex invalid");
		ret = -1;
		goto exit;
	}
	hlen = (size_t)mt_unhexify(hash, MT_P256_HASH_HEX);
	if (hlen == 0) {
		zassert_true(false, "p256 kat hash hex invalid");
		ret = -1;
		goto exit;
	}

	/* 定 k 签名（黄金 r/s 按此 k 算出） */
	ret = mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, hlen, mt_rng_kbuf, k_buf);
	if (ret != 0) {
		goto exit;
	}
	mt_assert_mpi_eq(&r, &r_exp, sizeof(k_buf), "p256 kat r");
	mt_assert_mpi_eq(&s, &s_exp, sizeof(k_buf), "p256 kat s");

	ret = mbedtls_ecdsa_verify(&grp, hash, hlen, &Q, &r_exp, &s_exp);

exit:
	mbedtls_ecp_group_free(&grp);
	mbedtls_ecp_point_free(&Q);
	mbedtls_mpi_free(&d);
	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	mbedtls_mpi_free(&r_exp);
	mbedtls_mpi_free(&s_exp);
	return ret;
}

ZTEST(ecc, p256_kat)
{
	uint32_t t0 = k_cycle_get_32();
	int ret = ecdsa_p256_kat();
	uint32_t us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);

	ZT_INFO("p256_kat", "curve=p256,mode=fixed_k_kat,us=%u", us);
	zassert_equal(ret, 0, "p256_kat ret=%d", ret);
}

/* ---- genkey(TRNG) → sign(固定 k) → verify 回环 ---- */
static int ecdsa_curve_once(int curve, const char *ztest_name)
{
	mbedtls_ecp_group grp;
	mbedtls_ecp_point Q;
	mbedtls_mpi d, r, s;
	unsigned char hash[MT_ECC_MAX_HASH];
	size_t hlen;
	uint32_t t0, t_sign, us_keygen = 0, us_sign = 0, us_verify = 0;
	int ret;

	hlen = (size_t)mt_unhexify(hash, MT_ECC_HASH_HEX);

	mbedtls_ecp_group_init(&grp);
	mbedtls_ecp_point_init(&Q);
	mbedtls_mpi_init(&d);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	ret = mbedtls_ecp_group_load(&grp, curve);
	if (ret == 0) {
		t0 = k_cycle_get_32();
		ret = mbedtls_ecp_gen_keypair(&grp, &d, &Q, mt_rng_trng, NULL);
		us_keygen = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	}
	if (ret == 0) {
		t_sign = k_cycle_get_32();
		ret = mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, hlen,
					 mt_rng_fixed, NULL);
		us_sign = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t_sign);
	}
	if (ret == 0) {
		t0 = k_cycle_get_32();
		ret = mbedtls_ecdsa_verify(&grp, hash, hlen, &Q, &r, &s);
		us_verify = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	}

	ZT_INFO(ztest_name, "curve=%s,mode=keygen_sign_verify,keygen_us=%u,sign_us=%u,verify_us=%u",
		(curve == MBEDTLS_ECP_DP_SECP256R1) ? "p256" :
		(curve == MBEDTLS_ECP_DP_SECP384R1) ? "p384" : "sm2",
		us_keygen, us_sign, us_verify);

	mbedtls_ecp_group_free(&grp);
	mbedtls_ecp_point_free(&Q);
	mbedtls_mpi_free(&d);
	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	return ret;
}

ZTEST(ecc, p256_keygen)
{
	int ret = ecdsa_curve_once(MBEDTLS_ECP_DP_SECP256R1, "p256_keygen");

	zassert_equal(ret, 0, "p256_keygen ret=%d", ret);
}

ZTEST(ecc, p384_keygen)
{
	int ret = ecdsa_curve_once(MBEDTLS_ECP_DP_SECP384R1, "p384_keygen");

	zassert_equal(ret, 0, "p384_keygen ret=%d", ret);
}

#if defined(CONFIG_MBEDTLS_ECP_DP_SM2_ENABLED) && \
	defined(CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT)
ZTEST(ecc, sm2_keygen)
{
	int ret = ecdsa_curve_once(MBEDTLS_ECP_DP_SM2, "sm2_keygen");

	zassert_equal(ret, 0, "sm2_keygen ret=%d", ret);
}
#endif /* SM2 keygen only with OTBN alt: software path does not generate valid SM2 keypair */
