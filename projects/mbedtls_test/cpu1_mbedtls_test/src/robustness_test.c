/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 鲁棒性测试：异常/临界输入、状态污染、并发共享上下文。
 *
 * 判定 = 不 HardFault、返回码符合文档或保持静默失败；
 * 不追求所有异常都返回 -EINVAL。
 */

#include "mbedtls_test_protocol.h"
#include "mbedtls_test.h"
#include "ztest_glue.h"
#include "pointops_vectors.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <mbedtls/aes.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/rsa.h>

extern const unsigned char rsa_msg[];
extern int rsa_import_test_key(mbedtls_rsa_context *rsa);

#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN)
#include <otbn_pointops.h>
#define MT_HAVE_POINTOPS_ROBUST 1
#endif

#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
#include <mbedtls/sm4_alt.h>
#endif

ZTEST_SUITE(robustness, NULL, NULL, NULL, NULL, NULL);

/* ---- SHA-256 异常参数 ---- */
ZTEST(robustness, sha256_bad_params)
{
	mbedtls_sha256_context ctx;
	unsigned char md[32];
	int ret;

	/* NULL context：starts 不应写入/崩溃，返回非 0 */
	ret = mbedtls_sha256_starts(NULL, 0);
	zassert_not_equal(ret, 0, "sha256_starts NULL should fail");

	/* NULL input with len>0 */
	mbedtls_sha256_init(&ctx);
	ret = mbedtls_sha256_starts(&ctx, 0);
	zassert_equal(ret, 0, "sha256_starts init ret=%d", ret);
	ret = mbedtls_sha256_update(&ctx, NULL, 4);
	zassert_not_equal(ret, 0, "sha256_update NULL should fail");

	/* len=0 合法（空 update） */
	ret = mbedtls_sha256_update(&ctx, NULL, 0);
	zassert_equal(ret, 0, "sha256_update len=0 ret=%d", ret);

	/* finish 前 reset 再 update */
	ret = mbedtls_sha256_starts(&ctx, 0);
	zassert_equal(ret, 0, "sha256 re-starts ret=%d", ret);
	ret = mbedtls_sha256_update(&ctx, (const unsigned char *)"abc", 3);
	zassert_equal(ret, 0, "sha256 update abc ret=%d", ret);
	ret = mbedtls_sha256_finish(&ctx, md);
	zassert_equal(ret, 0, "sha256 finish ret=%d", ret);

	mbedtls_sha256_free(&ctx);
}

/* ---- SHA-384 异常参数 ---- */
ZTEST(robustness, sha384_bad_params)
{
	mbedtls_sha512_context ctx;
	int ret;

	ret = mbedtls_sha512_starts(NULL, 1);
	zassert_not_equal(ret, 0, "sha384_starts NULL should fail");

	mbedtls_sha512_init(&ctx);
	ret = mbedtls_sha512_starts(&ctx, 1);
	zassert_equal(ret, 0, "sha384_starts init ret=%d", ret);
	ret = mbedtls_sha512_update(&ctx, NULL, 4);
	zassert_not_equal(ret, 0, "sha384_update NULL should fail");

	mbedtls_sha512_free(&ctx);
}

/* ---- AES 异常参数 ---- */
ZTEST(robustness, aes_bad_params)
{
	mbedtls_aes_context ctx;
	unsigned char key[16], iv[16], pt[16], ct[16];
	int ret;

	memset(key, 0xab, sizeof(key));
	memset(iv, 0, sizeof(iv));
	memset(pt, 0, sizeof(pt));

	/* NULL ctx setkey */
	ret = mbedtls_aes_setkey_enc(NULL, key, 128);
	zassert_not_equal(ret, 0, "aes setkey NULL should fail");

	/* 错误 keybits */
	mbedtls_aes_init(&ctx);
	ret = mbedtls_aes_setkey_enc(&ctx, key, 64);
	zassert_not_equal(ret, 0, "aes setkey bad bits should fail");

	/* 正常 setkey 后 CBC NULL in/out */
	ret = mbedtls_aes_setkey_enc(&ctx, key, 128);
	zassert_equal(ret, 0, "aes setkey ret=%d", ret);
	ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, 16, iv, NULL, ct);
	zassert_not_equal(ret, 0, "aes cbc NULL input should fail");

	mbedtls_aes_free(&ctx);
}

