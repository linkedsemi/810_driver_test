/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * mbedtls 测试协议头（tests/include/）：
 *   1. 测试项枚举（文档性：实际记账索引 = ztest 链接段 SORT_BY_NAME 序，
 *      与 scripts/dump_results.py 的 unit_test_names() 自动对齐，§10.27）
 *   2. 预期向量头 include（向量本体在 tests/expected/，§13.2）
 *
 * 本工程是 mbedtls API 级测试（无跨核 opcode 协议），故此处只承载
 * 测试项清单与向量集合，不含 token/opcode 宏（ztest 原生输出即协议）。
 */
#ifndef MBEDTLS_TEST_PROTOCOL_H
#define MBEDTLS_TEST_PROTOCOL_H

/*
 * 测试项全集（各 case 经 prj.conf 路由后子集编译，条件编译门控见各 .c）：
 *   hash   sha224_kat / sha256_kat / sha384_kat / sha512_kat / sm3_kat
 *   cipher aes128_cbc / sm4_ecb(SM4 HW) / sm4_cbc(SM4 HW)
 *   ecc    p256_kat / p256_keygen / p384_keygen / sm2_keygen(SM2 curve)
 *   rsa    rsa2048_modexp / rsa2048_keygen
 */
enum mbedtls_test_item {
	MT_T_SHA224_KAT = 0,
	MT_T_SHA256_KAT,
	MT_T_SHA384_KAT,
	MT_T_SHA512_KAT,
	MT_T_SM3_KAT,
	MT_T_AES128_CBC,
	MT_T_SM4_ECB,
	MT_T_SM4_CBC,
	MT_T_P256_KAT,
	MT_T_P256_KEYGEN,
	MT_T_P384_KEYGEN,
	MT_T_SM2_KEYGEN,
	MT_T_RSA2048_MODEXP,
	MT_T_RSA2048_KEYGEN,
	MT_T_COUNT
};

/* 确定性测试预期向量（§13.2：来源注释在各头文件内） */
#include "hash_vectors.h"
#include "cipher_vectors.h"
#include "ecdsa_vectors.h"
#include "rsa_vectors.h"

#endif /* MBEDTLS_TEST_PROTOCOL_H */
