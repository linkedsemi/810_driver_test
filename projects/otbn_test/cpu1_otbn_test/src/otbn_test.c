/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN 单核测试公共 RNG（cpu1 安全域）。
 *
 * 测试为纯 ztest：main.c 打印角色 banner 后 zt_init + zt_run_all（ztest 原生
 * 框架按链接顺序跑完所有 ZTEST 套件），结果记账由 common/ztest_glue 自动完成。
 * 本文件只保留各测试共用的 RNG 回调。
 * selftest 广谱套件在兄弟工程 projects/otbn_selftest/（镜像体积拆分）。
 */

#include "otbn_test.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/ztest.h>

void zassert_hash_eq(const uint8_t *act, const uint8_t *exp, size_t len,
		     const char *where_fmt, ...)
{
	size_t i;

	for (i = 0; i < len && act[i] == exp[i]; i++) {
	}
	if (i == len) {
		return;
	}

	char where[64];
	va_list ap;

	va_start(ap, where_fmt);
	vsnprintf(where, sizeof(where), where_fmt, ap);
	va_end(ap);

	/* 4th 参是消息格式串（本树 ztest_assert.h）：reason 形如
	 * "hash not equal to exp: sha256 KAT[0]: byte[3] 0x12 != 0x34" */
	zassert_mem_equal(act, exp, len,
			  "%s: byte[%u] 0x%02x != 0x%02x",
			  where, (unsigned int)i, act[i], exp[i]);
}

static const struct device *trng_dev;

static int trng_init(void)
{
	if (trng_dev != NULL) {
		return 0;
	}

	trng_dev = DEVICE_DT_GET(DT_NODELABEL(trng1));
	if (!device_is_ready(trng_dev)) {
		printk("TRNG device not ready\n");
		trng_dev = NULL;
		return -ENODEV;
	}

	return 0;
}

static volatile uint32_t keygen_rng_call_count;

int otbn_rsa_keygen_get_rng(void *ctx, unsigned char *buf, size_t len)
{
	int rc;

	(void)ctx;

	keygen_rng_call_count++;

	if (trng_init() != 0) {
		return -ENODEV;
	}

	rc = entropy_get_entropy_isr(trng_dev, buf, len, ENTROPY_BUSYWAIT);
	return (rc < 0) ? rc : 0;
}

uint32_t otbn_keygen_rng_calls(void)
{
	return keygen_rng_call_count;
}

int otbn_test_rng(void *ctx, unsigned char *out, size_t len)
{
	static uint32_t state = 0x12345678;

	(void)ctx;

	for (size_t i = 0; i < len; i++) {
		state = state * 1103515245U + 12345U;
		/* PKCS#1 v1.5 padding requires non-zero random bytes. */
		out[i] = (unsigned char)(state | 0x01U);
	}
	return 0;
}

int otbn_get_random(void *ctx, unsigned char *buf, size_t size)
{
	(void)ctx;

	if (trng_init() != 0) {
		return -ENODEV;
	}
	return entropy_get_entropy(trng_dev, buf, size);
}

int otbn_get_fixed(void *ctx, unsigned char *buf, size_t size)
{
	static const uint8_t fixed[3] = { 0xcc, 0xbb, 0xaa };

	(void)ctx;

	/* 循环填充固定 pattern（deterministic ECDSA k） */
	for (size_t i = 0; i < size; i++) {
		buf[i] = fixed[i % sizeof(fixed)];
	}
	return 0;
}

int otbn_rnd_buf(void *ctx, unsigned char *buf, size_t size)
{
	/* 从 ctx 指明的固定 buffer 读 k（ECDSA 定 k KAT）：
	 * 与 samples/mbedtls_test 的 ls_mbedtls_get_fixed 同语义
	 * （memcpy(buf, fix_trng_output, size)）。 */
	memcpy(buf, ctx, size);
	return 0;
}