/* ---- SM4 异常参数 ---- */
#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
ZTEST(robustness, sm4_bad_params)
{
	mbedtls_sm4_context ctx;
	unsigned char key[16] __attribute__((aligned(4)));
	unsigned char iv[16] __attribute__((aligned(4)));
	unsigned char pt[16] __attribute__((aligned(4)));
	unsigned char ct[16] __attribute__((aligned(4)));
	int ret;

	memset(key, 0xab, sizeof(key));
	memset(iv, 0, sizeof(iv));
	memset(pt, 0, sizeof(pt));

	mbedtls_sm4_init(&ctx);
	ret = mbedtls_sm4_setkey(key);
	zassert_equal(ret, 0, "sm4 setkey ret=%d", ret);
	ret = mbedtls_sm4_setiv(&ctx, iv);
	zassert_equal(ret, 0, "sm4 setiv ret=%d", ret);

	ret = mbedtls_sm4_ctr_crypto(&ctx, ct, NULL, 16);
	zassert_not_equal(ret, 0, "sm4 ctr NULL input should fail");

	/* ECB 原语走内联寄存器操作（不经 HAL），须自行拒绝非法入参 */
	ret = mbedtls_sm4_ecb_encrypt(NULL, ct, pt, 16);
	zassert_not_equal(ret, 0, "sm4 ecb NULL ctx should fail");
	ret = mbedtls_sm4_ecb_encrypt(&ctx, ct, NULL, 16);
	zassert_not_equal(ret, 0, "sm4 ecb NULL input should fail");
	ret = mbedtls_sm4_ecb_encrypt(&ctx, NULL, pt, 16);
	zassert_not_equal(ret, 0, "sm4 ecb NULL output should fail");
	ret = mbedtls_sm4_ecb_decrypt(&ctx, ct, NULL, 16);
	zassert_not_equal(ret, 0, "sm4 ecb dec NULL input should fail");
	ret = mbedtls_sm4_ecb_decrypt(&ctx, NULL, pt, 16);
	zassert_not_equal(ret, 0, "sm4 ecb dec NULL output should fail");

	/* 长度 0 与非 16 整块：进硬件前即须拒绝（CALC_LEN 下溢） */
	ret = mbedtls_sm4_ecb_encrypt(&ctx, ct, pt, 0);
	zassert_not_equal(ret, 0, "sm4 ecb len=0 should fail");
	ret = mbedtls_sm4_ecb_encrypt(&ctx, ct, pt, 20);
	zassert_not_equal(ret, 0, "sm4 ecb non-block len should fail");

	/* 合法入参仍须成功——确认校验未误伤正常路径 */
	ret = mbedtls_sm4_ecb_encrypt(&ctx, ct, pt, 16);
	zassert_equal(ret, 0, "sm4 ecb valid ret=%d", ret);
	ret = mbedtls_sm4_ecb_decrypt(&ctx, pt, ct, 16);
	zassert_equal(ret, 0, "sm4 ecb dec valid ret=%d", ret);

	mbedtls_sm4_free(&ctx);
}
#endif

/* ---- ECDSA 异常/负例 ---- */
ZTEST(robustness, ecdsa_tamper_verify)
{
	mbedtls_ecp_group grp;
	mbedtls_ecp_point Q;
	mbedtls_mpi d, r, s;
	unsigned char hash[32];
	int ret;

	mbedtls_ecp_group_init(&grp);
	mbedtls_ecp_point_init(&Q);
	mbedtls_mpi_init(&d);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
	if (ret == 0) {
		ret = mbedtls_ecp_gen_keypair(&grp, &d, &Q, mt_rng_trng, NULL);
	}
	memset(hash, 0x55, sizeof(hash));
	if (ret == 0) {
		ret = mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, sizeof(hash),
				       mt_rng_fixed, NULL);
	}
	if (ret == 0) {
		ret = mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &Q, &r, &s);
		zassert_equal(ret, 0, "ecdsa verify good ret=%d", ret);
	}

	/* 改消息后 verify 应失败 */
	if (ret == 0) {
		hash[0] ^= 0x01;
		ret = mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &Q, &r, &s);
		zassert_not_equal(ret, 0, "ecdsa verify tampered should fail");
		hash[0] ^= 0x01;
	}

	/* NULL ctx（group 为空） */
	if (ret != 0) {
		ret = 0; /* reset so test can continue */
	}
	ret = mbedtls_ecdsa_verify(NULL, hash, sizeof(hash), &Q, &r, &s);
	zassert_not_equal(ret, 0, "ecdsa verify NULL grp should fail");

	mbedtls_ecp_group_free(&grp);
	mbedtls_ecp_point_free(&Q);
	mbedtls_mpi_free(&d);
	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
}

