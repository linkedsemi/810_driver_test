/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN 多算法并发线程测试。
 *
 * 仅在 case_all_on_max 执行（用唯一组合宏识别：THREADING_ALT +
 * OTBN RSA + AES HW + SM4 HW），避免矩阵污染。
 * 线程 A：反复跑 SHA-256 OTBN 长报文；线程 B：反复跑 ECDSA P-256 OTBN sign。
 * 验证 THREADING_ALT 下 OTBN session 互斥不崩溃、输出完整。
 */

#include "mbedtls_test_protocol.h"
#include "mbedtls_test.h"
#include "ztest_glue.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <mbedtls/sha256.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>

#if defined(CONFIG_MBEDTLS_THREADING_LINKEDSEMI_ALT) && \
	defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT) && \
	defined(CONFIG_MBEDTLS_CIPHER_AES_LINKEDSEMI) && \
	defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
#define MT_HAVE_THREAD_TEST 1
#endif

#ifdef MT_HAVE_THREAD_TEST

ZTEST_SUITE(thread, NULL, NULL, NULL, NULL, NULL);

#define MT_THREAD_LOOPS  5
#define MT_THREAD_STACK  8192

static K_THREAD_STACK_DEFINE(sha_stack, MT_THREAD_STACK);
static K_THREAD_STACK_DEFINE(ecdsa_stack, MT_THREAD_STACK);
static struct k_thread sha_thread;
static struct k_thread ecdsa_thread;

static volatile int sha_thread_ret;
static volatile int ecdsa_thread_ret;

static const unsigned char thread_msg[] = "thread mix sha256 payload";

static void sha_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	int ret = 0;
	unsigned char md[32];

	for (int i = 0; i < MT_THREAD_LOOPS && ret == 0; i++) {
		mbedtls_sha256_context ctx;

		mbedtls_sha256_init(&ctx);
		ret = mbedtls_sha256_starts(&ctx, 0);
		if (ret == 0) {
			ret = mbedtls_sha256_update(&ctx, thread_msg, sizeof(thread_msg) - 1);
		}
		if (ret == 0) {
			ret = mbedtls_sha256_finish(&ctx, md);
		}
		mbedtls_sha256_free(&ctx);
	}

	sha_thread_ret = ret;
}

static void ecdsa_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	int ret = 0;
	mbedtls_ecp_group grp;
	mbedtls_ecp_point Q;
	mbedtls_mpi d, r, s;
	unsigned char hash[32];

	mbedtls_ecp_group_init(&grp);
	mbedtls_ecp_point_init(&Q);
	mbedtls_mpi_init(&d);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
	if (ret == 0) {
		ret = mbedtls_ecp_gen_keypair(&grp, &d, &Q, mt_rng_trng, NULL);
	}

	memset(hash, 0xaa, sizeof(hash));

	for (int i = 0; i < MT_THREAD_LOOPS && ret == 0; i++) {
		ret = mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, sizeof(hash),
				       mt_rng_fixed, NULL);
		if (ret == 0) {
			ret = mbedtls_ecdsa_verify(&grp, hash, sizeof(hash),
						   &Q, &r, &s);
		}
	}

	mbedtls_ecp_group_free(&grp);
	mbedtls_ecp_point_free(&Q);
	mbedtls_mpi_free(&d);
	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);

	ecdsa_thread_ret = ret;
}

ZTEST(thread, concurrent_otbn_hash_ecc)
{
	sha_thread_ret = -1;
	ecdsa_thread_ret = -1;

	k_thread_create(&sha_thread, sha_stack, K_THREAD_STACK_SIZEOF(sha_stack),
			sha_thread_fn, NULL, NULL, NULL,
			K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	k_thread_create(&ecdsa_thread, ecdsa_stack, K_THREAD_STACK_SIZEOF(ecdsa_stack),
			ecdsa_thread_fn, NULL, NULL, NULL,
			K_PRIO_PREEMPT(1), 0, K_NO_WAIT);

	k_thread_join(&sha_thread, K_FOREVER);
	k_thread_join(&ecdsa_thread, K_FOREVER);

	ZT_INFO("concurrent_otbn_hash_ecc", "loops=%d,sha_ret=%d,ecdsa_ret=%d",
		MT_THREAD_LOOPS, sha_thread_ret, ecdsa_thread_ret);
	zassert_equal(sha_thread_ret, 0, "sha thread ret=%d", sha_thread_ret);
	zassert_equal(ecdsa_thread_ret, 0, "ecdsa thread ret=%d", ecdsa_thread_ret);
}

#endif /* MT_HAVE_THREAD_TEST */
