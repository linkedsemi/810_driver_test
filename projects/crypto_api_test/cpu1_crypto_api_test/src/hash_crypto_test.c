/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hash 驱动测试 —— 走 Zephyr crypto 驱动 API（hash_begin_session /
 * hash_update / hash_compute），不是 mbedtls 的 sha*_alt。
 *
 * DUT:
 *   zephyr/drivers/crypto/crypto_linkedsemi_sha256.c  （SHA224 / SHA256 / SM3）
 *   zephyr/drivers/crypto/crypto_linkedsemi_sha512.c  （SHA384 / SHA512）
 * 使能：CONFIG_CRYPTO_LINKEDSEMI_SHA256 / _SHA512 + DT 节点 &sha256 / &sha512
 * status=okay。
 *
 * 覆盖：
 *   - 5 个算法各一条短报文 KAT（"abc"，单次 hash_compute）
 *   - 5 个算法各一条分段喂入（hash_update ×3 + hash_compute）等价性
 *   - SHA256 / SHA512 各一条 padding 边界（长度跨块头/块尾：0/55/56/64 与
 *     0/111/112/128），覆盖驱动 sha_byte_update + 填充分支
 *
 * 期望值全部由 Python hashlib（sha224/256/384/512）与 OpenSSL
 * （openssl dgst -sm3）离线生成，见 tests/expected/hash_crypto_vectors.h。
 */

#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ztest.h>
#include <zephyr/crypto/crypto.h>

#include "ztest_glue.h"

#if defined(CONFIG_CRYPTO_LINKEDSEMI_SHA256) || \
	defined(CONFIG_CRYPTO_LINKEDSEMI_SHA512)

#include "hash_crypto_vectors.h"

ZTEST_SUITE(hash_api, NULL, NULL, NULL, NULL, NULL);

#define CAPI_HASH_FLAGS (CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS)
#define CAPI_HASH_MAX_DIGEST 64
#define CAPI_PAD_BUF_LEN 128

#if defined(CONFIG_CRYPTO_LINKEDSEMI_SHA256)
static const struct device *capi_hash_sha256_dev =
	DEVICE_DT_GET(DT_NODELABEL(sha256));
#endif

#if defined(CONFIG_CRYPTO_LINKEDSEMI_SHA512)
static const struct device *capi_hash_sha512_dev =
	DEVICE_DT_GET(DT_NODELABEL(sha512));
#endif

/* 输入报文（NIST FIPS 180-2 示例报文） */
static const uint8_t capi_msg_abc[3] = { 'a', 'b', 'c' };

static const uint8_t capi_msg_448[56] = {
	'a', 'b', 'c', 'd', 'b', 'c', 'd', 'e', 'c', 'd', 'e', 'f', 'd', 'e',
	'f', 'g', 'e', 'f', 'g', 'h', 'f', 'g', 'h', 'i', 'g', 'h', 'i', 'j',
	'h', 'i', 'j', 'k', 'i', 'j', 'k', 'l', 'j', 'k', 'l', 'm', 'k', 'l',
	'm', 'n', 'l', 'm', 'n', 'o', 'm', 'n', 'o', 'p', 'n', 'o', 'p', 'q',
};

static const uint8_t capi_msg_896[112] = {
	'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'b', 'c', 'd', 'e', 'f', 'g',
	'h', 'i', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'd', 'e', 'f', 'g',
	'h', 'i', 'j', 'k', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'f', 'g',
	'h', 'i', 'j', 'k', 'l', 'm', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u',
};

/* padding 边界用：内容 = 下标 mod 256（与生成期望值时 byte(i) 的构造一致） */
static uint8_t capi_pad_buf[CAPI_PAD_BUF_LEN];

static void capi_pad_buf_init(void)
{
	for (size_t i = 0; i < sizeof(capi_pad_buf); i++) {
		capi_pad_buf[i] = (uint8_t)i;
	}
}

