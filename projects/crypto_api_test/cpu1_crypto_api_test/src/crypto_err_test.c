/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Crypto 驱动 API 的**错误返回**校验（负例）。
 *
 * DUT（同一镜像内三颗驱动）：
 *   zephyr/drivers/crypto/crypto_linkedsemi_sm4.c
 *   zephyr/drivers/crypto/crypto_linkedsemi_sha256.c
 *   zephyr/drivers/crypto/crypto_linkedsemi_sha512.c
 *
 * 判据来源 = **本驱动的实际契约**（2026-10-29 实机复核），按 API 层级区分：
 *
 *   - session 级不支持（algo / mode / keylen / flags）→ `-ENOTSUP`
 *       本驱动 `crypto_linkedsemi_{sm4,sha256,sha512}.c` 在这四条路径上
 *       一律 `return -ENOTSUP` 并打 `LOG_ERR("Unsupported …")`（sm4 5 处、
 *       sha256 2 处、sha512 2 处）。语义上也更贴切：这些参数的**枚举值本身
 *       合法**，只是驱动不支持，不属"参数非法"。
 *       ⚠️ 与本树多数厂商驱动（`return -EINVAL` 184 处 vs `-ENOTSUP` 23 处）
 *       **刻意不一致**：驱动侧统一成 -EINVAL 会波及 mbedtls 等其它工程，
 *       本仓测试按驱动真实契约断言。若将来驱动统一口径，本组期望值需同步改
 *       （见 KNOWN_ISSUES #157）。
 *
 *   - 参数越界（CCM 的 NULL / 长度 / tag_len）→ `-EINVAL`（同一驱动的 pkt 级
 *       路径确实返回 -EINVAL，实测佐证，见 ERR_CCM 组）。
 *
 * ⚠️ 历史沿革：本组用例原先按生态多数惯例把 session 级也断言为 `-EINVAL`，
 *    导致对当前驱动 7 项恒 FAIL。2026-10-29 核实"驱动 session/pkt 两套错误码
 *    是有意为之、且各自语义自洽"后改判为本驱动的真实契约。**这不是放宽校验**：
 *    仍是逐值精确断言，错误码不同照样 FAIL。
 *
 * ⚠️ 负例的"假 PASS"防线：所有 CCM 负例**先跑一次合法调用作正对照**，
 *    只有正对照返回 0 时，后续 `ret == -EINVAL` 才有判别力（否则"全返 -EINVAL"
 *    的实现也能让负例全绿）。
 *
 * ⚠️ 未覆盖（驱动侧缺口，登记在 COVERAGE.md）：
 *   - GCM 路径**没有任何参数校验**，NULL 入参会直接解引用（Load access fault），
 *     故不做 NULL 负例（会把整个 case 打崩）；需驱动先补校验。
 *   - ECB/CBC 对 `in_len % 16 != 0` 既无错误码也无断言：ECB 忽略余块后
 *     `return ret;`（`ret` 未初始化），CBC 同样忽略余块。无确定返回值可断言，
 *     故不写负例，记为覆盖缺口。
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/ztest.h>
#include <zephyr/crypto/crypto.h>

#include "ztest_glue.h"

#if defined(CONFIG_CRYPTO_LINKEDSEMI_SM4) || \
	defined(CONFIG_CRYPTO_LINKEDSEMI_SHA256) || \
	defined(CONFIG_CRYPTO_LINKEDSEMI_SHA512)

/* 合法 flags：RAW_KEY + SYNC + SEPARATE_IO_BUFS（两个 wrapper 的断言都要求
 * 恰好一个 IO 类型、恰好一个 sync/async 类型） */
#define ERR_VALID_FLAGS (CAP_RAW_KEY | CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS)
/* 非法但能过 wrapper 断言的 flags：只声明 ASYNC（驱动 caps 不含它） */
#define ERR_ASYNC_FLAGS (CAP_RAW_KEY | CAP_ASYNC_OPS | CAP_SEPARATE_IO_BUFS)

#define ERR_MSG_LEN 64
#define ERR_NONCE_LEN 12
#define ERR_AAD_LEN 8
#define ERR_TAG_LEN 16

/* ============================ SM4 ============================ */
#if defined(CONFIG_CRYPTO_LINKEDSEMI_SM4)

