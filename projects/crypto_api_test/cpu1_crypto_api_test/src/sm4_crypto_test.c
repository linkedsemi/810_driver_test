/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SM4 驱动测试 —— 走 Zephyr crypto 驱动 API（zephyr/crypto/crypto.h 的
 * cipher_begin_session / cipher_*_op），不是 mbedtls 的 sm4_alt。
 *
 * DUT: zephyr/drivers/crypto/crypto_linkedsemi_sm4.c
 *      由 CONFIG_CRYPTO_LINKEDSEMI_SM4 + DT 节点 &sm4 status=okay 使能。
 *
 * 覆盖模式：ECB / CBC / CTR / CCM / GCM，每项 = 标准向量加密 +
 * 解密回环（AEAD 项额外校验 tag）。
 *
 * 向量来源（全部离线复核，与被测驱动无关）：
 *   ECB / CBC / CTR : OpenSSL 3.2（openssl enc -sm4-{ecb,cbc,ctr} -nopad）
 *   GCM             : pyca/cryptography（algorithms.SM4 + modes.GCM）
 *   CCM             : RFC 3610 参考实现（以 OpenSSL SM4-ECB 为块函数）
 *   复核用 key      : FFEEDDCCBBAA99887766554433221100（**as-is，标准字节序**）
 *
 * ★ key 字节序（2026-11-03 驱动更新后）：
 *   驱动的 `sm4_linkedsemi_key_expansion()` 已去掉 sys_memcpy_swap，改为与
 *   HAL 一致的 memcpy（等价 `u8tou32`）⇒ key 按调用方字节序原样使用，
 *   与 mbedtls `sm4_alt` / HAL 路径一致。向量因此从"反序 key"版本换成
 *   "as-is key"版本（见 tests/expected/sm4_crypto_vectors.h 头注释）。
 *
 * 注：`ctx->ops.cipher_mode` 由通用封装 `cipher_begin_session()` 统一写入
 *   （`zephyr/include/zephyr/crypto/crypto.h:168`），随后 `cipher_*_op()` 里
 *   的 `__ASSERT(ctx->ops.cipher_mode == <mode>)` 才能成立 —— 驱动与测试
 *   两侧都不需要再赋值。
 */

#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ztest.h>
#include <zephyr/crypto/crypto.h>

#include "ztest_glue.h"

#if defined(CONFIG_CRYPTO_LINKEDSEMI_SM4)

#include "sm4_crypto_vectors.h"

ZTEST_SUITE(sm4_api, NULL, NULL, NULL, NULL, NULL);

#define CAPI_SM4_KEYLEN   16
#define CAPI_SM4_MSGLEN   64
#define CAPI_SM4_NONCELEN 12
#define CAPI_SM4_AADLEN   8
#define CAPI_SM4_TAGLEN   16
#define CAPI_SM4_CTRLEN   32   /* ivlen + ctrlen = 128，故 ctrlen = 32 */

#define CAPI_SM4_FLAGS \
	(CAP_RAW_KEY | CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS)

static const struct device *capi_sm4_dev = DEVICE_DT_GET(DT_NODELABEL(sm4));

/* 与 samples/crypto/sm4/src/main.c 完全一致的输入（见文件头 key 字节序说明） */
static const uint8_t capi_sm4_key[CAPI_SM4_KEYLEN] = {
	0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
	0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
};

static const uint8_t capi_sm4_pt[CAPI_SM4_MSGLEN] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
};

static const uint8_t capi_sm4_iv[CAPI_SM4_KEYLEN] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};

static const uint8_t capi_sm4_nonce[CAPI_SM4_NONCELEN] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
	0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
};

static const uint8_t capi_sm4_aad[CAPI_SM4_AADLEN] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
};

/*
 * begin_session（cipher_mode 由封装写入，见文件头）。
 * 失败时返回非 0，调用方据此跳过后续 op。
 */
static int capi_sm4_session(struct cipher_ctx *ctx, enum cipher_mode mode,
			    enum cipher_op op)
{
	return cipher_begin_session(capi_sm4_dev, ctx, CRYPTO_CIPHER_ALGO_SM4,
				    mode, op);
}