/* 单次会话：begin → compute → free */
static int capi_hash_once(const struct device *dev, enum hash_algo algo,
			  const uint8_t *msg, size_t len, uint8_t *out)
{
	struct hash_ctx ctx = { .flags = CAPI_HASH_FLAGS };
	struct hash_pkt pkt = {
		.in_buf = (uint8_t *)msg,
		.in_len = len,
		.out_buf = out,
	};
	int ret = hash_begin_session(dev, &ctx, algo);

	if (ret != 0) {
		return ret;
	}
	ret = hash_compute(&ctx, &pkt);
	(void)hash_free_session(dev, &ctx);
	return ret;
}

/* 分段会话：begin → update×N → compute → free */
static int capi_hash_split(const struct device *dev, enum hash_algo algo,
			   const uint8_t *msg, size_t len,
			   const size_t *cuts, size_t ncuts, uint8_t *out)
{
	struct hash_ctx ctx = { .flags = CAPI_HASH_FLAGS };
	struct hash_pkt pkt = { 0 };
	size_t off = 0;
	int ret = hash_begin_session(dev, &ctx, algo);

	if (ret != 0) {
		return ret;
	}

	for (size_t i = 0; i < ncuts && ret == 0; i++) {
		pkt.in_buf = (uint8_t *)msg + off;
		pkt.in_len = cuts[i];
		ret = hash_update(&ctx, &pkt);
		off += cuts[i];
	}

	if (ret == 0) {
		pkt.in_buf = (uint8_t *)msg + off;
		pkt.in_len = len - off;
		pkt.out_buf = out;
		ret = hash_compute(&ctx, &pkt);
	}

	(void)hash_free_session(dev, &ctx);
	return ret;
}

/* 单算法 KAT：短报文单次 calc */
static void capi_hash_kat(const struct device *dev, enum hash_algo algo,
			  const char *alg_name, const char *info_name,
			  const uint8_t *exp, size_t exp_len)
{
	uint8_t out[CAPI_HASH_MAX_DIGEST] = { 0 };
	uint32_t t0, us;
	int ret;
	bool ok;

	t0 = k_cycle_get_32();
	ret = capi_hash_once(dev, algo, capi_msg_abc, sizeof(capi_msg_abc), out);
	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);

	ok = (ret == 0) && (memcmp(out, exp, exp_len) == 0);

	ZT_INFO(info_name, "algo=%s,bytes=%u,kat=%d,ret=%d,us=%u",
		alg_name, (unsigned int)sizeof(capi_msg_abc), ok, ret, us);

	zassert_equal(ret, 0, "%s KAT ret=%d", alg_name, ret);
	zassert_mem_equal(out, exp, exp_len, "%s digest != reference vector", alg_name);
}

/* 单算法分段等价：hash_update 分三段喂入，结果须与单次 calc 一致 */
static void capi_hash_split_case(const struct device *dev, enum hash_algo algo,
				 const char *alg_name, const char *info_name,
				 const uint8_t *msg, size_t len,
				 const size_t *cuts, size_t ncuts,
				 const uint8_t *exp, size_t exp_len)
{
	uint8_t out[CAPI_HASH_MAX_DIGEST] = { 0 };
	uint32_t t0, us;
	int ret;
	bool ok;

	t0 = k_cycle_get_32();
	ret = capi_hash_split(dev, algo, msg, len, cuts, ncuts, out);
	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);

	ok = (ret == 0) && (memcmp(out, exp, exp_len) == 0);

	ZT_INFO(info_name, "algo=%s,bytes=%u,segments=%u,split_eq_kat=%d,ret=%d,us=%u",
		alg_name, (unsigned int)len, (unsigned int)(ncuts + 1), ok, ret, us);

	zassert_equal(ret, 0, "%s split ret=%d", alg_name, ret);
	zassert_mem_equal(out, exp, exp_len,
			  "%s digest with segmented hash_update != reference vector",
			  alg_name);
}