static const struct device *err_sm4_dev = DEVICE_DT_GET(DT_NODELABEL(sm4));

static const uint8_t err_sm4_key[16] = {
	0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
	0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
};

ZTEST_SUITE(sm4_err, NULL, NULL, NULL, NULL, NULL);

static int err_sm4_begin(enum cipher_algo algo, uint16_t keylen,
			 uint16_t flags, enum cipher_mode mode)
{
	struct cipher_ctx ctx = {
		.keylen = keylen,
		.key.bit_stream = err_sm4_key,
		.flags = flags,
	};

	return cipher_begin_session(err_sm4_dev, &ctx, algo, mode,
				    CRYPTO_CIPHER_OP_ENCRYPT);
}

/* ---- 能力位与设备就绪：三条必需能力必须都声明 ----
 * 测试名一律带 `sm4_`/`hash_` 前缀：run_test.py 的 unit_test_names 会剥掉
 * suite 名（只取 `<suite>__<name>` 的后半段），同名项在报告表里会撞键。 */
ZTEST(sm4_err, sm4_caps)
{
	int caps = crypto_query_hwcaps(err_sm4_dev);
	bool ready = device_is_ready(err_sm4_dev);
	bool raw = (caps & CAP_RAW_KEY) != 0;
	bool sync = (caps & CAP_SYNC_OPS) != 0;
	bool sep = (caps & CAP_SEPARATE_IO_BUFS) != 0;

	ZT_INFO("sm4_caps", "ready=%d,caps=0x%x,raw_key=%d,sync=%d,sep_io=%d",
		ready, caps, raw, sync, sep);

	zassert_true(ready, "sm4 device not ready");
	zassert_true(raw, "sm4 caps missing CAP_RAW_KEY (0x%x)", caps);
	zassert_true(sync, "sm4 caps missing CAP_SYNC_OPS (0x%x)", caps);
	zassert_true(sep, "sm4 caps missing CAP_SEPARATE_IO_BUFS (0x%x)", caps);
}

/* ---- 不支持的算法 → -ENOTSUP ---- */
ZTEST(sm4_err, sm4_bad_algo)
{
	int ret = err_sm4_begin(CRYPTO_CIPHER_ALGO_AES, 16, ERR_VALID_FLAGS,
				CRYPTO_CIPHER_MODE_ECB);

	ZT_INFO("sm4_bad_algo", "algo=aes,ret=%d,ok=%d", ret, ret == -ENOTSUP);

	zassert_equal(ret, -ENOTSUP, "begin_session(AES) ret=%d, expected -ENOTSUP", ret);
}

/* ---- 错误 key 长度 → -ENOTSUP ---- */
ZTEST(sm4_err, sm4_bad_keylen)
{
	int ret = err_sm4_begin(CRYPTO_CIPHER_ALGO_SM4, 24, ERR_VALID_FLAGS,
				CRYPTO_CIPHER_MODE_ECB);

	ZT_INFO("sm4_bad_keylen", "keylen=24,ret=%d,ok=%d", ret, ret == -ENOTSUP);

	zassert_equal(ret, -ENOTSUP, "begin_session(keylen=24) ret=%d, expected -ENOTSUP", ret);
}

/* ---- 不支持的 flags（ASYNC，驱动只有同步实现）→ -ENOTSUP ---- */
ZTEST(sm4_err, sm4_bad_flags)
{
	int ret = err_sm4_begin(CRYPTO_CIPHER_ALGO_SM4, 16, ERR_ASYNC_FLAGS,
				CRYPTO_CIPHER_MODE_ECB);

	ZT_INFO("sm4_bad_flags", "flags=0x%lx,ret=%d,ok=%d",
		(unsigned long)ERR_ASYNC_FLAGS, ret, ret == -ENOTSUP);

	zassert_equal(ret, -ENOTSUP, "begin_session(CAP_ASYNC_OPS) ret=%d, expected -ENOTSUP",
		      ret);
}

/* ---- 不支持的模式 → -ENOTSUP ---- */
ZTEST(sm4_err, sm4_bad_mode)
{
	int ret = err_sm4_begin(CRYPTO_CIPHER_ALGO_SM4, 16, ERR_VALID_FLAGS,
				(enum cipher_mode)0);

	ZT_INFO("sm4_bad_mode", "mode=0,ret=%d,ok=%d", ret, ret == -ENOTSUP);

	zassert_equal(ret, -ENOTSUP, "begin_session(mode=0) ret=%d, expected -ENOTSUP", ret);
}

