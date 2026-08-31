/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * hash 套件：SHA224/SHA256/SHA384/SHA512/SM3 KAT（短报文 + 跨块长报文）。
 *
 * 判定 = 标准向量逐字节比对（expected/hash_vectors.h：FIPS 180-4 /
 * GM/T 0004 官方向量 + hashlib 计算的百万 'a' 长报文黄金值）。
 * 走 mbedtls API（mbedtls_sha256/sha512/sm3），具体落在软件还是
 * LINKEDSEMI 硬件/OTBN 路径由 case 的 Kconfig 路由决定（本工程测试目标）。
 * 跨块长报文经 update 分片喂入（1000 x 1000B），覆盖分块累积路径。
 */

#include "mbedtls_test_protocol.h"
#include "mbedtls_test.h"
#include "ztest_glue.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>

/* SM3 本树只有 alt 实现（无软件 SM3）：仅在 case 路由了 SM3 OTBN/硬件 ALT
 * 时编译（sm3_alt.h 的原型随同一宏门控）。 */
#if defined(CONFIG_MBEDTLS_SM3_LINKEDSEMI_OTBN_ALT) || \
	defined(CONFIG_MBEDTLS_SM3_LINKEDSEMI_HARDWARE_ALT)
#include <mbedtls/sm3_alt.h>
#define MT_HAVE_SM3 1
#endif

ZTEST_SUITE(hash, NULL, NULL, NULL, NULL, NULL);

#define MT_MD_MAX      64
#define MT_LONG_CHUNK  1000U   /* 百万 'a' 分片：1000 x 1000B */

static uint8_t long_chunk[MT_LONG_CHUNK];

/* 通用向量运行器：variant 语义 sha 族 = is224/is384，sm3 忽略 */
struct mt_hash_ops {
	const char *name;
	size_t md_size;
	int variant;
	int (*starts)(void *ctx, int variant);
	int (*update)(void *ctx, const unsigned char *in, size_t len);
	int (*finish)(void *ctx, unsigned char *out);
};

static int sha256_starts(void *ctx, int variant)
{
	return mbedtls_sha256_starts((mbedtls_sha256_context *)ctx, variant);
}

static int sha256_update(void *ctx, const unsigned char *in, size_t len)
{
	return mbedtls_sha256_update((mbedtls_sha256_context *)ctx, in, len);
}

static int sha256_finish(void *ctx, unsigned char *out)
{
	return mbedtls_sha256_finish((mbedtls_sha256_context *)ctx, out);
}

static int sha512_starts(void *ctx, int variant)
{
	return mbedtls_sha512_starts((mbedtls_sha512_context *)ctx, variant);
}

static int sha512_update(void *ctx, const unsigned char *in, size_t len)
{
	return mbedtls_sha512_update((mbedtls_sha512_context *)ctx, in, len);
}

static int sha512_finish(void *ctx, unsigned char *out)
{
	return mbedtls_sha512_finish((mbedtls_sha512_context *)ctx, out);
}

#ifdef MT_HAVE_SM3
static int sm3_starts(void *ctx, int variant)
{
	(void)variant;
	return mbedtls_sm3_starts((mbedtls_sm3_context *)ctx);
}

static int sm3_update(void *ctx, const unsigned char *in, size_t len)
{
	return mbedtls_sm3_update((mbedtls_sm3_context *)ctx, in, len);
}

static int sm3_finish(void *ctx, unsigned char *out)
{
	return mbedtls_sm3_finish((mbedtls_sm3_context *)ctx, out);
}
#endif /* MT_HAVE_SM3 */