static struct cipher_ctx capi_sm4_ctx(void)
{
	struct cipher_ctx ctx = {
		.keylen = CAPI_SM4_KEYLEN,
		.key.bit_stream = capi_sm4_key,
		.flags = CAPI_SM4_FLAGS,
	};

	return ctx;
}

/* ---- ECB：样例向量 + 解密回环 ---- */
ZTEST(sm4_api, ecb)
{
	struct cipher_ctx ctx = capi_sm4_ctx();
	uint8_t enc[CAPI_SM4_MSGLEN] = { 0 };
	uint8_t dec[CAPI_SM4_MSGLEN] = { 0 };
	struct cipher_pkt pkt_enc = {
		.in_buf = (uint8_t *)capi_sm4_pt,
		.in_len = CAPI_SM4_MSGLEN,
		.out_buf = enc,
		.out_buf_max = sizeof(enc),
	};
	struct cipher_pkt pkt_dec = {
		.in_buf = enc,
		.in_len = CAPI_SM4_MSGLEN,
		.out_buf = dec,
		.out_buf_max = sizeof(dec),
	};
	uint32_t t0, us;
	int ret_be, ret_enc, ret_bd, ret_dec;
	bool kat_ok, rt_ok;

	t0 = k_cycle_get_32();

	ret_be = capi_sm4_session(&ctx, CRYPTO_CIPHER_MODE_ECB,
				  CRYPTO_CIPHER_OP_ENCRYPT);
	ret_enc = (ret_be == 0) ? cipher_block_op(&ctx, &pkt_enc) : ret_be;
	(void)cipher_free_session(capi_sm4_dev, &ctx);

	ret_bd = capi_sm4_session(&ctx, CRYPTO_CIPHER_MODE_ECB,
				  CRYPTO_CIPHER_OP_DECRYPT);
	ret_dec = (ret_bd == 0) ? cipher_block_op(&ctx, &pkt_dec) : ret_bd;
	(void)cipher_free_session(capi_sm4_dev, &ctx);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	kat_ok = (ret_enc == 0) && (pkt_enc.out_len == CAPI_SM4_MSGLEN) &&
		 (memcmp(enc, sm4_ecb_ct, sizeof(sm4_ecb_ct)) == 0);
	rt_ok = (ret_dec == 0) && (pkt_dec.out_len == CAPI_SM4_MSGLEN) &&
		(memcmp(dec, capi_sm4_pt, sizeof(capi_sm4_pt)) == 0);

	ZT_INFO("ecb",
		"algo=sm4-ecb,bytes=%u,kat=%d,roundtrip=%d,enc_ret=%d,dec_ret=%d,us=%u",
		(unsigned int)CAPI_SM4_MSGLEN, kat_ok, rt_ok, ret_enc, ret_dec, us);

	zassert_equal(ret_be, 0, "sm4-ecb begin(encrypt) ret=%d", ret_be);
	zassert_equal(ret_enc, 0, "sm4-ecb encrypt ret=%d", ret_enc);
	zassert_equal(pkt_enc.out_len, CAPI_SM4_MSGLEN,
		      "sm4-ecb encrypt out_len=%d", pkt_enc.out_len);
	zassert_true(kat_ok, "sm4-ecb ciphertext != sample vector");
	zassert_equal(ret_bd, 0, "sm4-ecb begin(decrypt) ret=%d", ret_bd);
	zassert_equal(ret_dec, 0, "sm4-ecb decrypt ret=%d", ret_dec);
	zassert_true(rt_ok, "sm4-ecb decrypt round trip != plaintext");
}