#if defined(CONFIG_CRYPTO_LINKEDSEMI_SHA256)
/* ---- SHA224 / SHA256 / SM3：同一颗 sha256 引擎 ---- */
ZTEST(hash_api, sha224_kat)
{
	capi_hash_kat(capi_hash_sha256_dev, CRYPTO_HASH_ALGO_SHA224, "sha224",
		      "sha224_kat", h_sha224_abc, sizeof(h_sha224_abc));
}

ZTEST(hash_api, sha256_kat)
{
	capi_hash_kat(capi_hash_sha256_dev, CRYPTO_HASH_ALGO_SHA256, "sha256",
		      "sha256_kat", h_sha256_abc, sizeof(h_sha256_abc));
}

ZTEST(hash_api, sm3_kat)
{
	capi_hash_kat(capi_hash_sha256_dev, CRYPTO_HASH_ALGO_SM3, "sm3",
		      "sm3_kat", h_sm3_abc, sizeof(h_sm3_abc));
}

ZTEST(hash_api, sha224_split)
{
	static const size_t cuts[] = { 13, 20 };

	capi_hash_split_case(capi_hash_sha256_dev, CRYPTO_HASH_ALGO_SHA224,
			     "sha224", "sha224_split",
			     capi_msg_448, sizeof(capi_msg_448),
			     cuts, ARRAY_SIZE(cuts),
			     h_sha224_m448, sizeof(h_sha224_m448));
}

ZTEST(hash_api, sha256_split)
{
	static const size_t cuts[] = { 13, 20 };

	capi_hash_split_case(capi_hash_sha256_dev, CRYPTO_HASH_ALGO_SHA256,
			     "sha256", "sha256_split",
			     capi_msg_448, sizeof(capi_msg_448),
			     cuts, ARRAY_SIZE(cuts),
			     h_sha256_m448, sizeof(h_sha256_m448));
}

ZTEST(hash_api, sm3_split)
{
	static const size_t cuts[] = { 13, 20 };

	capi_hash_split_case(capi_hash_sha256_dev, CRYPTO_HASH_ALGO_SM3,
			     "sm3", "sm3_split",
			     capi_msg_448, sizeof(capi_msg_448),
			     cuts, ARRAY_SIZE(cuts),
			     h_sm3_m448, sizeof(h_sm3_m448));
}

/* ---- SHA256 padding 边界：0 / 55 / 56 / 64 字节 ---- */
ZTEST(hash_api, sha256_pad_boundary)
{
	static const struct {
		size_t len;
		const uint8_t *exp;
		size_t exp_len;
	} vec[] = {
		{ 0, h_sha256_pad_0, sizeof(h_sha256_pad_0) },
		{ 55, h_sha256_pad_55, sizeof(h_sha256_pad_55) },
		{ 56, h_sha256_pad_56, sizeof(h_sha256_pad_56) },
		{ 64, h_sha256_pad_64, sizeof(h_sha256_pad_64) },
	};
	uint8_t out[CAPI_HASH_MAX_DIGEST];
	bool ok[ARRAY_SIZE(vec)];
	int ret[ARRAY_SIZE(vec)];
	size_t i;

	capi_pad_buf_init();

	for (i = 0; i < ARRAY_SIZE(vec); i++) {
		memset(out, 0, sizeof(out));
		ret[i] = capi_hash_once(capi_hash_sha256_dev,
					CRYPTO_HASH_ALGO_SHA256,
					capi_pad_buf, vec[i].len, out);
		ok[i] = (ret[i] == 0) &&
			(memcmp(out, vec[i].exp, vec[i].exp_len) == 0);
	}

	ZT_INFO("sha256_pad_boundary",
		"algo=sha256,lens=0_55_56_64,len0=%d,len55=%d,len56=%d,len64=%d",
		ok[0], ok[1], ok[2], ok[3]);

	for (i = 0; i < ARRAY_SIZE(vec); i++) {
		zassert_equal(ret[i], 0, "sha256 padding len=%u ret=%d",
			      (unsigned int)vec[i].len, ret[i]);
		zassert_true(ok[i],
			     "sha256 digest wrong at padding boundary len=%u",
			     (unsigned int)vec[i].len);
	}
}
#endif /* CONFIG_CRYPTO_LINKEDSEMI_SHA256 */