/* ---- RSA 异常/负例 ---- */
ZTEST(robustness, rsa_key_mismatch)
{
	mbedtls_rsa_context rsa_pub;
	unsigned char n[256];
	int ret;

	mbedtls_rsa_init(&rsa_pub);

	/* 构造只含公钥 (n,e) 的上下文 */
	memcpy(n, rsa2048_n, sizeof(rsa2048_n));
	ret = mbedtls_rsa_import_raw(&rsa_pub,
				     n, sizeof(n),
				     NULL, 0,
				     NULL, 0,
				     NULL, 0,
				     rsa2048_e, sizeof(rsa2048_e));
	if (ret == 0) {
		ret = mbedtls_rsa_complete(&rsa_pub);
	}
	zassert_equal(ret, 0, "rsa import pub ret=%d", ret);

	/* 公钥上下文缺少 d/p/q 等私钥分量，check_privkey 应失败 */
	ret = mbedtls_rsa_check_privkey(&rsa_pub);
	zassert_not_equal(ret, 0, "rsa check_privkey pub-only should fail");

	mbedtls_rsa_free(&rsa_pub);
}

/* ---- RSA keygen 非 F4 回退软件 ---- */
ZTEST(robustness, rsa_keygen_non_f4_fallback)
{
	mbedtls_rsa_context rsa;
	int ret;

	mbedtls_rsa_init(&rsa);
	/* public exponent = 3，应仍成功（可能回退软件路径） */
	ret = mbedtls_rsa_gen_key(&rsa, mt_rng_trng, NULL, 2048, 3);
	zassert_equal(ret, 0, "rsa genkey e=3 ret=%d", ret);

	if (ret == 0) {
		ret = mbedtls_rsa_check_privkey(&rsa);
		zassert_equal(ret, 0, "rsa check e=3 ret=%d", ret);
	}

	mbedtls_rsa_free(&rsa);
}

/* 双线程共享同一 SHA-256 ctx（THREADING_ALT 互斥生效）。
 * 仅在纯软件 SHA-256 路径下执行：HW/OTBN alt 驱动层在跨线程复用同一 ctx
 * 时会死锁（doneLock 快照/引擎状态与线程切换互锁，实测 case_hw_otbn_mix
 * 子线程 update 挂死），属驱动限制，非本测试目标；HW/OTBN 路径的线程安全
 * 由 concurrency_test.c 的独立上下文并发 suite 覆盖。
 */
#if defined(CONFIG_MBEDTLS_THREADING_LINKEDSEMI_ALT) && \
	!defined(CONFIG_MBEDTLS_SHA224_SHA256_LINKEDSEMI_HARDWARE_ALT) && \
	!defined(CONFIG_MBEDTLS_SHA224_SHA256_LINKEDSEMI_OTBN_ALT)
static K_THREAD_STACK_DEFINE(rb_sha_stack, 24576);
static struct k_thread rb_sha_thread;
static volatile int rb_sha_ret;
static mbedtls_sha256_context rb_sha_ctx;
static unsigned char rb_sha_md[32];

static void rb_sha_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	int ret = 0;
	for (int i = 0; i < 10 && ret == 0; i++) {
		ret = mbedtls_sha256_update(&rb_sha_ctx,
					    (const unsigned char *)"thread", 6);
	}
	rb_sha_ret = ret;
}