/* ---- CBC：样例向量 + 解密回环 ---- */
ZTEST(sm4_api, cbc)
{
	struct cipher_ctx ctx = capi_sm4_ctx();
	uint8_t iv_enc[CAPI_SM4_KEYLEN];
	uint8_t iv_dec[CAPI_SM4_KEYLEN];
	uint8_t enc[CAPI_SM4_MSGLEN] = { 0 };
	uint8_t dec[CAPI_SM4_MSGLEN] = { 0 };
	struct cipher_pkt pkt_enc = {
		.in_buf = (uint8_t *)capi_sm4_pt,
		.in_len = CAPI_SM4_MSGLEN,
		.out_buf = enc,
		.out_buf_max = sizeof(enc),
	};
	struct cipher_pkt pkt_dec = {
		.in_buf = enc,
		.in_len = CAPI_SM4_MSGLEN,
		.out_buf = dec,
		.out_buf_max = sizeof(dec),
	};
	uint32_t t0, us;
	int ret_be, ret_enc, ret_bd, ret_dec;
	bool kat_ok, rt_ok;

	memcpy(iv_enc, capi_sm4_iv, sizeof(iv_enc));
	memcpy(iv_dec, capi_sm4_iv, sizeof(iv_dec));

	t0 = k_cycle_get_32();

	ret_be = capi_sm4_session(&ctx, CRYPTO_CIPHER_MODE_CBC,
				  CRYPTO_CIPHER_OP_ENCRYPT);
	ret_enc = (ret_be == 0) ? cipher_cbc_op(&ctx, &pkt_enc, iv_enc) : ret_be;
	(void)cipher_free_session(capi_sm4_dev, &ctx);

	ret_bd = capi_sm4_session(&ctx, CRYPTO_CIPHER_MODE_CBC,
				  CRYPTO_CIPHER_OP_DECRYPT);
	ret_dec = (ret_bd == 0) ? cipher_cbc_op(&ctx, &pkt_dec, iv_dec) : ret_bd;
	(void)cipher_free_session(capi_sm4_dev, &ctx);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	kat_ok = (ret_enc == 0) && (pkt_enc.out_len == CAPI_SM4_MSGLEN) &&
		 (memcmp(enc, sm4_cbc_ct, sizeof(sm4_cbc_ct)) == 0);
	rt_ok = (ret_dec == 0) && (pkt_dec.out_len == CAPI_SM4_MSGLEN) &&
		(memcmp(dec, capi_sm4_pt, sizeof(capi_sm4_pt)) == 0);

	ZT_INFO("cbc",
		"algo=sm4-cbc,bytes=%u,kat=%d,roundtrip=%d,enc_ret=%d,dec_ret=%d,us=%u",
		(unsigned int)CAPI_SM4_MSGLEN, kat_ok, rt_ok, ret_enc, ret_dec, us);

	zassert_equal(ret_be, 0, "sm4-cbc begin(encrypt) ret=%d", ret_be);
	zassert_equal(ret_enc, 0, "sm4-cbc encrypt ret=%d", ret_enc);
	zassert_true(kat_ok, "sm4-cbc ciphertext != sample vector");
	zassert_equal(ret_bd, 0, "sm4-cbc begin(decrypt) ret=%d", ret_bd);
	zassert_equal(ret_dec, 0, "sm4-cbc decrypt ret=%d", ret_dec);
	zassert_true(rt_ok, "sm4-cbc decrypt round trip != plaintext");
}

