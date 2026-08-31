/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * mbedtls 单核测试公共实现：断言辅助 + RNG 回调。
 */

#include "mbedtls_test.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/ztest.h>

#include <mbedtls/bignum.h>

void mt_assert_mem_eq(const uint8_t *act, const uint8_t *exp, size_t len,
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

void mt_assert_mpi_eq(const struct mbedtls_mpi *mt, const struct mbedtls_mpi *exp,
		      size_t max_bytes, const char *where_fmt, ...)
{
	unsigned char act_buf[80];
	unsigned char exp_buf[80];
	char where[64];
	va_list ap;
	size_t len;
	int ret_act, ret_exp;

	ret_act = mbedtls_mpi_write_binary(mt, act_buf, sizeof(act_buf));
	ret_exp = mbedtls_mpi_write_binary(exp, exp_buf, sizeof(exp_buf));
	if (ret_act != 0 || ret_exp != 0) {
		zassert_false(ret_act || ret_exp,
			      "mpi write_binary failed act=%d exp=%d", ret_act, ret_exp);
		return;
	}

	len = max_bytes;
	for (size_t i = 0; i < len; i++) {
		if (act_buf[i] != exp_buf[i]) {
			va_start(ap, where_fmt);
			vsnprintf(where, sizeof(where), where_fmt, ap);
			va_end(ap);
			zassert_mem_equal(act_buf, exp_buf, len,
					  "%s: byte[%u] 0x%02x != 0x%02x",
					  where, (unsigned int)i,
					  act_buf[i], exp_buf[i]);
			return;
		}
	}
}

int mt_unhexify(unsigned char *obuf, const char *ibuf)
{
	unsigned char c, c2;
	int len = (int)(strlen(ibuf) / 2);

	while (*ibuf != 0) {
		c = *ibuf++;
		if (c >= '0' && c <= '9') {
			c -= '0';
		} else if (c >= 'a' && c <= 'f') {
			c -= 'a' - 10;
		} else if (c >= 'A' && c <= 'F') {
			c -= 'A' - 10;
		} else {
			return -1;
		}

		c2 = *ibuf++;
		if (c2 >= '0' && c2 <= '9') {
			c2 -= '0';
		} else if (c2 >= 'a' && c2 <= 'f') {
			c2 -= 'a' - 10;
		} else if (c2 >= 'A' && c2 <= 'F') {
			c2 -= 'A' - 10;
		} else {
			return -1;
		}

		*obuf++ = (c << 4) | c2;
	}

	return len;
}

/* ---- RNG 回调 ---- */

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

int mt_rng_trng(void *ctx, unsigned char *buf, size_t len)
{
	int rc;

	(void)ctx;

	if (trng_init() != 0) {
		return -ENODEV;
	}

	rc = entropy_get_entropy_isr(trng_dev, buf, len, ENTROPY_BUSYWAIT);
	return (rc < 0) ? rc : 0;
}

int mt_rng_fixed(void *ctx, unsigned char *buf, size_t size)
{
	static const uint8_t fixed[3] = { 0xcc, 0xbb, 0xaa };

	(void)ctx;

	/* 循环填充固定 pattern（确定性 k，签名可复现） */
	for (size_t i = 0; i < size; i++) {
		buf[i] = fixed[i % sizeof(fixed)];
	}
	return 0;
}

int mt_rng_lcg(void *ctx, unsigned char *buf, size_t len)
{
	static uint32_t state = 0x12345678U;

	(void)ctx;

	for (size_t i = 0; i < len; i++) {
		state = state * 1103515245U + 12345U;
		/* PKCS#1 v1.5 加密 padding 要求非零随机字节 */
		buf[i] = (unsigned char)(state | 0x01U);
	}
	return 0;
}

int mt_rng_kbuf(void *ctx, unsigned char *buf, size_t size)
{
	/* ctx 指明固定 buffer（定 k KAT：黄金 r/s 按此 k 算出） */
	memcpy(buf, ctx, size);
	return 0;
}
