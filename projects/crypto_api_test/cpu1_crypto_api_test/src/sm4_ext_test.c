/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SM4 扩展功能用例（2026-10-29 补）：补足 crypto_api_test 原先只测
 * 「64 字节 / nonce12 / aad8 / tag16」单点参数空间的缺口。
 *
 * 覆盖维度：
 *   1. 标准 key 直通（国标 GB/T 32907 向量）—— 用于暴露驱动 key 字节序约定
 *   2. 认证失败（GCM/CCM 的 tag 篡改、AAD 篡改），每项带正确 tag 的正对照
 *   3. 明文长度：0 / 1 / 15 / 16 / 17 / 31 / 33 / 255 / 256 / 4081
 *   4. ECB 整块长度上限 4080 B（255 块）
 *   5. AAD 三态：无 AAD / 8 B / 64 B / 13 B（非整块）
 *   6. CCM tag_len（4/8/12/16）与 nonce_len（8/12/13）
 *   7. CTR 计数器位宽（16/32/64/128 bit）
 *
 * ⚠️ 已知不覆盖（会挂死，等驱动修好再补）：
 *   ECB/CBC/CTR 的块循环用 `uint8_t i`（GCM 是 `uint32_t`，不受影响），
 *   明文 ≥ 4096 B（256 块）时 i 回绕成 0 ⇒ 死循环。
 *   故本文件最大只到 4081 B（255 块 + 1 字节余量）。
 *   见 OPEN_ITEMS（待登记）/ COVERAGE.md「未覆盖」。
 *
 * ⚠️ 标准 key 用例对当前驱动是**预期 FAIL** 的：
 *   `sm4_linkedsemi_key_expansion()`（crypto_linkedsemi_sm4.c:80）用
 *   `sys_memcpy_swap()` 把整段 16 字节 key 反转，而 HAL（mbedtls 通路）用
 *   `u8tou32()` 小端拼字、不做反转。⇒ 调用方按 SM4 标准传 key 会得到与国标
 *   不同的密文。本用例就是守这条契约，FAIL 即缺陷暴露，不是用例写错。
 *   详见 tests/COVERAGE.md「SM4 key 字节序」。
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ztest.h>
#include <zephyr/crypto/crypto.h>

#include "ztest_glue.h"

#if defined(CONFIG_CRYPTO_LINKEDSEMI_SM4)

ZTEST_SUITE(sm4_ext, NULL, NULL, NULL, NULL, NULL);

#define EXT_KEYLEN   16
#define EXT_MAXLEN   4082   /* 4081 是当前驱动的安全上限（255 块 + 1 字节） */

#define EXT_FLAGS \
	(CAP_RAW_KEY | CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS)

static const struct device *ext_dev = DEVICE_DT_GET(DT_NODELABEL(sm4));

/* 与既有用例一致的样例约定 key（驱动会整段反转） */
static const uint8_t ext_key[EXT_KEYLEN] = {
	0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
	0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
};

/* SM4 国标 key / 明文 / 密文（GB/T 32907-2016 附录 A） */
static const uint8_t ext_std_key[EXT_KEYLEN] = {
	0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
	0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
};
static const uint8_t ext_std_ct[EXT_KEYLEN] = {
	0x68, 0x1E, 0xDF, 0x34, 0xD2, 0x06, 0x96, 0x5E,
	0x86, 0xB3, 0xE9, 0x4F, 0x53, 0x6E, 0x42, 0x46,
};

static const uint8_t ext_nonce12[12] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
	0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
};

static const uint8_t ext_aad8[8] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
};

/* 静态缓冲：避免压 ztest 线程栈 */
static uint8_t ext_in[EXT_MAXLEN];
static uint8_t ext_out[EXT_MAXLEN];
static uint8_t ext_dec[EXT_MAXLEN];
static uint8_t ext_aad[64];
static uint8_t ext_tag[16];
static uint8_t ext_tag2[16];
static uint8_t ext_nonce[13];
static uint8_t ext_iv[16];

static struct cipher_ctx ext_ctx(void)
{
	struct cipher_ctx ctx = {
		.keylen = EXT_KEYLEN,
		.key.bit_stream = ext_key,
		.flags = EXT_FLAGS,
	};

	return ctx;
}