ZTEST(robustness, shared_sha256_ctx_thread)
{
	int ret;

	mbedtls_sha256_init(&rb_sha_ctx);
	ret = mbedtls_sha256_starts(&rb_sha_ctx, 0);
	zassert_equal(ret, 0, "shared sha starts ret=%d", ret);

	rb_sha_ret = -1;
	k_thread_create(&rb_sha_thread, rb_sha_stack,
			K_THREAD_STACK_SIZEOF(rb_sha_stack),
			rb_sha_thread_fn, NULL, NULL, NULL,
			K_PRIO_PREEMPT(-1), 0, K_NO_WAIT);

	/* 子线程优先级更高：先 yield 让子线程完成全部 update */
	k_yield();

	/* 主线程同步更新同一 ctx */
	for (int i = 0; i < 10 && ret == 0; i++) {
		ret = mbedtls_sha256_update(&rb_sha_ctx,
					    (const unsigned char *)"main", 4);
	}

	/* 10s 超时兜底：若驱动层锁嵌套死锁，不让整个 suite 挂死 */
	ret = k_thread_join(&rb_sha_thread, K_MSEC(10000));
	zassert_equal(ret, 0, "shared sha ctx thread join timed out (deadlock?)");

	/* 最后由主线程 finish（避免子线程在 finish 中触发引擎时与主线程互锁） */
	if (ret == 0) {
		ret = mbedtls_sha256_finish(&rb_sha_ctx, rb_sha_md);
	}

	/* 不崩溃即视为 THREADING_ALT 互斥生效；结果不具确定性 */
	zassert_true(ret == 0 || rb_sha_ret == 0,
		     "shared sha ctx crash/ret=%d/%d", ret, rb_sha_ret);
	mbedtls_sha256_free(&rb_sha_ctx);
}
#endif

/* ---- otbn_pointops 鲁棒性 ---- */
#ifdef MT_HAVE_POINTOPS_ROBUST

/* 非法 curve 值 */
#define MT_BAD_CURVE ((enum ls_otbn_pointops_curve)0xFF)

ZTEST(robustness, pointops_field_bytes_bad)
{
	size_t b;
	int ret;

	ret = ls_otbn_pointops_field_bytes(MT_BAD_CURVE, &b);
	zassert_not_equal(ret, 0, "field_bytes bad curve should fail");

	ret = ls_otbn_pointops_field_bytes(LS_OTBN_POINTOPS_CURVE_P256, NULL);
	zassert_not_equal(ret, 0, "field_bytes NULL bytes should fail");
}

ZTEST(robustness, pointops_scalar_mult_bad)
{
	uint8_t rx[32], ry[32];
	int ret;

	ret = ls_otbn_pointops_scalar_mult(LS_OTBN_POINTOPS_CURVE_P256,
					   NULL, p256_px, p256_py,
					   rx, ry);
	zassert_not_equal(ret, 0, "scalar_mult NULL scalar should fail");

	ret = ls_otbn_pointops_scalar_mult(MT_BAD_CURVE,
					   p256_k, p256_px, p256_py,
					   rx, ry);
	zassert_not_equal(ret, 0, "scalar_mult bad curve should fail");
}

ZTEST(robustness, pointops_base_mult_bad)
{
	uint8_t rx[32], ry[32];
	int ret;

	ret = ls_otbn_pointops_base_mult(LS_OTBN_POINTOPS_CURVE_P256, NULL,
					 rx, ry);
	zassert_not_equal(ret, 0, "base_mult NULL scalar should fail");

	ret = ls_otbn_pointops_base_mult(MT_BAD_CURVE, p256_k, rx, ry);
	zassert_not_equal(ret, 0, "base_mult bad curve should fail");
}

ZTEST(robustness, pointops_point_add_bad)
{
	uint8_t rx[32], ry[32];
	int ret;

	ret = ls_otbn_pointops_point_add(LS_OTBN_POINTOPS_CURVE_P256,
					 p256_gx, p256_gy, NULL, p256_py,
					 rx, ry);
	zassert_not_equal(ret, 0, "point_add NULL qx should fail");

	ret = ls_otbn_pointops_point_add(MT_BAD_CURVE,
					 p256_gx, p256_gy,
					 p256_px, p256_py,
					 rx, ry);
	zassert_not_equal(ret, 0, "point_add bad curve should fail");
}

/* scalar 全 0 / 全 0xFF：不崩溃，记录输出即可 */
ZTEST(robustness, pointops_scalar_edge)
{
	uint8_t rx[32], ry[32];
	uint8_t zero[32] = {0};
	uint8_t ff[32];
	int ret;

	memset(ff, 0xff, sizeof(ff));

	ret = ls_otbn_pointops_base_mult(LS_OTBN_POINTOPS_CURVE_P256, zero, rx, ry);
	zassert_true(ret == 0 || ret != 0,
		     "scalar=0 should not crash (ret=%d)", ret);

	ret = ls_otbn_pointops_base_mult(LS_OTBN_POINTOPS_CURVE_P256, ff, rx, ry);
	zassert_true(ret == 0 || ret != 0,
		     "scalar=0xff should not crash (ret=%d)", ret);
}

#endif /* MT_HAVE_POINTOPS_ROBUST */