/* ---- CCM 参数越界 → -EINVAL（先做正对照） ---- */
static int err_ccm_call(struct cipher_ctx *ctx, uint8_t *nonce,
			uint8_t *in, int in_len, uint8_t *out, int out_max,
			uint8_t *ad, uint32_t ad_len, uint8_t *tag)
{
	struct cipher_pkt pkt = {
		.in_buf = in,
		.in_len = in_len,
		.out_buf = out,
		.out_buf_max = out_max,
	};
	struct cipher_aead_pkt aead = {
		.pkt = &pkt,
		.ad = ad,
		.ad_len = ad_len,
		.tag = tag,
	};

	return cipher_ccm_op(ctx, &aead, nonce);
}

ZTEST(sm4_err, sm4_ccm_params)
{
	struct cipher_ctx ctx = {
		.keylen = 16,
		.key.bit_stream = err_sm4_key,
		.flags = ERR_VALID_FLAGS,
	};
	uint8_t nonce[ERR_NONCE_LEN] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
		0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
	};
	uint8_t aad[ERR_AAD_LEN] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	uint8_t in[ERR_MSG_LEN];
	uint8_t out[ERR_MSG_LEN];
	uint8_t tag[ERR_TAG_LEN];
	int ret_null_out, ret_null_in, ret_null_ad, ret_small_out;
	int ret_tag_short, ret_tag_odd, ret_tag_big;
	int ret_base, ret_begin;
	bool base_ok;

	for (size_t i = 0; i < sizeof(in); i++) {
		in[i] = (uint8_t)i;
	}
	memset(out, 0, sizeof(out));
	memset(tag, 0, sizeof(tag));

	ctx.mode_params.ccm_info.nonce_len = ERR_NONCE_LEN;
	ctx.mode_params.ccm_info.tag_len = ERR_TAG_LEN;

	ret_begin = cipher_begin_session(err_sm4_dev, &ctx, CRYPTO_CIPHER_ALGO_SM4,
					 CRYPTO_CIPHER_MODE_CCM,
					 CRYPTO_CIPHER_OP_ENCRYPT);

	/* begin 失败时不能继续调 op（ops 未填，调用会踩空）。先断言、就此收尾。 */
	if (ret_begin != 0) {
		ZT_INFO("sm4_ccm_params", "begin=%d,aborted=1", ret_begin);
		zassert_equal(ret_begin, 0, "ccm begin_session ret=%d", ret_begin);
		return;
	}

	/* 正对照：合法参数必须成功。没有它，下面的 -EINVAL 可能来自"整个会话坏掉" */
	ret_base = err_ccm_call(&ctx, nonce, in, sizeof(in), out, sizeof(out),
				aad, sizeof(aad), tag);
	base_ok = (ret_base == 0);

	/* 逐项：只改一个非法字段 */
	ret_null_out = err_ccm_call(&ctx, nonce, in, sizeof(in), NULL, sizeof(out),
				    aad, sizeof(aad), tag);
	ret_null_in = err_ccm_call(&ctx, nonce, NULL, sizeof(in), out, sizeof(out),
				   aad, sizeof(aad), tag);
	ret_null_ad = err_ccm_call(&ctx, nonce, in, sizeof(in), out, sizeof(out),
				   NULL, ERR_AAD_LEN, tag);
	ret_small_out = err_ccm_call(&ctx, nonce, in, sizeof(in), out,
				     sizeof(out) / 2, aad, sizeof(aad), tag);

	ctx.mode_params.ccm_info.tag_len = 3;      /* < 4 */
	ret_tag_short = err_ccm_call(&ctx, nonce, in, sizeof(in), out, sizeof(out),
				     aad, sizeof(aad), tag);
	ctx.mode_params.ccm_info.tag_len = 5;      /* 奇数 */
	ret_tag_odd = err_ccm_call(&ctx, nonce, in, sizeof(in), out, sizeof(out),
				   aad, sizeof(aad), tag);
	ctx.mode_params.ccm_info.tag_len = 18;     /* > 16 */
	ret_tag_big = err_ccm_call(&ctx, nonce, in, sizeof(in), out, sizeof(out),
				   aad, sizeof(aad), tag);
	ctx.mode_params.ccm_info.tag_len = ERR_TAG_LEN;

	(void)cipher_free_session(err_sm4_dev, &ctx);

	ZT_INFO("sm4_ccm_params",
		"base=%d,null_out=%d,null_in=%d,null_ad=%d,small_out=%d,"
		"tag3=%d,tag5=%d,tag18=%d",
		ret_base, ret_null_out, ret_null_in, ret_null_ad, ret_small_out,
		ret_tag_short, ret_tag_odd, ret_tag_big);

	zassert_true(base_ok, "ccm positive control ret=%d (session broken, negatives meaningless)",
		     ret_base);
	zassert_equal(ret_null_out, -EINVAL, "ccm out_buf=NULL ret=%d", ret_null_out);
	zassert_equal(ret_null_in, -EINVAL, "ccm in_buf=NULL ret=%d", ret_null_in);
	zassert_equal(ret_null_ad, -EINVAL, "ccm ad=NULL ret=%d", ret_null_ad);
	zassert_equal(ret_small_out, -EINVAL, "ccm out_buf_max<in_len ret=%d", ret_small_out);
	zassert_equal(ret_tag_short, -EINVAL, "ccm tag_len=3 ret=%d", ret_tag_short);
	zassert_equal(ret_tag_odd, -EINVAL, "ccm tag_len=5 (odd) ret=%d", ret_tag_odd);
	zassert_equal(ret_tag_big, -EINVAL, "ccm tag_len=18 ret=%d", ret_tag_big);
}