/* ECB / CBC / CTR 单次操作；返回驱动返回值，out_len 经指针回传 */
static int ext_op(enum cipher_mode mode, enum cipher_op op,
		  uint8_t *in, uint32_t len, uint8_t *out, uint32_t out_max,
		  uint8_t *iv, uint16_t ctr_len, uint32_t *out_len)
{
	struct cipher_ctx ctx = ext_ctx();
	struct cipher_pkt pkt = {
		.in_buf = in,
		.in_len = len,
		.out_buf = out,
		.out_buf_max = out_max,
	};
	int ret;

	if (mode == CRYPTO_CIPHER_MODE_CTR) {
		ctx.mode_params.ctr_info.ctr_len = ctr_len;
	}

	ret = cipher_begin_session(ext_dev, &ctx, CRYPTO_CIPHER_ALGO_SM4, mode, op);
	if (ret != 0) {
		return ret;
	}

	switch (mode) {
	case CRYPTO_CIPHER_MODE_ECB:
		ret = cipher_block_op(&ctx, &pkt);
		break;
	case CRYPTO_CIPHER_MODE_CBC:
		ret = cipher_cbc_op(&ctx, &pkt, iv);
		break;
	case CRYPTO_CIPHER_MODE_CTR:
		ret = cipher_ctr_op(&ctx, &pkt, iv);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	(void)cipher_free_session(ext_dev, &ctx);
	*out_len = pkt.out_len;
	return ret;
}

/* CCM / GCM 单次操作 */
static int ext_aead_op(enum cipher_mode mode, enum cipher_op op,
		       uint8_t *in, uint32_t len, uint8_t *out, uint32_t out_max,
		       const uint8_t *aad, uint32_t aad_len, uint8_t *tag,
		       uint16_t nonce_len, uint16_t tag_len, const uint8_t *nonce,
		       uint32_t *out_len)
{
	struct cipher_ctx ctx = ext_ctx();
	struct cipher_pkt pkt = {
		.in_buf = in,
		.in_len = len,
		.out_buf = out,
		.out_buf_max = out_max,
	};
	struct cipher_aead_pkt apkt = {
		.pkt = &pkt,
		.ad = (uint8_t *)aad,
		.ad_len = aad_len,
		.tag = tag,
	};
	int ret;

	if (mode == CRYPTO_CIPHER_MODE_CCM) {
		ctx.mode_params.ccm_info.nonce_len = nonce_len;
		ctx.mode_params.ccm_info.tag_len = tag_len;
	} else {
		ctx.mode_params.gcm_info.nonce_len = nonce_len;
		ctx.mode_params.gcm_info.tag_len = tag_len;
	}

	ret = cipher_begin_session(ext_dev, &ctx, CRYPTO_CIPHER_ALGO_SM4, mode, op);
	if (ret != 0) {
		return ret;
	}

	ret = (mode == CRYPTO_CIPHER_MODE_CCM)
		      ? cipher_ccm_op(&ctx, &apkt, (uint8_t *)nonce)
		      : cipher_gcm_op(&ctx, &apkt, (uint8_t *)nonce);

	(void)cipher_free_session(ext_dev, &ctx);
	*out_len = pkt.out_len;
	return ret;
}

static void ext_fill(uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		ext_in[i] = (uint8_t)i;
	}
	memset(ext_out, 0, EXT_MAXLEN);
	memset(ext_dec, 0, EXT_MAXLEN);
}

/* ======================= 1. 标准 key 直通 ======================= */

/* 国标 GB/T 32907 向量：key=pt=0123...3210 → 681EDF34D206965E86B3E94F536E4246。
 * 对当前驱动预期 FAIL（key 被整段反转），用于守住 SM4 标准语义。 */
ZTEST(sm4_ext, sm4_stdkey_ecb)
{
	struct cipher_ctx ctx = {
		.keylen = EXT_KEYLEN,
		.key.bit_stream = ext_std_key,
		.flags = EXT_FLAGS,
	};
	uint8_t out[16] = { 0 };
	struct cipher_pkt pkt = {
		.in_buf = (uint8_t *)ext_std_key,
		.in_len = EXT_KEYLEN,
		.out_buf = out,
		.out_buf_max = sizeof(out),
	};
	int ret;

	ret = cipher_begin_session(ext_dev, &ctx, CRYPTO_CIPHER_ALGO_SM4,
				   CRYPTO_CIPHER_MODE_ECB, CRYPTO_CIPHER_OP_ENCRYPT);
	if (ret == 0) {
		ret = cipher_block_op(&ctx, &pkt);
	}
	(void)cipher_free_session(ext_dev, &ctx);

	ZT_INFO("sm4_stdkey_ecb",
		"ret=%d,ct=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
		ret, out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
		out[8], out[9], out[10], out[11], out[12], out[13], out[14], out[15]);

	zassert_equal(ret, 0, "stdkey ecb ret=%d", ret);
	zassert_equal(memcmp(out, ext_std_ct, sizeof(ext_std_ct)), 0,
		      "SM4 标准 key 不符国标向量（驱动 key 字节序非标准，见 COVERAGE / O-54）");
}