#if defined(CONFIG_CRYPTO_LINKEDSEMI_SHA512)
/* ---- SHA384 / SHA512 ---- */
ZTEST(hash_api, sha384_kat)
{
	capi_hash_kat(capi_hash_sha512_dev, CRYPTO_HASH_ALGO_SHA384, "sha384",
		      "sha384_kat", h_sha384_abc, sizeof(h_sha384_abc));
}

ZTEST(hash_api, sha512_kat)
{
	capi_hash_kat(capi_hash_sha512_dev, CRYPTO_HASH_ALGO_SHA512, "sha512",
		      "sha512_kat", h_sha512_abc, sizeof(h_sha512_abc));
}

ZTEST(hash_api, sha384_split)
{
	static const size_t cuts[] = { 32, 48 };

	capi_hash_split_case(capi_hash_sha512_dev, CRYPTO_HASH_ALGO_SHA384,
			     "sha384", "sha384_split",
			     capi_msg_896, sizeof(capi_msg_896),
			     cuts, ARRAY_SIZE(cuts),
			     h_sha384_m896, sizeof(h_sha384_m896));
}

ZTEST(hash_api, sha512_split)
{
	static const size_t cuts[] = { 32, 48 };

	capi_hash_split_case(capi_hash_sha512_dev, CRYPTO_HASH_ALGO_SHA512,
			     "sha512", "sha512_split",
			     capi_msg_896, sizeof(capi_msg_896),
			     cuts, ARRAY_SIZE(cuts),
			     h_sha512_m896, sizeof(h_sha512_m896));
}

/* ---- SHA512 padding 边界：0 / 111 / 112 / 128 字节 ---- */
ZTEST(hash_api, sha512_pad_boundary)
{
	static const struct {
		size_t len;
		const uint8_t *exp;
		size_t exp_len;
	} vec[] = {
		{ 0, h_sha512_pad_0, sizeof(h_sha512_pad_0) },
		{ 111, h_sha512_pad_111, sizeof(h_sha512_pad_111) },
		{ 112, h_sha512_pad_112, sizeof(h_sha512_pad_112) },
		{ 128, h_sha512_pad_128, sizeof(h_sha512_pad_128) },
	};
	uint8_t out[CAPI_HASH_MAX_DIGEST];
	bool ok[ARRAY_SIZE(vec)];
	int ret[ARRAY_SIZE(vec)];
	size_t i;

	capi_pad_buf_init();

	for (i = 0; i < ARRAY_SIZE(vec); i++) {
		memset(out, 0, sizeof(out));
		ret[i] = capi_hash_once(capi_hash_sha512_dev,
					CRYPTO_HASH_ALGO_SHA512,
					capi_pad_buf, vec[i].len, out);
		ok[i] = (ret[i] == 0) &&
			(memcmp(out, vec[i].exp, vec[i].exp_len) == 0);
	}

	ZT_INFO("sha512_pad_boundary",
		"algo=sha512,lens=0_111_112_128,len0=%d,len111=%d,len112=%d,len128=%d",
		ok[0], ok[1], ok[2], ok[3]);

	for (i = 0; i < ARRAY_SIZE(vec); i++) {
		zassert_equal(ret[i], 0, "sha512 padding len=%u ret=%d",
			      (unsigned int)vec[i].len, ret[i]);
		zassert_true(ok[i],
			     "sha512 digest wrong at padding boundary len=%u",
			     (unsigned int)vec[i].len);
	}
}
#endif /* CONFIG_CRYPTO_LINKEDSEMI_SHA512 */

#endif /* CONFIG_CRYPTO_LINKEDSEMI_SHA256 || CONFIG_CRYPTO_LINKEDSEMI_SHA512 */