#endif /* CONFIG_CRYPTO_LINKEDSEMI_SM4 */

/* ============================ hash ============================ */
#if defined(CONFIG_CRYPTO_LINKEDSEMI_SHA256) || \
	defined(CONFIG_CRYPTO_LINKEDSEMI_SHA512)

ZTEST_SUITE(hash_err, NULL, NULL, NULL, NULL, NULL);

static int err_hash_begin(const struct device *dev, enum hash_algo algo,
			  uint16_t flags)
{
	struct hash_ctx ctx = { .flags = flags };

	return hash_begin_session(dev, &ctx, algo);
}

#endif /* SHA256 || SHA512 */

#if defined(CONFIG_CRYPTO_LINKEDSEMI_SHA256)
static const struct device *err_hash_sha256_dev =
	DEVICE_DT_GET(DT_NODELABEL(sha256));

ZTEST(hash_err, hash_caps_sha256)
{
	int caps = crypto_query_hwcaps(err_hash_sha256_dev);
	bool ready = device_is_ready(err_hash_sha256_dev);
	bool raw = (caps & CAP_RAW_KEY) != 0;
	bool sync = (caps & CAP_SYNC_OPS) != 0;
	bool sep = (caps & CAP_SEPARATE_IO_BUFS) != 0;

	ZT_INFO("hash_caps_sha256", "ready=%d,caps=0x%x,raw_key=%d,sync=%d,sep_io=%d",
		ready, caps, raw, sync, sep);

	zassert_true(ready, "sha256 device not ready");
	/* hash 不取 key，但 crypto_query_hwcaps() 要求至少声明一种 key 类型，
	 * 缺了会在 CONFIG_ASSERT 下 panic（KNOWN_ISSUES #158），故显式锁住。 */
	zassert_true(raw, "sha256 caps missing CAP_RAW_KEY (0x%x)", caps);
	zassert_true(sync, "sha256 caps missing CAP_SYNC_OPS (0x%x)", caps);
	zassert_true(sep, "sha256 caps missing CAP_SEPARATE_IO_BUFS (0x%x)", caps);
}