/* ======================= 2. 认证失败 ======================= */

static void ext_auth_fail(enum cipher_mode mode, bool tamper_tag, bool tamper_aad,
			  const char *tname)
{
	const char *name = (mode == CRYPTO_CIPHER_MODE_CCM) ? "ccm" : "gcm";
	const uint32_t len = 64;
	const uint32_t aad_len = sizeof(ext_aad8);
	uint32_t out_len = 0;
	int ret_ok, ret_bad;

	memcpy(ext_aad, ext_aad8, aad_len);
	ext_fill(len);

	/* 正对照：**原始** tag/AAD 必须解密成功，否则下面的"失败"没有判别力 */
	memset(ext_tag, 0, sizeof(ext_tag));
	ret_ok = ext_aead_op(mode, CRYPTO_CIPHER_OP_ENCRYPT, ext_in, len,
			     ext_out, EXT_MAXLEN, ext_aad, aad_len, ext_tag,
			     sizeof(ext_nonce12), 16, ext_nonce12, &out_len);
	if (ret_ok == 0) {
		ret_ok = ext_aead_op(mode, CRYPTO_CIPHER_OP_DECRYPT, ext_out, len,
				     ext_dec, EXT_MAXLEN, ext_aad, aad_len, ext_tag,
				     sizeof(ext_nonce12), 16, ext_nonce12, &out_len);
	}

	/* 正对照失败时直接报错，避免"因为链路坏了所以篡改也失败"的假绿 */
	zassert_equal(ret_ok, 0, "%s positive control failed ret=%d", name, ret_ok);

	/* 篡改只作用于负路径这次调用，不能污染上面的正对照 */
	memcpy(ext_tag2, ext_tag, sizeof(ext_tag));
	if (tamper_tag) {
		ext_tag2[0] ^= 0x01;
	}
	if (tamper_aad) {
		ext_aad[0] ^= 0x80;
	}

	ret_bad = ext_aead_op(mode, CRYPTO_CIPHER_OP_DECRYPT, ext_out, len,
			      ext_dec, EXT_MAXLEN, ext_aad, aad_len,
			      tamper_tag ? ext_tag2 : ext_tag,
			      sizeof(ext_nonce12), 16, ext_nonce12, &out_len);

	/* INFO 名必须等于用例名，否则归档报告按名字取不到证据（会显示 "-"） */
	ZT_INFO(tname, "mode=%s,tamper=%s,control_ret=0,bad_ret=%d", name,
		tamper_tag ? "tag" : "aad", ret_bad);

	zassert_not_equal(ret_bad, 0,
			  "%s %s tamper accepted (ret=0) —— 认证失败未被拒绝", name,
			  tamper_tag ? "tag" : "aad");
}

ZTEST(sm4_ext, sm4_gcm_tag_tamper)
{
	ext_auth_fail(CRYPTO_CIPHER_MODE_GCM, true, false, "sm4_gcm_tag_tamper");
}

ZTEST(sm4_ext, sm4_gcm_aad_tamper)
{
	ext_auth_fail(CRYPTO_CIPHER_MODE_GCM, false, true, "sm4_gcm_aad_tamper");
}

ZTEST(sm4_ext, sm4_ccm_tag_tamper)
{
	ext_auth_fail(CRYPTO_CIPHER_MODE_CCM, true, false, "sm4_ccm_tag_tamper");
}

ZTEST(sm4_ext, sm4_ccm_aad_tamper)
{
	ext_auth_fail(CRYPTO_CIPHER_MODE_CCM, false, true, "sm4_ccm_aad_tamper");
}

/* ======================= 3. 长度边界 ======================= */