/* ---- CTR：样例向量（12B nonce + 32bit counter）+ 解密回环 ---- */
ZTEST(sm4_api, ctr)
{
	struct cipher_ctx ctx = capi_sm4_ctx();
	uint8_t nonce_enc[CAPI_SM4_NONCELEN];
	uint8_t nonce_dec[CAPI_SM4_NONCELEN];
	uint8_t enc[CAPI_SM4_MSGLEN] = { 0 };
	uint8_t dec[CAPI_SM4_MSGLEN] = { 0 };
	struct cipher_pkt pkt_enc = {
		.in_buf = (uint8_t *)capi_sm4_pt,
		.in_len = CAPI_SM4_MSGLEN,
		.out_buf = enc,
		.out_buf_max = sizeof(enc),
	};
	struct cipher_pkt pkt_dec = {
		.in_buf = enc,
		.in_len = CAPI_SM4_MSGLEN,
		.out_buf = dec,
		.out_buf_max = sizeof(dec),
	};
	uint32_t t0, us;
	int ret_be, ret_enc, ret_bd, ret_dec;
	bool kat_ok, rt_ok;

	ctx.mode_params.ctr_info.ctr_len = CAPI_SM4_CTRLEN;
	memcpy(nonce_enc, capi_sm4_nonce, sizeof(nonce_enc));
	memcpy(nonce_dec, capi_sm4_nonce, sizeof(nonce_dec));

	t0 = k_cycle_get_32();

	ret_be = capi_sm4_session(&ctx, CRYPTO_CIPHER_MODE_CTR,
				  CRYPTO_CIPHER_OP_ENCRYPT);
	ret_enc = (ret_be == 0) ? cipher_ctr_op(&ctx, &pkt_enc, nonce_enc) : ret_be;
	(void)cipher_free_session(capi_sm4_dev, &ctx);

	ret_bd = capi_sm4_session(&ctx, CRYPTO_CIPHER_MODE_CTR,
				  CRYPTO_CIPHER_OP_DECRYPT);
	ret_dec = (ret_bd == 0) ? cipher_ctr_op(&ctx, &pkt_dec, nonce_dec) : ret_bd;
	(void)cipher_free_session(capi_sm4_dev, &ctx);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	kat_ok = (ret_enc == 0) && (pkt_enc.out_len == CAPI_SM4_MSGLEN) &&
		 (memcmp(enc, sm4_ctr_ct, sizeof(sm4_ctr_ct)) == 0);
	rt_ok = (ret_dec == 0) && (pkt_dec.out_len == CAPI_SM4_MSGLEN) &&
		(memcmp(dec, capi_sm4_pt, sizeof(capi_sm4_pt)) == 0);

	ZT_INFO("ctr",
		"algo=sm4-ctr,bytes=%u,ctr_len=%u,kat=%d,roundtrip=%d,enc_ret=%d,dec_ret=%d,us=%u",
		(unsigned int)CAPI_SM4_MSGLEN, (unsigned int)CAPI_SM4_CTRLEN,
		kat_ok, rt_ok, ret_enc, ret_dec, us);

	zassert_equal(ret_be, 0, "sm4-ctr begin(encrypt) ret=%d", ret_be);
	zassert_equal(ret_enc, 0, "sm4-ctr encrypt ret=%d", ret_enc);
	zassert_true(kat_ok, "sm4-ctr ciphertext != sample vector");
	zassert_equal(ret_bd, 0, "sm4-ctr begin(decrypt) ret=%d", ret_bd);
	zassert_equal(ret_dec, 0, "sm4-ctr decrypt ret=%d", ret_dec);
	zassert_true(rt_ok, "sm4-ctr decrypt round trip != plaintext");
}

