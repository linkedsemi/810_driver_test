/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * rsa 套件：RSA-2048/3072/4096 KAT。**路由随 case 变化**：case_otbn_rsa /
 * case_otbn_all / case_all_on_max / case_otbn_sha256_hw_sm3 / case_thread_mix
 * 开了 `CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT`（走 OTBN hook）；其余 case
 * 走纯软件。注意：hook 失败会**静默回落软件**，KAT 相等不证明 OTBN 被使用 ⇒
 * 由 `rsa2048_otbn_hit`（见文末）显式断言 OTBN 命中。
 *   - rsa2048_modexp：导入预置密钥对 → public modexp（encrypt）+ private
 *     modexp（decrypt）+ PKCS#1 v1.5 sign/verify + OpenSSL 黄金密文/签名 KAT
 *   - rsa2048_keygen：F4 / 2048 一档 keygen → check_privkey → sign/verify
 *     （keygen 用 TRNG 真随机源）
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
static int rsa_modexp_kat(const char *ztest_name, int bits,
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
		ZT_INFO(ztest_name, "algo=rsa%d,stage=dec,us=%u", bits, us);
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
		ZT_INFO(ztest_name, "algo=rsa%d,stage=signverify,us=%u",
			bits, us);
	}

	mbedtls_rsa_free(&rsa);
	return ret;
}

/* ---- RSA-3072 modexp KAT ---- */
ZTEST(rsa, rsa3072_modexp)
{
	int ret = rsa_modexp_kat("rsa3072_modexp", 3072,
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
	int ret = rsa_modexp_kat("rsa4096_modexp", 4096,
				 rsa4096_n, sizeof(rsa4096_n),
				 rsa4096_d, sizeof(rsa4096_d),
				 rsa4096_e, sizeof(rsa4096_e),
				 rsa4096_ciphertext, sizeof(rsa4096_ciphertext),
				 rsa4096_signature, sizeof(rsa4096_signature));

	zassert_equal(ret, 0, "rsa4096_modexp ret=%d", ret);
}

/* ==================================================================
 * P1-2（2026-09-18）：OTBN RSA 通路命中的可观测性断言
 *
 * 背景（测试有效性审计 A 组 P1-2）：mbedtls 的 OTBN hook（rsa_alt.c）返回非 0
 * 时，rsa.c 会**静默回落软件 modexp**——`mbedtls_rsa_public` 的 `rsa.c:1287-1293`
 * 与 `mbedtls_rsa_private` 的 `:1518-1524` 都把 hook 的返回值**改写为 0** 后继续
 * 走软件路径，无日志、无错误上抛。因此 rsa2048/3072/4096_modexp 的 KAT 逐字节
 * 相等**不能证明 OTBN 被使用**（sw_baseline 与 case_otbn_rsa 的结果逐字节相同）。
 *
 * 本项给出两条可判 FAIL 的观测：
 *   ① 直调非 static 的 `mbedtls_rsa_public_otbn()`（rsa.c:52 声明，zephyr.map 已
 *      确认链接进固件）——返回 0 ⟺ OTBN 硬件完成了 2048-bit modexp 并读回结果
 *      （rsa_alt.c:188-202 全链路成功才 return 0；尺寸不符/会话失败/执行失败
 *       都返回非 0）。
 *   ② `ls_otbn_imem_firmware_get()` == `OTBN_FIRMWARE_RSA_MODEXP`（8）——证明
 *      rsa.c 的 hook 集成路径确实被走到（若回落到软件，IMEM 里仍是别的固件）。
 *   ③ 公/私 modexp 回环（public → private 还原输入）——抓"返回 0 但结果错"。
 *
 * 仅在开启 RSA OTBN ALT 的 case 下编入（`MBEDTLS_RSA_OTBN_HOOK`，由
 * `zephyr/modules/mbedtls/configs/config-linkedsemi-alt.h:22-24` 从
 * `CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT` 派生）；纯软件 case 不编入。
 * ================================================================== */
#if defined(MBEDTLS_RSA_OTBN_HOOK)

#include <ls_otbn_config.h>   /* ls_otbn_imem_firmware_get / OTBN_FIRMWARE_RSA_MODEXP */

/* rsa.c:52-59 的非 static 全局符号（本文件不改 modules/**，仅在测试侧补原型） */
extern int mbedtls_rsa_public_otbn(mbedtls_rsa_context *ctx,
				   const unsigned char *input,
				   unsigned char *output);

ZTEST(rsa, rsa2048_otbn_hit)
{
	mbedtls_rsa_context rsa;
	unsigned char in[256];
	unsigned char out_otbn[256];
	unsigned char back[256];
	size_t back_len = 0;
	int ret;

	ret = rsa_import_test_key(&rsa);
	zassert_equal(ret, 0, "import test key ret=%d", ret);
	if (ret != 0) {
		return;
	}

	for (int i = 0; i < (int)sizeof(in); i++) {
		in[i] = (unsigned char)(i + 1);   /* 确定性输入，首字节非 0 ⇒ < n */
	}
	memset(out_otbn, 0, sizeof(out_otbn));

	/* ① 直调 OTBN hook：返回 0 才算"OTBN 命中" */
	ret = mbedtls_rsa_public_otbn(&rsa, in, out_otbn);
	ZT_INFO("rsa2048_otbn_hit", "hook_ret=%d", ret);
	zassert_equal(ret, 0,
		"mbedtls_rsa_public_otbn ret=%d：OTBN RSA 通路不可用（尺寸/会话/执行失败），"
		"本 case 的 RSA KAT 实为软件回落", ret);

	/* ② 集成证据：IMEM 固件已切到 RSA_MODEXP（rsa.c 未走软件回落）。
	 *    必须紧跟 RSA 调用读取——任何后续 OTBN hash/ECC 操作都会改写该状态。 */
	otbn_firmware_t fw = ls_otbn_imem_firmware_get();

	ZT_INFO("rsa2048_otbn_hit", "imem_firmware=%u", (unsigned)fw);
	zassert_equal((unsigned)fw, (unsigned)OTBN_FIRMWARE_RSA_MODEXP,
		"IMEM firmware=%u != OTBN_FIRMWARE_RSA_MODEXP(%u)：rsa.c 可能走了软件回落",
		(unsigned)fw, (unsigned)OTBN_FIRMWARE_RSA_MODEXP);

	/* ③ 正确性回环：public 后再 private 必须还原输入（抓"返回 0 但结果错"） */
	ret = mbedtls_rsa_private(&rsa, mt_rng_lcg, NULL, out_otbn, back);
	zassert_equal(ret, 0, "mbedtls_rsa_private ret=%d", ret);
	if (ret == 0) {
		back_len = sizeof(in);
		mt_assert_mem_eq(back, in, back_len, "otbn public->private roundtrip");
	}

	mbedtls_rsa_free(&rsa);
}
#endif /* MBEDTLS_RSA_OTBN_HOOK */