/* CTR 支持任意长度：0/1/15/16/17/31/33/255/256/4081 全部往返 */
ZTEST(sm4_ext, sm4_ctr_len_sweep)
{
	static const uint32_t lens[] = { 0, 1, 15, 16, 17, 31, 33, 255, 256, 4081 };

	for (size_t k = 0; k < ARRAY_SIZE(lens); k++) {
		uint32_t len = lens[k];
		uint32_t out_len = 0;
		int ret_e, ret_d;

		ext_fill(len);
		memcpy(ext_iv, ext_nonce12, 12);
		memset(ext_iv + 12, 0, 4);

		ret_e = ext_op(CRYPTO_CIPHER_MODE_CTR, CRYPTO_CIPHER_OP_ENCRYPT,
			       ext_in, len, ext_out, EXT_MAXLEN, ext_iv, 32, &out_len);
		if (ret_e == 0) {
			memcpy(ext_iv, ext_nonce12, 12);
			memset(ext_iv + 12, 0, 4);
			ret_d = ext_op(CRYPTO_CIPHER_MODE_CTR,
				       CRYPTO_CIPHER_OP_DECRYPT, ext_out, len,
				       ext_dec, EXT_MAXLEN, ext_iv, 32, &out_len);
		} else {
			ret_d = ret_e;
		}

		ZT_INFO("sm4_ctr_len_sweep", "len=%u,enc_ret=%d,dec_ret=%d,out_len=%u",
			len, ret_e, ret_d, out_len);

		zassert_equal(ret_e, 0, "ctr len=%u encrypt ret=%d", len, ret_e);
		zassert_equal(ret_d, 0, "ctr len=%u decrypt ret=%d", len, ret_d);
		zassert_equal(out_len, len, "ctr len=%u out_len=%u", len, out_len);
		zassert_equal(memcmp(ext_dec, ext_in, len), 0,
			      "ctr len=%u roundtrip mismatch", len);
	}
}

/* ECB 整块长度：16 / 32 / 4080（255 块，当前驱动安全上限） */
ZTEST(sm4_ext, sm4_ecb_len_sweep)
{
	static const uint32_t lens[] = { 16, 32, 4080 };

	for (size_t k = 0; k < ARRAY_SIZE(lens); k++) {
		uint32_t len = lens[k];
		uint32_t out_len = 0;
		int ret_e, ret_d;

		ext_fill(len);
		ret_e = ext_op(CRYPTO_CIPHER_MODE_ECB, CRYPTO_CIPHER_OP_ENCRYPT,
			       ext_in, len, ext_out, EXT_MAXLEN, NULL, 0, &out_len);
		if (ret_e == 0) {
			ret_d = ext_op(CRYPTO_CIPHER_MODE_ECB,
				       CRYPTO_CIPHER_OP_DECRYPT, ext_out, len,
				       ext_dec, EXT_MAXLEN, NULL, 0, &out_len);
		} else {
			ret_d = ret_e;
		}

		ZT_INFO("sm4_ecb_len_sweep", "len=%u,enc_ret=%d,dec_ret=%d,out_len=%u",
			len, ret_e, ret_d, out_len);

		zassert_equal(ret_e, 0, "ecb len=%u encrypt ret=%d", len, ret_e);
		zassert_equal(ret_d, 0, "ecb len=%u decrypt ret=%d", len, ret_d);
		zassert_equal(out_len, len, "ecb len=%u out_len=%u", len, out_len);
		zassert_equal(memcmp(ext_dec, ext_in, len), 0,
			      "ecb len=%u roundtrip mismatch", len);
	}
}

/* 0 字节：应为 no-op 成功。当前驱动在循环不执行时返回未初始化的 ret，
 * 本用例就是守这个语义（预期可能 FAIL，FAIL 即暴露缺陷）。 */
ZTEST(sm4_ext, sm4_ecb_zero_len)
{
	uint8_t dummy = 0;
	uint32_t out_len = 0;
	int ret;

	ret = ext_op(CRYPTO_CIPHER_MODE_ECB, CRYPTO_CIPHER_OP_ENCRYPT, &dummy, 0,
		     ext_out, EXT_MAXLEN, NULL, 0, &out_len);

	ZT_INFO("sm4_ecb_zero_len", "ret=%d,out_len=%u", ret, out_len);

	zassert_equal(ret, 0, "ecb zero-length ret=%d（非 0 说明返回了未初始化值）", ret);
	zassert_equal(out_len, 0, "ecb zero-length out_len=%u", out_len);
}