/* ---- sha256 引擎只支持 SHA224/256/SM3：SHA384/SHA512 → -ENOTSUP ---- */
ZTEST(hash_err, hash_bad_algo_sha256)
{
	int ret_384 = err_hash_begin(err_hash_sha256_dev, CRYPTO_HASH_ALGO_SHA384,
				     CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS);
	int ret_512 = err_hash_begin(err_hash_sha256_dev, CRYPTO_HASH_ALGO_SHA512,
				     CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS);

	ZT_INFO("hash_bad_algo_sha256", "sha384=%d,sha512=%d", ret_384, ret_512);

	zassert_equal(ret_384, -ENOTSUP, "sha256 dev begin(SHA384) ret=%d, expected -ENOTSUP",
		      ret_384);
	zassert_equal(ret_512, -ENOTSUP, "sha256 dev begin(SHA512) ret=%d, expected -ENOTSUP",
		      ret_512);
}
#endif /* CONFIG_CRYPTO_LINKEDSEMI_SHA256 */

#if defined(CONFIG_CRYPTO_LINKEDSEMI_SHA512)
static const struct device *err_hash_sha512_dev =
	DEVICE_DT_GET(DT_NODELABEL(sha512));

ZTEST(hash_err, hash_caps_sha512)
{
	int caps = crypto_query_hwcaps(err_hash_sha512_dev);
	bool ready = device_is_ready(err_hash_sha512_dev);
	bool raw = (caps & CAP_RAW_KEY) != 0;
	bool sync = (caps & CAP_SYNC_OPS) != 0;
	bool sep = (caps & CAP_SEPARATE_IO_BUFS) != 0;

	ZT_INFO("hash_caps_sha512", "ready=%d,caps=0x%x,raw_key=%d,sync=%d,sep_io=%d",
		ready, caps, raw, sync, sep);

	zassert_true(ready, "sha512 device not ready");
	/* 同 sha256：hash 不取 key，但 crypto_query_hwcaps() 要求至少声明一种
	 * key 类型，缺了会在 CONFIG_ASSERT 下 panic（KNOWN_ISSUES #158）。 */
	zassert_true(raw, "sha512 caps missing CAP_RAW_KEY (0x%x)", caps);
	zassert_true(sync, "sha512 caps missing CAP_SYNC_OPS (0x%x)", caps);
	zassert_true(sep, "sha512 caps missing CAP_SEPARATE_IO_BUFS (0x%x)", caps);
}

/* ---- sha512 引擎只支持 SHA384/512：SHA224/SHA256/SM3 → -ENOTSUP ---- */
ZTEST(hash_err, hash_bad_algo_sha512)
{
	int ret_224 = err_hash_begin(err_hash_sha512_dev, CRYPTO_HASH_ALGO_SHA224,
				     CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS);
	int ret_256 = err_hash_begin(err_hash_sha512_dev, CRYPTO_HASH_ALGO_SHA256,
				     CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS);
	int ret_sm3 = err_hash_begin(err_hash_sha512_dev, CRYPTO_HASH_ALGO_SM3,
				     CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS);

	ZT_INFO("hash_bad_algo_sha512", "sha224=%d,sha256=%d,sm3=%d",
		ret_224, ret_256, ret_sm3);

	zassert_equal(ret_224, -ENOTSUP, "sha512 dev begin(SHA224) ret=%d, expected -ENOTSUP",
		      ret_224);
	zassert_equal(ret_256, -ENOTSUP, "sha512 dev begin(SHA256) ret=%d, expected -ENOTSUP",
		      ret_256);
	zassert_equal(ret_sm3, -ENOTSUP, "sha512 dev begin(SM3) ret=%d, expected -ENOTSUP",
		      ret_sm3);
}
#endif /* CONFIG_CRYPTO_LINKEDSEMI_SHA512 */

#if defined(CONFIG_CRYPTO_LINKEDSEMI_SHA256)
/* ---- 不支持的 flags（ASYNC）→ -ENOTSUP ---- */
ZTEST(hash_err, hash_bad_flags)
{
	int ret = err_hash_begin(err_hash_sha256_dev, CRYPTO_HASH_ALGO_SHA256,
				 CAP_ASYNC_OPS | CAP_SEPARATE_IO_BUFS);

	ZT_INFO("hash_bad_flags", "flags=async|sep_io,ret=%d,ok=%d", ret, ret == -ENOTSUP);

	zassert_equal(ret, -ENOTSUP, "hash begin(CAP_ASYNC_OPS) ret=%d, expected -ENOTSUP",
		      ret);
}
#endif /* CONFIG_CRYPTO_LINKEDSEMI_SHA256 */

#endif /* 任一 crypto 驱动使能 */