/* ---- AEAD 公共流程（CCM / GCM 结构一致：nonce 12B + AAD 8B + tag 16B） ---- */
static void capi_sm4_aead_run(enum cipher_mode mode, bool ccm,
			      const uint8_t *exp_ct, size_t exp_ct_len,
			      const uint8_t *exp_tag, size_t exp_tag_len)
{
	struct cipher_ctx ctx = capi_sm4_ctx();
	uint8_t enc[CAPI_SM4_MSGLEN] = { 0 };
	uint8_t dec[CAPI_SM4_MSGLEN] = { 0 };
	uint8_t tag_enc[CAPI_SM4_TAGLEN] = { 0 };
	uint8_t tag_dec[CAPI_SM4_TAGLEN] = { 0 };
	struct cipher_pkt pkt_enc = {
		.in_buf = (uint8_t *)capi_sm4_pt,
		.in_len = CAPI_SM4_MSGLEN,
		.out_buf = enc,
		.out_buf_max = sizeof(enc),
	};
	struct cipher_pkt pkt_dec = {
		.in_buf = enc,
		.in_len = CAPI_SM4_MSGLEN,
		.out_buf = dec,
		.out_buf_max = sizeof(dec),
	};
	struct cipher_aead_pkt aead_enc = {
		.pkt = &pkt_enc,
		.ad = (uint8_t *)capi_sm4_aad,
		.ad_len = CAPI_SM4_AADLEN,
		.tag = tag_enc,
	};
	struct cipher_aead_pkt aead_dec = {
		.pkt = &pkt_dec,
		.ad = (uint8_t *)capi_sm4_aad,
		.ad_len = CAPI_SM4_AADLEN,
		.tag = tag_dec,
	};
	const char *name = ccm ? "ccm" : "gcm";
	uint32_t t0, us;
	int ret_be, ret_enc, ret_bd, ret_dec;
	bool kat_ok, tag_ok, rt_ok;

	if (ccm) {
		ctx.mode_params.ccm_info.nonce_len = CAPI_SM4_NONCELEN;
		ctx.mode_params.ccm_info.tag_len = CAPI_SM4_TAGLEN;
	} else {
		ctx.mode_params.gcm_info.nonce_len = CAPI_SM4_NONCELEN;
		ctx.mode_params.gcm_info.tag_len = CAPI_SM4_TAGLEN;
	}

	t0 = k_cycle_get_32();

	ret_be = capi_sm4_session(&ctx, mode, CRYPTO_CIPHER_OP_ENCRYPT);
	if (ret_be == 0) {
		ret_enc = ccm ? cipher_ccm_op(&ctx, &aead_enc, (uint8_t *)capi_sm4_nonce)
			      : cipher_gcm_op(&ctx, &aead_enc, (uint8_t *)capi_sm4_nonce);
	} else {
		ret_enc = ret_be;
	}
	(void)cipher_free_session(capi_sm4_dev, &ctx);

	ret_bd = capi_sm4_session(&ctx, mode, CRYPTO_CIPHER_OP_DECRYPT);
	if (ret_bd == 0) {
		/* 解密侧 tag 由调用方给出：先放入 KAT 期望 tag（错误 tag 会被驱动拒绝） */
		memcpy(tag_dec, exp_tag, MIN(exp_tag_len, sizeof(tag_dec)));
		ret_dec = ccm ? cipher_ccm_op(&ctx, &aead_dec, (uint8_t *)capi_sm4_nonce)
			      : cipher_gcm_op(&ctx, &aead_dec, (uint8_t *)capi_sm4_nonce);
	} else {
		ret_dec = ret_bd;
	}
	(void)cipher_free_session(capi_sm4_dev, &ctx);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);

	kat_ok = (ret_enc == 0) && (pkt_enc.out_len == CAPI_SM4_MSGLEN) &&
		 (memcmp(enc, exp_ct, exp_ct_len) == 0);
	tag_ok = (ret_enc == 0) && (memcmp(tag_enc, exp_tag, exp_tag_len) == 0);
	rt_ok = (ret_dec == 0) && (pkt_dec.out_len == CAPI_SM4_MSGLEN) &&
		(memcmp(dec, capi_sm4_pt, sizeof(capi_sm4_pt)) == 0);

	ZT_INFO(name,
		"algo=sm4-%s,bytes=%u,aad=%u,kat=%d,tag=%d,roundtrip=%d,enc_ret=%d,dec_ret=%d,us=%u",
		name, (unsigned int)CAPI_SM4_MSGLEN, (unsigned int)CAPI_SM4_AADLEN,
		kat_ok, tag_ok, rt_ok, ret_enc, ret_dec, us);

	zassert_equal(ret_be, 0, "sm4-%s begin(encrypt) ret=%d", name, ret_be);
	zassert_equal(ret_enc, 0, "sm4-%s encrypt ret=%d", name, ret_enc);
	zassert_equal(pkt_enc.out_len, CAPI_SM4_MSGLEN,
		      "sm4-%s encrypt out_len=%d", name, pkt_enc.out_len);
	zassert_true(kat_ok, "sm4-%s ciphertext != reference vector", name);
	zassert_true(tag_ok, "sm4-%s tag != reference vector", name);
	zassert_equal(ret_bd, 0, "sm4-%s begin(decrypt) ret=%d", name, ret_bd);
	zassert_equal(ret_dec, 0, "sm4-%s decrypt(with KAT tag) ret=%d", name, ret_dec);
	zassert_true(rt_ok, "sm4-%s decrypt round trip != plaintext", name);
}

/* ---- CCM：RFC 3610 参考实现复核的向量 + tag + 解密回环 ---- */
ZTEST(sm4_api, ccm)
{
	capi_sm4_aead_run(CRYPTO_CIPHER_MODE_CCM, true,
			  sm4_ccm_ct, sizeof(sm4_ccm_ct),
			  sm4_ccm_tag, sizeof(sm4_ccm_tag));
}

/* ---- GCM：pyca/cryptography 复核的向量 + tag + 解密回环 ---- */
ZTEST(sm4_api, gcm)
{
	capi_sm4_aead_run(CRYPTO_CIPHER_MODE_GCM, false,
			  sm4_gcm_ct, sizeof(sm4_gcm_ct),
			  sm4_gcm_tag, sizeof(sm4_gcm_tag));
}

#endif /* CONFIG_CRYPTO_LINKEDSEMI_SM4 */