/* ======================= 4. AAD 三态 ======================= */

static void ext_aad_case(enum cipher_mode mode, uint32_t aad_len, const char *tname)
{
	const char *name = (mode == CRYPTO_CIPHER_MODE_CCM) ? "ccm" : "gcm";
	const uint32_t len = 64;
	uint32_t out_len = 0;
	int ret;
	bool rt;

	for (uint32_t i = 0; i < aad_len; i++) {
		ext_aad[i] = (uint8_t)(0xA0 + i);
	}

	ext_fill(len);
	memset(ext_tag, 0, sizeof(ext_tag));
	ret = ext_aead_op(mode, CRYPTO_CIPHER_OP_ENCRYPT, ext_in, len, ext_out,
			  EXT_MAXLEN, aad_len ? ext_aad : NULL, aad_len, ext_tag,
			  sizeof(ext_nonce12), 16, (uint8_t *)ext_nonce12, &out_len);
	if (ret == 0) {
		ret = ext_aead_op(mode, CRYPTO_CIPHER_OP_DECRYPT, ext_out, len,
				  ext_dec, EXT_MAXLEN, aad_len ? ext_aad : NULL,
				  aad_len, ext_tag, sizeof(ext_nonce12), 16,
				  (uint8_t *)ext_nonce12, &out_len);
	}
	rt = (ret == 0) && (out_len == len) && (memcmp(ext_dec, ext_in, len) == 0);

	ZT_INFO(tname, "mode=%s,aad_len=%u,ret=%d,roundtrip=%d", name, aad_len,
		ret, rt);

	zassert_equal(ret, 0, "%s aad_len=%u ret=%d", name, aad_len, ret);
	zassert_true(rt, "%s aad_len=%u roundtrip failed", name, aad_len);
}

ZTEST(sm4_ext, sm4_gcm_aad_none)
{
	ext_aad_case(CRYPTO_CIPHER_MODE_GCM, 0, "sm4_gcm_aad_none");
}

ZTEST(sm4_ext, sm4_gcm_aad_large)
{
	ext_aad_case(CRYPTO_CIPHER_MODE_GCM, 64, "sm4_gcm_aad_large");
}

ZTEST(sm4_ext, sm4_ccm_aad_none)
{
	ext_aad_case(CRYPTO_CIPHER_MODE_CCM, 0, "sm4_ccm_aad_none");
}

ZTEST(sm4_ext, sm4_ccm_aad_large)
{
	ext_aad_case(CRYPTO_CIPHER_MODE_CCM, 64, "sm4_ccm_aad_large");
}

ZTEST(sm4_ext, sm4_ccm_aad_unaligned)
{
	ext_aad_case(CRYPTO_CIPHER_MODE_CCM, 13, "sm4_ccm_aad_unaligned");
}

/* ======================= 5. tag / nonce 变体 ======================= */

ZTEST(sm4_ext, sm4_ccm_taglen_sweep)
{
	static const uint16_t tags[] = { 4, 8, 12, 16 };
	const uint32_t len = 64;
	uint32_t out_len = 0;

	for (size_t k = 0; k < ARRAY_SIZE(tags); k++) {
		uint16_t tag_len = tags[k];
		int ret;

		ext_fill(len);
		memset(ext_tag, 0, sizeof(ext_tag));
		ret = ext_aead_op(CRYPTO_CIPHER_MODE_CCM, CRYPTO_CIPHER_OP_ENCRYPT,
				  ext_in, len, ext_out, EXT_MAXLEN, ext_aad8,
				  sizeof(ext_aad8), ext_tag, sizeof(ext_nonce12),
				  tag_len, (uint8_t *)ext_nonce12, &out_len);
		if (ret == 0) {
			ret = ext_aead_op(CRYPTO_CIPHER_MODE_CCM,
					  CRYPTO_CIPHER_OP_DECRYPT, ext_out, len,
					  ext_dec, EXT_MAXLEN, ext_aad8,
					  sizeof(ext_aad8), ext_tag,
					  sizeof(ext_nonce12), tag_len,
					  (uint8_t *)ext_nonce12, &out_len);
		}

		ZT_INFO("sm4_ccm_taglen_sweep", "tag_len=%u,ret=%d,roundtrip=%d",
			tag_len, ret, (ret == 0) && (memcmp(ext_dec, ext_in, len) == 0));

		zassert_equal(ret, 0, "ccm tag_len=%u ret=%d", tag_len, ret);
		zassert_equal(memcmp(ext_dec, ext_in, len), 0,
			      "ccm tag_len=%u roundtrip mismatch", tag_len);
	}
}