static int run_hash(const struct mt_hash_ops *ops,
		    const struct mt_hash_vector *vecs, size_t nvecs,
		    const char *ztest_name)
{
	mbedtls_sha256_context sha256_ctx;
	mbedtls_sha512_context sha512_ctx;
#ifdef MT_HAVE_SM3
	mbedtls_sm3_context sm3_ctx;
#endif
	void *ctx;
	uint8_t md[MT_MD_MAX];
	uint8_t exp_md[MT_MD_MAX];
	uint32_t t0, us;
	int ret = 0;

	if (ops->starts == sha256_starts) {
		mbedtls_sha256_init(&sha256_ctx);
		ctx = &sha256_ctx;
	} else {
		mbedtls_sha512_init(&sha512_ctx);
		ctx = &sha512_ctx;
	}
#ifdef MT_HAVE_SM3
	if (ops->starts == sm3_starts) {
		mbedtls_sm3_init(&sm3_ctx);
		ctx = &sm3_ctx;
	}
#endif

	memset(long_chunk, 'a', sizeof(long_chunk));
	t0 = k_cycle_get_32();

	for (size_t v = 0; v < nvecs && ret == 0; v++) {
		size_t in_len = (vecs[v].input == MT_HASH_MILLION_A)
				? MT_HASH_MILLION_A_LEN : strlen(vecs[v].input);
		const unsigned char *in = (vecs[v].input == MT_HASH_MILLION_A)
					  ? long_chunk
					  : (const unsigned char *)vecs[v].input;

		ret = ops->starts(ctx, ops->variant);
		if (ret != 0) {
			break;
		}

		if (vecs[v].input == MT_HASH_MILLION_A) {
			/* 百万 'a'：1000 次 x 1000B 分片，覆盖跨块累积路径 */
			for (size_t c = 0; c < MT_HASH_MILLION_A_LEN / MT_LONG_CHUNK
			     && ret == 0; c++) {
				ret = ops->update(ctx, in, MT_LONG_CHUNK);
			}
		} else {
			ret = ops->update(ctx, in, in_len);
		}
		if (ret != 0) {
			break;
		}

		ret = ops->finish(ctx, md);
		if (ret != 0) {
			break;
		}

		if (mt_unhexify(exp_md, vecs[v].md_hex) != (int)ops->md_size) {
			zassert_true(false, "%s vec=%u expected md hex invalid",
				     ops->name, (unsigned int)v);
			ret = -1;
			break;
		}
		mt_assert_mem_eq(md, exp_md, ops->md_size, "%s KAT[%u]",
				 ops->name, (unsigned int)v);
	}

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO(ztest_name, "algo=%s,vecs=%u,long_bytes=%u,us=%u",
		ops->name, (unsigned int)nvecs, MT_HASH_MILLION_A_LEN, us);

	if (ops->starts == sha256_starts) {
		mbedtls_sha256_free(&sha256_ctx);
	} else {
		mbedtls_sha512_free(&sha512_ctx);
	}
#ifdef MT_HAVE_SM3
	if (ops->starts == sm3_starts) {
		mbedtls_sm3_free(&sm3_ctx);
	}
#endif
	return ret;
}

#define MT_RUN_HASH(suite_name, ops_ptr, vecs_arr, zt_name) \
	do { \
		/* 变量名避开 _ret：zassert 宏内部声明同名变量会遮蔽外层 */ \
		int mt_ret = run_hash((ops_ptr), (vecs_arr), \
				      ARRAY_SIZE(vecs_arr), (zt_name)); \
		zassert_equal(mt_ret, 0, "%s ret=%d", (zt_name), mt_ret); \
	} while (0)

static const struct mt_hash_ops mt_sha224_ops = {
	.name = "sha224", .md_size = 28, .variant = 1,
	.starts = sha256_starts, .update = sha256_update, .finish = sha256_finish,
};

static const struct mt_hash_ops mt_sha256_ops = {
	.name = "sha256", .md_size = 32, .variant = 0,
	.starts = sha256_starts, .update = sha256_update, .finish = sha256_finish,
};

static const struct mt_hash_ops mt_sha384_ops = {
	.name = "sha384", .md_size = 48, .variant = 1,
	.starts = sha512_starts, .update = sha512_update, .finish = sha512_finish,
};

static const struct mt_hash_ops mt_sha512_ops = {
	.name = "sha512", .md_size = 64, .variant = 0,
	.starts = sha512_starts, .update = sha512_update, .finish = sha512_finish,
};

#ifdef MT_HAVE_SM3
static const struct mt_hash_ops mt_sm3_ops = {
	.name = "sm3", .md_size = 32, .variant = 0,
	.starts = sm3_starts, .update = sm3_update, .finish = sm3_finish,
};
#endif

ZTEST(hash, sha224_kat)
{
	MT_RUN_HASH(hash, &mt_sha224_ops, mt_sha224_vecs, "sha224_kat");
}

ZTEST(hash, sha256_kat)
{
	MT_RUN_HASH(hash, &mt_sha256_ops, mt_sha256_vecs, "sha256_kat");
}

ZTEST(hash, sha384_kat)
{
	MT_RUN_HASH(hash, &mt_sha384_ops, mt_sha384_vecs, "sha384_kat");
}

ZTEST(hash, sha512_kat)
{
	MT_RUN_HASH(hash, &mt_sha512_ops, mt_sha512_vecs, "sha512_kat");
}

#ifdef MT_HAVE_SM3
ZTEST(hash, sm3_kat)
{
	MT_RUN_HASH(hash, &mt_sm3_ops, mt_sm3_vecs, "sm3_kat");
}
#endif /* MT_HAVE_SM3 */
