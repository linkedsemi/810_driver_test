/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 全 case 公共并发线程安全测试。
 *
 * THREADING_ALT 是基座配置（prj.conf 全局开启）。
 * 本 suite 在每个 case 都编译，线程 A/B 各自使用独立上下文，
 * 同时调用当前 case 已启用的算法路径，验证互斥不崩溃、输出完整。
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

ZTEST_SUITE(mbedtls_thread_safety, NULL, NULL, NULL, NULL, NULL);

#define MT_CONC_LOOPS     5
#define MT_CONC_STACK     8192
#define MT_CONC_PRIO      K_PRIO_PREEMPT(-1)

static K_THREAD_STACK_DEFINE(conc_sha_stack, MT_CONC_STACK);
static K_THREAD_STACK_DEFINE(conc_ecdsa_stack, MT_CONC_STACK);
static struct k_thread conc_sha_thread;
static struct k_thread conc_ecdsa_thread;
static volatile int conc_sha_ret;
static volatile int conc_ecdsa_ret;

static const unsigned char conc_msg[] = "concurrent hash payload";

static void conc_sha_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	int ret = 0;
	unsigned char md[32];

	for (int i = 0; i < MT_CONC_LOOPS && ret == 0; i++) {
		mbedtls_sha256_context ctx;

		mbedtls_sha256_init(&ctx);
		ret = mbedtls_sha256_starts(&ctx, 0);
		if (ret == 0) {
			ret = mbedtls_sha256_update(&ctx, conc_msg, sizeof(conc_msg) - 1);
		}
		if (ret == 0) {
			ret = mbedtls_sha256_finish(&ctx, md);
		}
		mbedtls_sha256_free(&ctx);
	}

	conc_sha_ret = ret;
}

static void conc_ecdsa_thread_fn(void *a, void *b, void *c)
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

	memset(hash, 0xcc, sizeof(hash));

	for (int i = 0; i < MT_CONC_LOOPS && ret == 0; i++) {
		ret = mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, sizeof(hash),
					 mt_rng_fixed, NULL);
		if (ret == 0) {
			ret = mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &Q, &r, &s);
		}
	}

	mbedtls_ecp_group_free(&grp);
	mbedtls_ecp_point_free(&Q);
	mbedtls_mpi_free(&d);
	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);

	conc_ecdsa_ret = ret;
}

ZTEST(mbedtls_thread_safety, concurrent_hash_ecdsa)
{
	conc_sha_ret = -1;
	conc_ecdsa_ret = -1;

	k_thread_create(&conc_sha_thread, conc_sha_stack,
			K_THREAD_STACK_SIZEOF(conc_sha_stack),
			conc_sha_thread_fn, NULL, NULL, NULL,
			MT_CONC_PRIO, 0, K_NO_WAIT);
	k_thread_create(&conc_ecdsa_thread, conc_ecdsa_stack,
			K_THREAD_STACK_SIZEOF(conc_ecdsa_stack),
			conc_ecdsa_thread_fn, NULL, NULL, NULL,
			MT_CONC_PRIO, 0, K_NO_WAIT);

	k_thread_join(&conc_sha_thread, K_FOREVER);
	k_thread_join(&conc_ecdsa_thread, K_FOREVER);

	ZT_INFO("concurrent_hash_ecdsa", "loops=%d,sha_ret=%d,ecdsa_ret=%d",
		MT_CONC_LOOPS, conc_sha_ret, conc_ecdsa_ret);
	zassert_equal(conc_sha_ret, 0, "sha thread ret=%d", conc_sha_ret);
	zassert_equal(conc_ecdsa_ret, 0, "ecdsa thread ret=%d", conc_ecdsa_ret);
}