ZTEST(sm4_ext, sm4_ccm_noncelen_sweep)
{
	static const uint16_t nonces[] = { 8, 12, 13 };
	const uint32_t len = 64;
	uint32_t out_len = 0;

	for (size_t k = 0; k < ARRAY_SIZE(nonces); k++) {
		uint16_t nonce_len = nonces[k];
		int ret;

		for (uint16_t i = 0; i < nonce_len; i++) {
			ext_nonce[i] = (uint8_t)(0x10 + i);
		}

		ext_fill(len);
		memset(ext_tag, 0, sizeof(ext_tag));
		ret = ext_aead_op(CRYPTO_CIPHER_MODE_CCM, CRYPTO_CIPHER_OP_ENCRYPT,
				  ext_in, len, ext_out, EXT_MAXLEN, ext_aad8,
				  sizeof(ext_aad8), ext_tag, nonce_len, 16,
				  ext_nonce, &out_len);
		if (ret == 0) {
			ret = ext_aead_op(CRYPTO_CIPHER_MODE_CCM,
					  CRYPTO_CIPHER_OP_DECRYPT, ext_out, len,
					  ext_dec, EXT_MAXLEN, ext_aad8,
					  sizeof(ext_aad8), ext_tag, nonce_len, 16,
					  ext_nonce, &out_len);
		}

		ZT_INFO("sm4_ccm_noncelen_sweep", "nonce_len=%u,ret=%d,roundtrip=%d",
			nonce_len, ret, (ret == 0) && (memcmp(ext_dec, ext_in, len) == 0));

		zassert_equal(ret, 0, "ccm nonce_len=%u ret=%d", nonce_len, ret);
		zassert_equal(memcmp(ext_dec, ext_in, len), 0,
			      "ccm nonce_len=%u roundtrip mismatch", nonce_len);
	}
}

/* ======================= 6. CTR 计数器位宽 ======================= */

ZTEST(sm4_ext, sm4_ctr_ctrlen_sweep)
{
	static const uint16_t ctr_lens[] = { 16, 32, 64, 128 };
	const uint32_t len = 64;
	uint32_t out_len = 0;

	for (size_t k = 0; k < ARRAY_SIZE(ctr_lens); k++) {
		uint16_t ctr_len = ctr_lens[k];
		int ret_e, ret_d;

		ext_fill(len);
		memset(ext_iv, 0x5A, sizeof(ext_iv));   /* nonce/counter 区都置非 0 */
		ret_e = ext_op(CRYPTO_CIPHER_MODE_CTR, CRYPTO_CIPHER_OP_ENCRYPT,
			       ext_in, len, ext_out, EXT_MAXLEN, ext_iv, ctr_len,
			       &out_len);
		if (ret_e == 0) {
			memset(ext_iv, 0x5A, sizeof(ext_iv));
			ret_d = ext_op(CRYPTO_CIPHER_MODE_CTR,
				       CRYPTO_CIPHER_OP_DECRYPT, ext_out, len,
				       ext_dec, EXT_MAXLEN, ext_iv, ctr_len,
				       &out_len);
		} else {
			ret_d = ret_e;
		}

		ZT_INFO("sm4_ctr_ctrlen_sweep",
			"ctr_len=%u,enc_ret=%d,dec_ret=%d,roundtrip=%d", ctr_len,
			ret_e, ret_d, (ret_d == 0) && (memcmp(ext_dec, ext_in, len) == 0));

		zassert_equal(ret_e, 0, "ctr ctr_len=%u encrypt ret=%d", ctr_len, ret_e);
		zassert_equal(ret_d, 0, "ctr ctr_len=%u decrypt ret=%d", ctr_len, ret_d);
		zassert_equal(memcmp(ext_dec, ext_in, len), 0,
			      "ctr ctr_len=%u roundtrip mismatch", ctr_len);
	}
}

#endif /* CONFIG_CRYPTO_LINKEDSEMI_SM4 */
