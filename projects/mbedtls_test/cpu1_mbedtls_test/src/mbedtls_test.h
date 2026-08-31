/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * mbedtls 单核测试公共头：断言辅助 + RNG 回调 + 测量。
 *
 * 测试函数为纯 ztest：每项测试是 ZTEST(suite, name) 原生测试，断言用
 * zassert_*（失败详情 file:line+msg 由 zassert 自身 printk，harness 归因进
 * 报告 reason 列，§13.5）；数值上报用 ZT_INFO 约定行（§13.4）。结果记账
 * 由 common/ztest_glue 的 zt_run_all 自动完成（测试函数零 tr_record 样板）。
 */

#ifndef MBEDTLS_TEST_H
#define MBEDTLS_TEST_H

#include <stddef.h>
#include <stdint.h>

struct mbedtls_mpi;

/* ---- §13.5 可归因逐字节比对 ---- */
/* 定位首个不同字节后交 zassert 打印 "byte[i] 0x实际 != 0x期望"；
 * where_fmt 是场景描述（如 "sha256 KAT[%d]"），供归因定位向量下标。 */
void mt_assert_mem_eq(const uint8_t *act, const uint8_t *exp, size_t len,
		      const char *where_fmt, ...);

/* mbedtls_mpi 大数比对（write_binary 后走 mt_assert_mem_eq 语义）。
 * mt 与 exp 不相等时断言失败并给出首个不同字节。 */
void mt_assert_mpi_eq(const struct mbedtls_mpi *mt, const struct mbedtls_mpi *exp,
		      size_t max_bytes, const char *where_fmt, ...);

/* hex 大写字符串 → 二进制；返回字节数，非法字符返回 -1 */
int mt_unhexify(unsigned char *obuf, const char *ibuf);

/* ---- RNG 回调 ---- */

/* TRNG 真随机（RSA keygen 候选素数生成 / ECDSA genkey）。
 * 候选素数生成不能用确定性 LCG（分布有偏找不到素数）。 */
int mt_rng_trng(void *ctx, unsigned char *buf, size_t len);

/* 固定 pattern（ECDSA keygen+sign+verify 回环的确定性 k，签名可复现） */
int mt_rng_fixed(void *ctx, unsigned char *buf, size_t size);

/* PKCS#1 v1.5 加密 padding 用确定性 LCG（每字节强制非零） */
int mt_rng_lcg(void *ctx, unsigned char *buf, size_t len);

/* 从 ctx 指明的固定 buffer 读（定 k KAT：k_buf 作 ctx 传入） */
int mt_rng_kbuf(void *ctx, unsigned char *buf, size_t size);

#endif /* MBEDTLS_TEST_H */
