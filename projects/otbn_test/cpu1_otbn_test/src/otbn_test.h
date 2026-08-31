/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN 单核测试公共头：公共 RNG + api 导出给 mix 复用的操作。
 *
 * 测试函数为纯 ztest：每项测试是 ZTEST(suite, name) 原生测试，断言用
 * zassert_*（失败详情 file:line+msg 由 zassert 自身 printk，harness 归因进
 * 报告 reason 列）；数值上报用 ZT_INFO 约定行（§13.4，断言之前）。结果记账
 * 由 common/ztest_glue 的 zt_run_all 自动完成。
 */

#ifndef OTBN_TEST_H
#define OTBN_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* §13.5 可归因逐字节比对：定位首个不同字节后交 zassert 打印
 * "byte[i] 0x实际 != 0x期望"（断言失败 = 本测试 FAIL，ztest 原生语义）。
 * where_fmt 是场景格式串（如 "sha256 KAT[%d]"），供归因定位向量下标。 */
void zassert_hash_eq(const uint8_t *act, const uint8_t *exp, size_t len,
		     const char *where_fmt, ...);

/* ---- 公共 RNG（api/mix 测试共用） ---- */

/* RSA keygen 专用：TRNG 真随机（entropy_get_entropy_isr BUSYWAIT）。
 * 候选素数生成不能用确定性 LCG（分布有偏找不到素数）。 */
int otbn_rsa_keygen_get_rng(void *ctx, unsigned char *buf, size_t len);

/* PKCS#1 v1.5 padding 用确定性 LCG（每字节强制非零）。 */
int otbn_test_rng(void *ctx, unsigned char *out, size_t len);

/* TRNG 阻塞取熵（ECDSA genkey 用）。 */
int otbn_get_random(void *ctx, unsigned char *buf, size_t size);

/* 固定输出（ECDSA 签名确定性 k，KAT 比对 r/s 用）。 */
int otbn_get_fixed(void *ctx, unsigned char *buf, size_t size);
int otbn_rnd_buf(void *ctx, unsigned char *buf, size_t size);   /* 从 ctx buffer 读定 k（KAT） */

/* ---- api 导出给 mix 复用的操作（混用测试的"另一侧"） ---- */
struct mbedtls_rsa_context;
int otbn_rsa_import_test_key(struct mbedtls_rsa_context *rsa);
int otbn_rsa_keygen_check(unsigned int nbits, uint32_t *elapsed_ms);
int otbn_ecdsa_curve_once(int curve);
uint32_t otbn_keygen_rng_calls(void);

#endif /* OTBN_TEST_H */
