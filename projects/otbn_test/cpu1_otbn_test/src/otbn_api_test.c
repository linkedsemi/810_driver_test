/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2：OTBN 适配接口测试（纯 ztest）。
 *
 * 测试内容移植自：
 *   - samples/mbedtls_test/src/main.c      （sha/sm3/aes/sm4 KAT、rsa2048 KAT、keygen）
 *   - samples/mbedtls_test/src/ecdsa_test.c（ecdsa p256 定 k KAT、p384/sm2 genkey+sign+verify）
 *   - samples/mbedtls_throughput/src/main.c（sha256/rsa/ecdsa 性能测量，INFO 上报）
 *
 * 与原 sample 的差异：printf 判定改为 ztest 原生断言（zassert_*，失败详情
 * file:line+msg 由 zassert 自身 printk，harness 归因进报告 reason 列），
 * NIST 向量逐字节比对走 zassert_hash_eq（§13.5 可归因）；性能数值按 §13.4
 * 打 ZT_INFO 约定行。结果记账由 common/ztest_glue 的 zt_run_all 自动完成。
 *
 * 套件划分（套件/测试均按链接段 SORT_BY_NAME 升序执行，非源码序）：
 *   api      OTBN 适配接口：aes/sm4 硬件引擎 KAT、rsa2048 KAT、keygen 2048/3072、
 *           hash KAT（aes < rsa < sha 字母序，同阶段内先 rsa 后 hash）
 *   mix      OTBN 固件混用复现（mix_test.c，字母序在 api 之后）
 *   perf     ECDSA KAT/p384/sm2 + 性能测量（api 阶段晚段内容，字母序在 mix 之后）
 *   z_keygen RSA-4096 keygen（耗时最长且疑似挂死待定位，放套件末尾，字母序最后）
 */

#include "otbn_test.h"
#include "ztest_glue.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/sm3_alt.h"
#include "mbedtls/aes.h"

ZTEST_SUITE(api, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(perf, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(z_keygen, NULL, NULL, NULL, NULL, NULL);

typedef struct testVector {
    const char*  input;
    const char*  output;
    size_t inLen;
    size_t outLen;
} testVector;

#define SHA224_DIGEST_SIZE 28
#define SHA256_DIGEST_SIZE 32
#define SM3_DIGEST_SIZE 32
#define SHA384_DIGEST_SIZE 48
#define SHA512_DIGEST_SIZE 64

/* ========================================================================
 * SHA-224/256/SM3/SHA-384/512 KAT（mbedtls_test 原样向量）
 * ======================================================================== */
static int test_sha224(void)
{
    int ret = 0;
    mbedtls_sha256_context sha[3];
    bool is224 = true;

    uint8_t      hash[SHA224_DIGEST_SIZE];

    testVector a, b, c;
    testVector test_sha[3];
    int times = sizeof(test_sha) / sizeof(struct testVector), i, j;

    a.input  = "";
    a.output = "\xd1\x4a\x02\x8c\x2a\x3a\x2b\xc9\x47\x61\x02\xbb\x28\x82\x34"
               "\xc4\x15\xa2\xb0\x1f\x82\x8e\xa6\x2a\xc5\xb3\xe4\x2f";
    a.inLen  = strlen(a.input);
    a.outLen = SHA224_DIGEST_SIZE;

    b.input  = "abc";
    b.output = "\x23\x09\x7d\x22\x34\x05\xd8\x22\x86\x42\xa4\x77\xbd\xa2\x55"
               "\xb3\x2a\xad\xbc\xe4\xbd\xa0\xb3\xf7\xe3\x6c\x9d\xa7";
    b.inLen  = strlen(b.input);
    b.outLen = SHA224_DIGEST_SIZE;

    c.input  = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    c.output = "\x75\x38\x8b\x16\x51\x27\x76\xcc\x5d\xba\x5d\xa1\xfd\x89\x01"
               "\x50\xb0\xc6\x45\x5c\xb4\xf5\x8b\x19\x52\x52\x25\x25";
    c.inLen  = strlen(c.input);
    c.outLen = SHA224_DIGEST_SIZE;

    test_sha[0] = a;
    test_sha[1] = b;
    test_sha[2] = c;

    for (i = 0; i < times; ++i) {
        mbedtls_sha256_init(&sha[i]);

        if ((ret = mbedtls_sha256_starts(&sha[i], is224)) != 0) {
            goto exit;
        }

        if ((ret = mbedtls_sha256_update(&sha[i], (char*)test_sha[i].input, (size_t)test_sha[i].inLen)) != 0) {
            goto exit;
        }

        if ((ret = mbedtls_sha256_finish(&sha[i], hash)) != 0) {
            goto exit;
        }

        zassert_hash_eq(hash, (const uint8_t *)test_sha[i].output,
                        SHA224_DIGEST_SIZE, "sha224 KAT[%d]", i);
    }

exit:
    for(j = 0; j < i; ++j)
    {
        mbedtls_sha256_free(&sha[j]);
    }
    return ret;
}

static int test_sha256(void)
{
    int ret = 0;
    mbedtls_sha256_context sha[4];
    bool is224 = false;

    uint8_t      hash[SHA256_DIGEST_SIZE];

    testVector a, b, c, d;
    testVector test_sha[4];

    int times = sizeof(test_sha) / sizeof(struct testVector), i, j;

    a.input  = "";
    a.output = "\xe3\xb0\xc4\x42\x98\xfc\x1c\x14\x9a\xfb\xf4\xc8\x99\x6f\xb9"
               "\x24\x27\xae\x41\xe4\x64\x9b\x93\x4c\xa4\x95\x99\x1b\x78\x52"
               "\xb8\x55";
    a.inLen  = strlen(a.input);
    a.outLen = SHA256_DIGEST_SIZE;

    b.input  = "abc";
    b.output = "\xBA\x78\x16\xBF\x8F\x01\xCF\xEA\x41\x41\x40\xDE\x5D\xAE\x22"
               "\x23\xB0\x03\x61\xA3\x96\x17\x7A\x9C\xB4\x10\xFF\x61\xF2\x00"
               "\x15\xAD";
    b.inLen  = strlen(b.input);
    b.outLen = SHA256_DIGEST_SIZE;

    c.input  = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    c.output = "\x24\x8D\x6A\x61\xD2\x06\x38\xB8\xE5\xC0\x26\x93\x0C\x3E\x60"
               "\x39\xA3\x3C\xE4\x59\x64\xFF\x21\x67\xF6\xEC\xED\xD4\x19\xDB"
               "\x06\xC1";
    c.inLen  = strlen(c.input);
    c.outLen = SHA256_DIGEST_SIZE;

    d.input  = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
               "aaaaaa"; /* this is BLOCKSIZE length */
    d.output = "\xFF\xE0\x54\xFE\x7A\xE0\xCB\x6D\xC6\x5C\x3A\xF9\xB6\x1D\x52"
               "\x09\xF4\x39\x85\x1D\xB4\x3D\x0B\xA5\x99\x73\x37\xDF\x15\x46"
               "\x68\xEB";
    d.inLen  = strlen(d.input);
    d.outLen = SHA256_DIGEST_SIZE;

    test_sha[0] = a;
    test_sha[1] = b;
    test_sha[2] = c;
    test_sha[3] = d;

    for (i = 0; i < times; ++i) {
        mbedtls_sha256_init(&sha[i]);

        if ((ret = mbedtls_sha256_starts(&sha[i], is224)) != 0) {
            goto exit;
        }

        if ((ret = mbedtls_sha256_update(&sha[i], (char*)test_sha[i].input, (size_t)test_sha[i].inLen)) != 0) {
            goto exit;
        }

        if ((ret = mbedtls_sha256_finish(&sha[i], hash)) != 0) {
            goto exit;
        }

        zassert_hash_eq(hash, (const uint8_t *)test_sha[i].output,
                        SHA256_DIGEST_SIZE, "sha256 KAT[%d]", i);
    }

exit:
    for(j = 0; j < i; ++j)
    {
        mbedtls_sha256_free(&sha[j]);
    }
    return ret;
}

static int test_sm3(void)
{
    mbedtls_sm3_context sm3[3];
    uint8_t   hash[SM3_DIGEST_SIZE];
    int ret = 0;

    testVector a, b, c;
    testVector test_sm3[3];
    int times = sizeof(test_sm3) / sizeof(struct testVector), i, j;

    a.input  = "";
    a.output = "\x1a\xb2\x1d\x83\x55\xcf\xa1\x7f\x8e\x61\x19\x48\x31\xe8\x1a"
               "\x8f\x22\xbe\xc8\xc7\x28\xfe\xfb\x74\x7e\xd0\x35\xeb\x50\x82"
               "\xaa\x2b";
    a.inLen  = strlen(a.input);
    a.outLen = SM3_DIGEST_SIZE;

    b.input  = "abc";
    b.output = "\x66\xc7\xf0\xf4\x62\xee\xed\xd9\xd1\xf2\xd4\x6b\xdc\x10\xe4"
               "\xe2\x41\x67\xc4\x87\x5c\xf2\xf7\xa2\x29\x7d\xa0\x2b\x8f\x4b"
               "\xa8\xe0";
    b.inLen  = strlen(b.input);
    b.outLen = SM3_DIGEST_SIZE;

    c.input  = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    c.output = "\x63\x9b\x6c\xc5\xe6\x4d\x9e\x37\xa3\x90\xb1\x92\xdf\x4f\xa1"
               "\xea\x07\x20\xab\x74\x7f\xf6\x92\xb9\xf3\x8c\x4e\x66\xad\x7b"
               "\x8c\x05";
    c.inLen  = strlen(c.input);
    c.outLen = SM3_DIGEST_SIZE;

    test_sm3[0] = a;
    test_sm3[1] = b;
    test_sm3[2] = c;

    /* Test all the KATs. */
    for (i = 0; i < times; ++i) {

        mbedtls_sm3_init(&sm3[i]);

        if ((ret = mbedtls_sm3_starts(&sm3[i])) != 0) {
            goto exit;
        }

        if ((ret = mbedtls_sm3_update(&sm3[i], (char*)test_sm3[i].input, (size_t)test_sm3[i].inLen)) != 0) {
            goto exit;
        }

        if ((ret = mbedtls_sm3_finish(&sm3[i], hash)) != 0) {
            goto exit;
        }

        zassert_hash_eq(hash, (const uint8_t *)test_sm3[i].output,
                        SM3_DIGEST_SIZE, "sm3 KAT[%d]", i);
    }
exit:
    for(j = 0; j < i; ++j)
    {
        mbedtls_sm3_free(&sm3[j]);
    }

    return ret;
}

static int test_sha384(void)
{
    mbedtls_sha512_context sha[3];
    bool is384 = true;
    uint8_t      hash[SHA384_DIGEST_SIZE];
    uint8_t ret = 0;
    testVector a, b, c;
    testVector test_sha[3];
    int times = sizeof(test_sha) / sizeof(struct testVector), i, j;

    __attribute__((aligned(4))) uint8_t a_input[] = "";
    __attribute__((aligned(4))) uint8_t b_input[] = "abc";
    __attribute__((aligned(4))) uint8_t c_input[] = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

    a.input = (char *)a_input;
    a.output = "\x38\xb0\x60\xa7\x51\xac\x96\x38\x4c\xd9\x32\x7e\xb1\xb1\xe3"
               "\x6a\x21\xfd\xb7\x11\x14\xbe\x07\x43\x4c\x0c\xc7\xbf\x63\xf6"
               "\xe1\xda\x27\x4e\xde\xbf\xe7\x6f\x65\xfb\xd5\x1a\xd2\xf1\x48"
               "\x98\xb9\x5b";
    a.inLen  = strlen(a.input);
    a.outLen = SHA384_DIGEST_SIZE;

    b.input = (char *)b_input;
    b.output = "\xcb\x00\x75\x3f\x45\xa3\x5e\x8b\xb5\xa0\x3d\x69\x9a\xc6\x50"
               "\x07\x27\x2c\x32\xab\x0e\xde\xd1\x63\x1a\x8b\x60\x5a\x43\xff"
               "\x5b\xed\x80\x86\x07\x2b\xa1\xe7\xcc\x23\x58\xba\xec\xa1\x34"
               "\xc8\x25\xa7";
    b.inLen  = strlen(b.input);
    b.outLen = SHA384_DIGEST_SIZE;

    c.input = (char *)c_input;
    c.output = "\x09\x33\x0c\x33\xf7\x11\x47\xe8\x3d\x19\x2f\xc7\x82\xcd\x1b"
               "\x47\x53\x11\x1b\x17\x3b\x3b\x05\xd2\x2f\xa0\x80\x86\xe3\xb0"
               "\xf7\x12\xfc\xc7\xc7\x1a\x55\x7e\x2d\xb9\x66\xc3\xe9\xfa\x91"
               "\x74\x60\x39";
    c.inLen  = strlen(c.input);
    c.outLen = SHA384_DIGEST_SIZE;

    test_sha[0] = a;
    test_sha[1] = b;
    test_sha[2] = c;

    for (i = 0; i < times; ++i) {

        mbedtls_sha512_init(&sha[i]);

        if ((ret = mbedtls_sha512_starts(&sha[i], is384)) != 0) {
            goto exit;
        }

        if ((ret = mbedtls_sha512_update(&sha[i], (char*)test_sha[i].input, (size_t)test_sha[i].inLen)) != 0) {
            goto exit;
        }

        if ((ret = mbedtls_sha512_finish(&sha[i], hash)) != 0) {
            goto exit;
        }

        zassert_hash_eq(hash, (const uint8_t *)test_sha[i].output,
                        SHA384_DIGEST_SIZE, "sha384 KAT[%d]", i);
    }
exit:
    for(j = 0; j < i; ++j)
    {
        mbedtls_sha512_free(&sha[j]);
    }
    return ret;
}

static int test_sha512(void)
{
    mbedtls_sha512_context sha[3];
    bool is384 = false;
    uint8_t      hash[SHA512_DIGEST_SIZE];
    int ret = 0;
    testVector a, b, c;
    testVector test_sha[3];
    int times = sizeof(test_sha) / sizeof(struct testVector), i,j;
    __attribute__((aligned(4))) uint8_t a_input[] = "";
    __attribute__((aligned(4))) uint8_t b_input[] = "abc";
    __attribute__((aligned(4))) uint8_t c_input[] = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

    a.input  = (char *)a_input;
    a.output = "\xcf\x83\xe1\x35\x7e\xef\xb8\xbd\xf1\x54\x28\x50\xd6\x6d\x80"
               "\x07\xd6\x20\xe4\x05\x0b\x57\x15\xdc\x83\xf4\xa9\x21\xd3\x6c"
               "\xe9\xce\x47\xd0\xd1\x3c\x5d\x85\xf2\xb0\xff\x83\x18\xd2\x87"
               "\x7e\xec\x2f\x63\xb9\x31\xbd\x47\x41\x7a\x81\xa5\x38\x32\x7a"
               "\xf9\x27\xda\x3e";
    a.inLen  = strlen(a.input);
    a.outLen = SHA512_DIGEST_SIZE;

    b.input  = (char *)b_input;
    b.output = "\xdd\xaf\x35\xa1\x93\x61\x7a\xba\xcc\x41\x73\x49\xae\x20\x41"
               "\x31\x12\xe6\xfa\x4e\x89\xa9\x7e\xa2\x0a\x9e\xee\xe6\x4b\x55"
               "\xd3\x9a\x21\x92\x99\x2a\x27\x4f\xc1\xa8\x36\xba\x3c\x23\xa3"
               "\xfe\xeb\xbd\x45\x4d\x44\x23\x64\x3c\xe8\x0e\x2a\x9a\xc9\x4f"
               "\xa5\x4c\xa4\x9f";
    b.inLen  = strlen(b.input);
    b.outLen = SHA512_DIGEST_SIZE;

    c.input  = (char *)c_input;
    c.output = "\x8e\x95\x9b\x75\xda\xe3\x13\xda\x8c\xf4\xf7\x28\x14\xfc\x14"
               "\x3f\x8f\x77\x79\xc6\xeb\x9f\x7f\xa1\x72\x99\xae\xad\xb6\x88"
               "\x90\x18\x50\x1d\x28\x9e\x49\x00\xf7\xe4\x33\x1b\x99\xde\xc4"
               "\xb5\x43\x3a\xc7\xd3\x29\xee\xb6\xdd\x26\x54\x5e\x96\xe5\x5b"
               "\x87\x4b\xe9\x09";
    c.inLen  = strlen(c.input);
    c.outLen = SHA512_DIGEST_SIZE;

    test_sha[0] = a;
    test_sha[1] = b;
    test_sha[2] = c;

    for (i = 0; i < times; ++i) {

        mbedtls_sha512_init(&sha[i]);

        if ((ret = mbedtls_sha512_starts(&sha[i], is384)) != 0) {
            goto exit;
        }

        if ((ret = mbedtls_sha512_update(&sha[i], (char*)test_sha[i].input, (size_t)test_sha[i].inLen)) != 0) {
            goto exit;
        }

        if ((ret = mbedtls_sha512_finish(&sha[i], hash)) != 0) {
            goto exit;
        }

        zassert_hash_eq(hash, (const uint8_t *)test_sha[i].output,
                        SHA512_DIGEST_SIZE, "sha512 KAT[%d]", i);
    }
exit:
    for(j = 0; j < i; ++j)
    {
        mbedtls_sha512_free(&sha[j]);
    }
    return ret;
}

/* 多长度 + 多偏移 + 单/多 update 混合（mbedtls_test 原样，覆盖 OTBN hash 的
 * remain_data 拼凑 / DMEM 批量 / 收尾路径） */
static int test_sha256_various_lengths(void)
{
    static const struct {
        size_t len;
        const uint8_t hash[SHA256_DIGEST_SIZE];
    } cases[] = {
        {0,    {0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55}},
        {1,    {0xdb,0xc1,0xb4,0xc9,0x00,0xff,0xe4,0x8d,0x57,0x5b,0x5d,0xa5,0xc6,0x38,0x04,0x01,0x25,0xf6,0x5d,0xb0,0xfe,0x3e,0x24,0x49,0x4b,0x76,0xea,0x98,0x64,0x57,0xd9,0x86}},
        {63,   {0x2b,0x3c,0x7e,0x3b,0xb2,0x6b,0xdf,0xda,0xf5,0x36,0xeb,0x40,0x69,0x83,0xd9,0x3b,0x82,0x2e,0x8c,0xae,0x5e,0x6a,0x6d,0xdd,0x39,0x64,0xe4,0x10,0x71,0x9e,0x02,0xce}},
        {64,   {0xf8,0x3b,0x33,0x2b,0xe4,0xe6,0xa5,0xa4,0xb1,0xc5,0x6a,0xaf,0x6d,0xb5,0x26,0x57,0xda,0x49,0x5e,0x14,0x98,0x70,0x05,0x7d,0x85,0x90,0xab,0x9d,0x7a,0x61,0x67,0xad}},
        {65,   {0x64,0xf0,0x23,0xa6,0xcb,0x9d,0x76,0x68,0xa0,0x25,0x88,0xce,0x41,0xfe,0xc6,0x4b,0x78,0xb4,0x14,0xf4,0xb3,0x14,0xfc,0x51,0xe7,0x3c,0x94,0x5c,0x7f,0x9e,0xc2,0x02}},
        {127,  {0xa7,0x96,0xc8,0x8a,0xad,0x57,0xd7,0x87,0xae,0x4e,0xfd,0x0f,0xf2,0xff,0x81,0x27,0x58,0xb0,0xd9,0xa4,0x03,0x46,0x14,0xf0,0xae,0x89,0x61,0x9c,0xec,0x1a,0xab,0x7f}},
        {128,  {0x9e,0x50,0xf9,0x0f,0xd0,0x35,0x92,0x4a,0x20,0x2c,0x1f,0x5c,0x52,0x46,0xac,0xc4,0x0a,0xb0,0xc3,0x21,0x13,0x66,0xe8,0x9f,0x0c,0x5d,0x9c,0xf4,0x2a,0x6a,0x8d,0x78}},
        {255,  {0x59,0xc9,0x0b,0x02,0x06,0xa4,0xc3,0x89,0xee,0x90,0xb0,0x64,0xce,0xbd,0xac,0x8f,0x76,0x63,0xbc,0xb7,0x8c,0xae,0x3e,0x23,0xc5,0xad,0xc4,0x84,0xfa,0x23,0xe2,0x48}},
        {256,  {0xf5,0xc2,0x2e,0x35,0xd0,0x41,0x67,0xe3,0x79,0x13,0xe7,0x96,0x3c,0xe0,0x33,0xb1,0xf3,0xd1,0x7a,0x92,0x4a,0x4e,0x6f,0xe5,0xfc,0x95,0xaf,0x12,0x24,0x05,0x19,0x21}},
        {511,  {0xce,0x8e,0xff,0x1b,0xec,0xc2,0x34,0xa0,0x20,0x02,0xfe,0x67,0x38,0x1a,0x98,0x60,0x8b,0x01,0x20,0x84,0x0a,0x9a,0x35,0x51,0x6d,0x75,0xcf,0x3c,0x00,0x5e,0x5b,0x1e}},
        {512,  {0x1b,0xd8,0xd0,0x4b,0xb1,0x27,0xc9,0xdb,0xdb,0x40,0x6c,0x3c,0x21,0x3b,0x20,0x2c,0x84,0xdc,0x75,0xf1,0x22,0xec,0xd5,0x53,0xb0,0x2f,0xb3,0x22,0xbf,0xfa,0xfd,0xc9}},
        {1023, {0xd2,0x9d,0x01,0x2b,0x4c,0xee,0xa4,0xa1,0xcc,0x96,0xff,0xfa,0x14,0x0c,0x14,0x0b,0x36,0x4f,0x2a,0x3d,0x03,0x48,0x8d,0xd6,0x38,0x4f,0xf9,0x9a,0x14,0xde,0x8d,0x34}},
        /* sha256(1024 x 0x02) 真实值（PC 侧 hashlib 复核；样本原表此行多出
         * 一个字节 0xee 导致 33 元素告警且前 32B 错位，从未真正通过过） */
        {1024, {0x14,0xd6,0xfc,0x84,0x87,0x12,0x81,0x5b,0xc1,0xb5,0xfe,0x1c,0xed,0x1b,0x89,0x80,0xee,0xa1,0xe0,0xdb,0x78,0x1a,0x94,0x6d,0xac,0x5a,0xde,0xd9,0x76,0x9d,0x19,0x84}},
        {16257,{0x79,0x14,0x3e,0x04,0xbf,0xb7,0x6e,0xce,0xf7,0xd6,0xdf,0xdf,0x69,0xeb,0x3a,0xd2,0xde,0x49,0xa5,0xbc,0x41,0xd0,0x25,0xfc,0x0c,0x5d,0x52,0x30,0x2c,0x46,0xb6,0x7b}},
    };

    __attribute__((aligned(32))) static uint8_t big_buffer[16260];
    memset(big_buffer, 0x2, sizeof(big_buffer));

    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        for (int off = 0; off < 4; ++off) {
            int ret;
            mbedtls_sha256_context ctx;
            uint8_t hash[SHA256_DIGEST_SIZE];

            /* Single-update reference for this (len, offset). */
            mbedtls_sha256_init(&ctx);
            ret = mbedtls_sha256_starts(&ctx, false);
            if (ret == 0) {
                ret = mbedtls_sha256_update(&ctx, big_buffer + off, cases[c].len);
            }
            if (ret == 0) {
                ret = mbedtls_sha256_finish(&ctx, hash);
            }
            mbedtls_sha256_free(&ctx);
            if (ret != 0) {
                printk("SHA-256 various: init/single-update failed len=%zu off=%d ret=%d\n",
                       cases[c].len, off, ret);
                return ret;
            }
            zassert_hash_eq(hash, cases[c].hash, SHA256_DIGEST_SIZE,
                            "varlen single-update len=%u off=%d",
                            (unsigned int)cases[c].len, off);

            /* Multi-update mixed test: 1 + 63 + 64 + remainder. */
            mbedtls_sha256_init(&ctx);
            ret = mbedtls_sha256_starts(&ctx, false);
            size_t remain = cases[c].len;
            uint8_t *p = big_buffer + off;
            size_t steps[] = {1, 63, 64, remain};
            for (size_t s = 0; ret == 0 && s < sizeof(steps) / sizeof(steps[0]) && remain > 0; ++s) {
                size_t n = steps[s];
                if (n > remain) {
                    n = remain;
                }
                if (n == 0) {
                    continue;
                }
                ret = mbedtls_sha256_update(&ctx, p, n);
                p += n;
                remain -= n;
            }
            if (ret == 0) {
                ret = mbedtls_sha256_finish(&ctx, hash);
            }
            mbedtls_sha256_free(&ctx);
            if (ret != 0) {
                printk("SHA-256 various: multi-update failed len=%zu off=%d ret=%d\n",
                       cases[c].len, off, ret);
                return ret;
            }
            zassert_hash_eq(hash, cases[c].hash, SHA256_DIGEST_SIZE,
                            "varlen multi-update len=%u off=%d",
                            (unsigned int)cases[c].len, off);
        }
    }

    return 0;
}

/* ========================================================================
 * AES KAT（硬件 AES 引擎，mbedtls_test 原样向量）
 * ======================================================================== */
#define MTLS_AES_192
#define MTLS_AES_256
#define AES_BLOCK_SIZE 16

static int aes_ecb_test(void)
{
    mbedtls_aes_context aes;
    uint8_t cipher[AES_BLOCK_SIZE];
    uint8_t plain [AES_BLOCK_SIZE];
    int ret = 0;

    const unsigned char key_128[] = {
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66};
#ifdef MTLS_AES_192
    const unsigned char key_192[] = {
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37};
#endif
#ifdef MTLS_AES_256
    const unsigned char key_256[] = {
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66};
#endif

   const unsigned char niKey[] = {
    0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
    0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
    0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
    0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4};

    __attribute__((aligned(4))) const uint8_t msg[] = {
        0x6e, 0x6f, 0x77, 0x20, 0x69, 0x73, 0x20, 0x74,
        0x68, 0x65, 0x20, 0x74, 0x69, 0x6d, 0x65, 0x20
    };
    const uint8_t verify_ecb_128[AES_BLOCK_SIZE] = {
        0xd0, 0xc9, 0xd9, 0xc9, 0x40, 0xe8, 0x97, 0xb6,
        0xc8, 0x8c, 0x33, 0x3b, 0xb5, 0x8f, 0x85, 0xd1
    };
#ifdef MTLS_AES_192
    const uint8_t verify_ecb_192[AES_BLOCK_SIZE] = {
        0x06, 0x57, 0xee, 0x78, 0x3f, 0x96, 0x00, 0xb1,
        0xec, 0x76, 0x94, 0x30, 0x29, 0xbe, 0x15, 0xab
    };
#endif
#ifdef MTLS_AES_256
    const uint8_t verify_ecb_256[AES_BLOCK_SIZE] = {
        0xcd, 0xf2, 0x81, 0x3e, 0x73, 0x3e, 0xf7, 0x33,
        0x3d, 0x18, 0xfd, 0x41, 0x85, 0x37, 0x04, 0x82
    };

    const uint8_t niPlain[] = {
        0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,
        0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a
    };
    const uint8_t niCipher[] = {
        0xf3,0xee,0xd1,0xbd,0xb5,0xd2,0xa0,0x3c,
        0x06,0x4b,0x5a,0x7e,0x3d,0xb1,0x81,0xf8
    };
#endif

    int i;
    struct {
        const unsigned char * key;
        int         keybits;
        const uint8_t* plain;
        const uint8_t* verify;
    } testVec[] = {
        { key_128, 128,  msg,     verify_ecb_128 },
#ifdef MTLS_AES_192
        { key_192, 192,  msg,     verify_ecb_192 },
#endif
#ifdef MTLS_AES_256
        { key_256, 256,  msg,     verify_ecb_256 },
        { niKey,   256,  niPlain, niCipher }
#endif
    };
    #define AES_ECB_TEST_LEN (int)(sizeof(testVec) / sizeof(*testVec))

    for (i = 0; i < AES_ECB_TEST_LEN; i++) {

        mbedtls_aes_init(&aes);

        if((ret = mbedtls_aes_setkey_enc(&aes, (char *)testVec[i].key, testVec[i].keybits)) != 0 ) {
            goto exit;
        }

        memset(cipher, 0, AES_BLOCK_SIZE);
        if((ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, testVec[i].plain, cipher)) !=0 ) {
            goto exit;
        }
        zassert_hash_eq(cipher, testVec[i].verify, AES_BLOCK_SIZE,
                        "aes_ecb enc vec=%d", i);

        memset(plain, 0, AES_BLOCK_SIZE);
        mbedtls_aes_init(&aes);

        if ((ret = mbedtls_aes_setkey_dec(&aes, (char *)testVec[i].key, testVec[i].keybits)) != 0 ) {
            goto exit;
        }
        if((ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, cipher, plain)) != 0) {
            goto exit;
        }
        zassert_hash_eq(plain, testVec[i].plain, AES_BLOCK_SIZE,
                        "aes_ecb dec vec=%d", i);
    }

exit:
    mbedtls_aes_free(&aes);
    return ret;
}

static int aes_cbc_test(void)
{
    mbedtls_aes_context aes;
    uint8_t cipher[AES_BLOCK_SIZE];
    uint8_t plain [AES_BLOCK_SIZE];
    int ret = 0;

    static const uint8_t msg[] = {
        0x6e,0x6f,0x77,0x20,0x69,0x73,0x20,0x74,
        0x68,0x65,0x20,0x74,0x69,0x6d,0x65,0x20
    };
    static const uint8_t verify[] =
    {
        0x95,0x94,0x92,0x57,0x5f,0x42,0x81,0x53,
        0x2c,0xcc,0x9d,0x46,0x77,0xa2,0x33,0xcb
    };

    const unsigned char key[] = {
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66};

    unsigned char enc_iv[] = {
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x30, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66};
    unsigned char dec_iv[] = {
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x30, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66};

    mbedtls_aes_init(&aes);

    if((ret = mbedtls_aes_setkey_enc(&aes, (char *)key, AES_BLOCK_SIZE*8)) != 0) {
        goto exit;
    }

    memset(cipher, 0, sizeof(cipher));
    if((ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, AES_BLOCK_SIZE, (char *)enc_iv, msg, cipher)) != 0) {
        goto exit;
    }
    zassert_hash_eq(cipher, verify, AES_BLOCK_SIZE, "aes_cbc enc");

    memset(plain, 0, sizeof(plain));
    if ((ret = mbedtls_aes_setkey_dec(&aes, (char *)key, AES_BLOCK_SIZE*8)) != 0) {
        goto exit;
    }
    if((ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, AES_BLOCK_SIZE, (char *)dec_iv, cipher, plain)) != 0) {
        goto exit;
    }
    zassert_hash_eq(plain, msg, AES_BLOCK_SIZE, "aes_cbc dec");
exit:
    mbedtls_aes_free(&aes);
    return ret;
}

static int aes_ctr_test(void)
{
    mbedtls_aes_context aes;
    uint8_t ctr_plaintext[] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10};

    uint8_t ctr_ciphertext_128[] = {
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26, 0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
        0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff, 0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff,
        0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5, 0xd3, 0x5e, 0x5b, 0x4f, 0x09, 0x02, 0x0d, 0xb0, 0x3e, 0xab,
        0x1e, 0x03, 0x1d, 0xda, 0x2f, 0xbe, 0x03, 0xd1, 0x79, 0x21, 0x70, 0xa0, 0xf3, 0x00, 0x9c, 0xee};

    const unsigned char ctr_key_128[] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};

    uint8_t ciphertext_buff[64];
    uint8_t plaintext_buff[64];
    int ret = 0;
    size_t current_nc_off = 0;
    size_t *nc_off_ptr = &current_nc_off;
    unsigned char nonce_counter_enc[16] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};
    unsigned char nonce_counter_dec[16] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};
    unsigned char stream_block[16] = { 0 };

    mbedtls_aes_init(&aes);

    if((ret = mbedtls_aes_setkey_enc(&aes, (char *)ctr_key_128, AES_BLOCK_SIZE*8)) != 0) {
        goto exit;
    }

    memset(ciphertext_buff, 0, sizeof(ciphertext_buff));
    if((ret = mbedtls_aes_crypt_ctr(&aes, sizeof(ctr_plaintext), nc_off_ptr, nonce_counter_enc, stream_block, ctr_plaintext, ciphertext_buff)) != 0) {
        goto exit;
    }
    zassert_hash_eq(ciphertext_buff, ctr_ciphertext_128,
                    sizeof(ctr_ciphertext_128), "aes_ctr enc");

    memset(plaintext_buff, 0, sizeof(plaintext_buff));
    if ((ret = mbedtls_aes_crypt_ctr(&aes, sizeof(ctr_ciphertext_128), nc_off_ptr, nonce_counter_dec, stream_block, ctr_ciphertext_128, plaintext_buff)) != 0) {
        goto exit;
    }
    zassert_hash_eq(plaintext_buff, ctr_plaintext,
                    sizeof(ctr_plaintext), "aes_ctr dec");
exit:
    mbedtls_aes_free(&aes);
    return ret;
}

static int aes_cfb128_test(void)
{
    mbedtls_aes_context aes;
    uint8_t cfb128_plaintext[] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10};

    uint8_t cfb128_ciphertext[] = {
        0x3b, 0x3f, 0xd9, 0x2e, 0xb7, 0x2d, 0xad, 0x20, 0x33, 0x34, 0x49, 0xf8, 0xe8, 0x3c, 0xfb, 0x4a,
        0xc8, 0xa6, 0x45, 0x37, 0xa0, 0xb3, 0xa9, 0x3f, 0xcd, 0xe3, 0xcd, 0xad, 0x9f, 0x1c, 0xe5, 0x8b,
        0x26, 0x75, 0x1f, 0x67, 0xa3, 0xcb, 0xb1, 0x40, 0xb1, 0x80, 0x8c, 0xf1, 0x87, 0xa4, 0xf4, 0xdf,
        0xc0, 0x4b, 0x05, 0x35, 0x7c, 0x5d, 0x1c, 0x0e, 0xea, 0xc4, 0xc6, 0x6f, 0x9f, 0xf7, 0xf2, 0xe6};

    const unsigned char cfb128_key[] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};

    unsigned char cfb128_enc_iv[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

    unsigned char cfb128_dec_iv[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

    uint8_t ciphertext_buff[64];
    uint8_t plaintext_buff[64];
    int ret = 0;
    size_t iv_off = 0;

    mbedtls_aes_init(&aes);

    if((ret = mbedtls_aes_setkey_enc(&aes, (char *)cfb128_key, AES_BLOCK_SIZE*8)) != 0) {
        goto exit;
    }

    memset(ciphertext_buff, 0, sizeof(ciphertext_buff));
    if((ret = mbedtls_aes_crypt_cfb128(&aes, MBEDTLS_AES_ENCRYPT, sizeof(cfb128_plaintext), &iv_off , cfb128_enc_iv, cfb128_plaintext, ciphertext_buff)) != 0) {
        goto exit;
    }
    zassert_hash_eq(ciphertext_buff, cfb128_ciphertext,
                    sizeof(cfb128_ciphertext), "aes_cfb128 enc");

    memset(plaintext_buff, 0, sizeof(plaintext_buff));
    if((ret = mbedtls_aes_crypt_cfb128(&aes, MBEDTLS_AES_DECRYPT, sizeof(cfb128_ciphertext), &iv_off, cfb128_dec_iv, ciphertext_buff, plaintext_buff)) != 0) {
        goto exit;
    }
    zassert_hash_eq(plaintext_buff, cfb128_plaintext,
                    sizeof(cfb128_plaintext), "aes_cfb128 dec");
exit:
    mbedtls_aes_free(&aes);
    return ret;
}

static int aes_cfb8_test(void)
{
    mbedtls_aes_context aes;
    uint8_t cfb8_plaintext[] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a, 0xae, 0x2d};

    uint8_t cfb8_ciphertext[] = {
        0x3b, 0x79, 0x42, 0x4c, 0x9c, 0x0d, 0xd4, 0x36, 0xba, 0xce, 0x9e, 0x0e, 0xd4, 0x58, 0x6a, 0x4f, 0x32, 0xb9};

    const unsigned char cfb8_key[] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};

    unsigned char cfb8_enc_iv[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

    unsigned char cfb8_dec_iv[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

    uint8_t ciphertext_buff[64];
    uint8_t plaintext_buff[64];
    int ret = 0;

    mbedtls_aes_init(&aes);

    if((ret = mbedtls_aes_setkey_enc(&aes, (char *)cfb8_key, AES_BLOCK_SIZE*8)) != 0) {
        goto exit;
    }

    memset(ciphertext_buff, 0, sizeof(ciphertext_buff));
    if((ret = mbedtls_aes_crypt_cfb8(&aes, MBEDTLS_AES_ENCRYPT, sizeof(cfb8_plaintext), cfb8_enc_iv, cfb8_plaintext, ciphertext_buff)) != 0) {
        goto exit;
    }
    zassert_hash_eq(ciphertext_buff, cfb8_ciphertext,
                    sizeof(cfb8_ciphertext), "aes_cfb8 enc");

    memset(plaintext_buff, 0, sizeof(plaintext_buff));
    if((ret = mbedtls_aes_crypt_cfb8(&aes, MBEDTLS_AES_DECRYPT, sizeof(cfb8_ciphertext), cfb8_dec_iv, ciphertext_buff, plaintext_buff)) != 0) {
        goto exit;
    }
    zassert_hash_eq(plaintext_buff, cfb8_plaintext,
                    sizeof(cfb8_plaintext), "aes_cfb8 dec");
exit:
    mbedtls_aes_free(&aes);
    return ret;
}

static int aes_ofb_test(void)
{
    mbedtls_aes_context aes;
    uint8_t ofb_plaintext[] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10};

    uint8_t ofb_ciphertext[] = {
        0x3b, 0x3f, 0xd9, 0x2e, 0xb7, 0x2d, 0xad, 0x20, 0x33, 0x34, 0x49, 0xf8, 0xe8, 0x3c, 0xfb, 0x4a,
        0x77, 0x89, 0x50, 0x8d, 0x16, 0x91, 0x8f, 0x03, 0xf5, 0x3c, 0x52, 0xda, 0xc5, 0x4e, 0xd8, 0x25,
        0x97, 0x40, 0x05, 0x1e, 0x9c, 0x5f, 0xec, 0xf6, 0x43, 0x44, 0xf7, 0xa8, 0x22, 0x60, 0xed, 0xcc,
        0x30, 0x4c, 0x65, 0x28, 0xf6, 0x59, 0xc7, 0x78, 0x66, 0xa5, 0x10, 0xd9, 0xc1, 0xd6, 0xae, 0x5e};

    const unsigned char ofb_key[] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};

    unsigned char ofb_enc_iv[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

    unsigned char ofb_dec_iv[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

    size_t iv_off = 0;
    uint8_t ciphertext_buff[64];
    uint8_t plaintext_buff[64];
    int ret = 0;

    mbedtls_aes_init(&aes);

    if((ret = mbedtls_aes_setkey_enc(&aes, (char *)ofb_key, AES_BLOCK_SIZE*8)) != 0) {
        goto exit;
    }

    memset(ciphertext_buff, 0, sizeof(ciphertext_buff));
    if((ret = mbedtls_aes_crypt_ofb(&aes, sizeof(ofb_plaintext), &iv_off, ofb_enc_iv, ofb_plaintext, ciphertext_buff)) != 0) {
        goto exit;
    }
    zassert_hash_eq(ciphertext_buff, ofb_ciphertext,
                    sizeof(ofb_ciphertext), "aes_ofb enc");

    memset(plaintext_buff, 0, sizeof(plaintext_buff));
    if ((ret = mbedtls_aes_crypt_ofb(&aes, sizeof(ofb_ciphertext), &iv_off, ofb_dec_iv, ciphertext_buff, plaintext_buff)) != 0) {
        goto exit;
    }
    zassert_hash_eq(plaintext_buff, ofb_plaintext,
                    sizeof(ofb_plaintext), "aes_ofb dec");
exit:
    mbedtls_aes_free(&aes);
    return ret;
}

static int aes_xts_test(void)
{
    int ret = 0;

    static const unsigned char aes_test_xts_key[][32] =
    {
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
          0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
          0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
          0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22 },
        { 0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8,
          0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1, 0xf0,
          0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
          0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22 },
    };

    static const unsigned char aes_test_xts_data_unit[][16] =
    {
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x33, 0x33, 0x33, 0x33, 0x33, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x33, 0x33, 0x33, 0x33, 0x33, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    };

    static const unsigned char aes_test_xts_ct32[][32] =
    {
        { 0x91, 0x7c, 0xf6, 0x9e, 0xbd, 0x68, 0xb2, 0xec,
          0x9b, 0x9f, 0xe9, 0xa3, 0xea, 0xdd, 0xa6, 0x92,
          0xcd, 0x43, 0xd2, 0xf5, 0x95, 0x98, 0xed, 0x85,
          0x8c, 0x02, 0xc2, 0x65, 0x2f, 0xbf, 0x92, 0x2e },
        { 0xc4, 0x54, 0x18, 0x5e, 0x6a, 0x16, 0x93, 0x6e,
          0x39, 0x33, 0x40, 0x38, 0xac, 0xef, 0x83, 0x8b,
          0xfb, 0x18, 0x6f, 0xff, 0x74, 0x80, 0xad, 0xc4,
          0x28, 0x93, 0x82, 0xec, 0xd6, 0xd3, 0x94, 0xf0 },
        { 0xaf, 0x85, 0x33, 0x6b, 0x59, 0x7a, 0xfc, 0x1a,
          0x90, 0x0b, 0x2e, 0xb2, 0x1e, 0xc9, 0x49, 0xd2,
          0x92, 0xdf, 0x4c, 0x04, 0x7e, 0x0b, 0x21, 0x53,
          0x21, 0x86, 0xa5, 0x97, 0x1a, 0x22, 0x7a, 0x89 },
    };

    static const unsigned char aes_test_xts_pt32[][32] =
    {
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
          0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
          0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
          0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44 },
        { 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
          0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
          0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
          0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44 },
    };

    static const int num_tests =
        sizeof(aes_test_xts_key) / sizeof(*aes_test_xts_key);
    mbedtls_aes_xts_context ctx_xts;

    mbedtls_aes_xts_init(&ctx_xts);

    for (uint8_t i = 0; i < num_tests << 1; i++) {
        mbedtls_aes_xts_init(&ctx_xts);

        const unsigned char *data_unit;
        uint8_t u = i >> 1;
        uint8_t mode = i & 1;
        unsigned char key[32];
        unsigned char buf[64];
        const unsigned char *aes_tests;

        memset(key, 0, sizeof(key));
        memcpy(key, aes_test_xts_key[u], 32);
        data_unit = aes_test_xts_data_unit[u];

        uint32_t len = sizeof(*aes_test_xts_ct32);

        if (mode == MBEDTLS_AES_DECRYPT) {
            ret = mbedtls_aes_xts_setkey_dec(&ctx_xts, key, 256);
            if (ret != 0) {
                goto exit;
            }
            memcpy(buf, aes_test_xts_ct32[u], len);
            aes_tests = aes_test_xts_pt32[u];
        } else {
            ret = mbedtls_aes_xts_setkey_enc(&ctx_xts, key, 256);
            if (ret != 0) {
                goto exit;
            }
            memcpy(buf, aes_test_xts_pt32[u], len);
            aes_tests = aes_test_xts_ct32[u];
        }

        ret = mbedtls_aes_crypt_xts(&ctx_xts, mode, len, data_unit,
                                    buf, buf);
        if (ret != 0) {
            goto exit;
        }

        zassert_hash_eq(buf, aes_tests, len,
                        "aes_xts vec=%u mode=%u", u, mode);
    }

    mbedtls_aes_xts_free(&ctx_xts);

    ret = 0;

exit:
    return ret;
}

static int aes_test(void)
{
    int ret = 0;

    if ((ret = aes_ecb_test()) != 0)    { printk("aes_ecb_test failed\n");    return ret; }
    if ((ret = aes_cbc_test()) != 0)    { printk("aes_cbc_test failed\n");    return ret; }
    if ((ret = aes_cfb128_test()) != 0) { printk("aes_cfb128_test failed\n"); return ret; }
    if ((ret = aes_cfb8_test()) != 0)   { printk("aes_cfb8_test failed\n");   return ret; }
    if ((ret = aes_ofb_test()) != 0)    { printk("aes_ofb_test failed\n");    return ret; }
    if ((ret = aes_ctr_test()) != 0)    { printk("aes_ctr_test failed\n");    return ret; }
    if ((ret = aes_xts_test()) != 0)    { printk("aes_xts_test failed\n");    return ret; }

    return 0;
}

/* ========================================================================
 * SM4 KAT（硬件 SM4，draft-ribose-cfrg-sm4-10 向量）
 * ======================================================================== */
#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
#include "mbedtls/sm4_alt.h"
#define SM4_BLOCK_SIZE 16

static int test_sm4(void)
{
    /* draft-ribose-cfrg-sm4-10 A.2.1.1 */
    static const uint8_t k1[] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
    };
    static const uint8_t p1[] = {
        0xAA, 0xAA, 0xAA, 0xAA, 0xBB, 0xBB, 0xBB, 0xBB,
        0xCC, 0xCC, 0xCC, 0xCC, 0xDD, 0xDD, 0xDD, 0xDD,
        0xEE, 0xEE, 0xEE, 0xEE, 0xFF, 0xFF, 0xFF, 0xFF,
        0xAA, 0xAA, 0xAA, 0xAA, 0xBB, 0xBB, 0xBB, 0xBB
    };
    static const uint8_t c1_ecb[] = {
        0x5E, 0xC8, 0x14, 0x3D, 0xE5, 0x09, 0xCF, 0xF7,
        0xB5, 0x17, 0x9F, 0x8F, 0x47, 0x4B, 0x86, 0x19,
        0x2F, 0x1D, 0x30, 0x5A, 0x7F, 0xB1, 0x7D, 0xF9,
        0x85, 0xF8, 0x1C, 0x84, 0x82, 0x19, 0x23, 0x04
    };

    mbedtls_sm4_context sm4;
    uint8_t enc[SM4_BLOCK_SIZE * 4];
    uint8_t dec[SM4_BLOCK_SIZE * 4];
    int ret;

    memset(enc, 0, SM4_BLOCK_SIZE * 4);
    memset(dec, 0, SM4_BLOCK_SIZE * 4);

    mbedtls_sm4_init(&sm4);

    if((ret = mbedtls_sm4_setkey(k1)) !=0 ) {
        goto exit;
    }

    if((ret = mbedtls_sm4_ecb_encrypt(&sm4, enc, p1, sizeof(p1))) !=0 ){
        goto exit;
    }

    zassert_hash_eq(enc, c1_ecb, sizeof(c1_ecb), "sm4 ecb enc");

    if((ret = mbedtls_sm4_ecb_decrypt(&sm4, dec, enc, sizeof(c1_ecb))) !=0 ) {
        goto exit;
    }

    zassert_hash_eq(dec, p1, sizeof(p1), "sm4 ecb dec");

exit:
    mbedtls_sm4_free(&sm4);

    return ret;
}

static int sm4_ctr_test(void)
{
    /* draft-ribose-cfrg-sm4-10 A.2.5.1 */
    static const uint8_t k1[] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
    };
    static const uint8_t i1[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
    };
    static const uint8_t p2[] = {
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
        0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
        0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD,
        0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
        0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB
    };
    static const uint8_t c2_ctr[] = {
        0xAC, 0x32, 0x36, 0xCB, 0x97, 0x0C, 0xC2, 0x07,
        0x91, 0x36, 0x4C, 0x39, 0x5A, 0x13, 0x42, 0xD1,
        0xA3, 0xCB, 0xC1, 0x87, 0x8C, 0x6F, 0x30, 0xCD,
        0x07, 0x4C, 0xCE, 0x38, 0x5C, 0xDD, 0x70, 0xC7,
        0xF2, 0x34, 0xBC, 0x0E, 0x24, 0xC1, 0x19, 0x80,
        0xFD, 0x12, 0x86, 0x31, 0x0C, 0xE3, 0x7B, 0x92,
        0x6E, 0x02, 0xFC, 0xD0, 0xFA, 0xA0, 0xBA, 0xF3,
        0x8B, 0x29, 0x33, 0x85, 0x1D, 0x82, 0x45, 0x14
    };

    mbedtls_sm4_context sm4;
    uint8_t enc[SM4_BLOCK_SIZE * 4];
    uint8_t dec[SM4_BLOCK_SIZE * 4];
    int ret;

    /* 每一步驱动调用的非 0 返回都**先 zassert 记 FAIL**，再 goto exit 释放上下文。
     * 旧版六处错误分支只 `goto exit` 跳过 KAT，且函数尾固定 `return 0`
     * ⇒ 驱动返回任何错误码时外层 `zassert_equal(ret,0)` 恒真、本项永不可 FAIL。
     * 现改为：错误即断言失败（zassert 走 ztest 的 test-fail 通路，本项必 FAIL），
     * 只有前一步成功才继续下一步（避免用无效输出做 KAT 产生误导性二次失败），
     * 并在结尾 `return ret` 让外层断言不再是恒真式。 */
    mbedtls_sm4_init(&sm4);

    ret = mbedtls_sm4_setkey(k1);
    zassert_equal(ret, 0, "sm4 ctr setkey ret=%d", ret);
    if (ret != 0) {
        goto exit;
    }

    ret = mbedtls_sm4_setiv(&sm4, i1);
    zassert_equal(ret, 0, "sm4 ctr setiv ret=%d", ret);
    if (ret != 0) {
        goto exit;
    }

    ret = mbedtls_sm4_ctr_crypto(&sm4, enc, p2, sizeof(p2));
    zassert_equal(ret, 0, "sm4 ctr enc crypto ret=%d", ret);
    if (ret != 0) {
        goto exit;
    }

    zassert_hash_eq(enc, c2_ctr, sizeof(c2_ctr), "sm4 ctr enc");

    mbedtls_sm4_init(&sm4);

    ret = mbedtls_sm4_setkey(k1);
    zassert_equal(ret, 0, "sm4 ctr setkey(2) ret=%d", ret);
    if (ret != 0) {
        goto exit;
    }

    ret = mbedtls_sm4_setiv(&sm4, i1);
    zassert_equal(ret, 0, "sm4 ctr setiv(2) ret=%d", ret);
    if (ret != 0) {
        goto exit;
    }

    ret = mbedtls_sm4_ctr_crypto(&sm4, dec, enc, sizeof(c2_ctr));
    zassert_equal(ret, 0, "sm4 ctr dec crypto ret=%d", ret);
    if (ret != 0) {
        goto exit;
    }

    zassert_hash_eq(dec, p2, sizeof(p2), "sm4 ctr dec");

exit:
    mbedtls_sm4_free(&sm4);

    return ret;
}
#endif /* CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT */

/* ========================================================================
 * RSA（OTBN）：导入固定 2048 key 的 enc/dec/sign/verify + OpenSSL KAT + keygen
 * ======================================================================== */
#if defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)
#include "mbedtls/rsa.h"
#include "rsa_test_key.h"

int otbn_rsa_import_test_key(mbedtls_rsa_context *rsa)
{
    int ret;

    mbedtls_rsa_init(rsa);

    ret = mbedtls_rsa_import_raw(rsa,
                                 rsa2048_n, sizeof(rsa2048_n),
                                 NULL, 0,
                                 NULL, 0,
                                 rsa2048_d, sizeof(rsa2048_d),
                                 rsa2048_e, sizeof(rsa2048_e));
    if (ret != 0) {
        return ret;
    }

    return mbedtls_rsa_complete(rsa);
}

static int test_rsa(void)
{
    int ret;
    mbedtls_rsa_context rsa;
    const unsigned char msg[] = "hello rsa otbn test";
    unsigned char hash[32];
    unsigned char enc[256];
    unsigned char dec[256];
    unsigned char sig[512];
    size_t dec_len;
    mbedtls_sha256_context sha;

    ret = otbn_rsa_import_test_key(&rsa);
    if (ret != 0) {
        goto exit;
    }

    /* PKCS#1 v1.5 encrypt with public key, decrypt with private key. */
    ret = mbedtls_rsa_pkcs1_encrypt(&rsa, otbn_test_rng, NULL,
                                    sizeof(msg) - 1, msg, enc);
    if (ret != 0) {
        goto exit;
    }

    ret = mbedtls_rsa_pkcs1_decrypt(&rsa, otbn_test_rng, NULL,
                                    &dec_len, enc, dec, sizeof(dec));
    if (ret != 0) {
        printk("rsa stage=decrypt ret=%d\n", ret);
        goto exit;
    }
    if (dec_len != sizeof(msg) - 1) {
        zassert_equal(dec_len, sizeof(msg) - 1,
                      "rsa stage=decrypt_len %u != %u",
                      (unsigned int)dec_len, (unsigned int)(sizeof(msg) - 1));
    }
    zassert_hash_eq(dec, msg, dec_len, "rsa stage=decrypt_cmp");

    /* PKCS#1 v1.5 sign and verify with SHA-256. */
    mbedtls_sha256_init(&sha);
    ret = mbedtls_sha256_starts(&sha, 0);
    if (ret == 0) {
        ret = mbedtls_sha256_update(&sha, msg, sizeof(msg) - 1);
    }
    if (ret == 0) {
        ret = mbedtls_sha256_finish(&sha, hash);
    }
    mbedtls_sha256_free(&sha);
    if (ret != 0) {
        goto exit;
    }

    ret = mbedtls_rsa_pkcs1_sign(&rsa, otbn_test_rng, NULL,
                                 MBEDTLS_MD_SHA256, 32, hash, sig);
    if (ret != 0) {
        printk("rsa stage=sign ret=%d\n", ret);
        goto exit;
    }

    ret = mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256, 32, hash, sig);
    if (ret != 0) {
        printk("rsa stage=verify ret=%d\n", ret);
        goto exit;
    }

    /* Known-answer test: decrypt OpenSSL-generated PKCS#1 v1.5 ciphertext. */
    ret = mbedtls_rsa_pkcs1_decrypt(&rsa, otbn_test_rng, NULL,
                                    &dec_len, rsa2048_ciphertext, dec, sizeof(dec));
    if (ret != 0) {
        printk("rsa stage=kat_decrypt ret=%d\n", ret);
        goto exit;
    }
    if (dec_len != sizeof(msg) - 1) {
        zassert_equal(dec_len, sizeof(msg) - 1,
                      "rsa stage=kat_decrypt_len %u != %u",
                      (unsigned int)dec_len, (unsigned int)(sizeof(msg) - 1));
    }
    zassert_hash_eq(dec, msg, dec_len, "rsa stage=kat_decrypt_cmp");

    /* Known-answer test: sign and compare to OpenSSL-generated signature. */
    ret = mbedtls_rsa_pkcs1_sign(&rsa, otbn_test_rng, NULL,
                                 MBEDTLS_MD_SHA256, 32, hash, sig);
    if (ret != 0) {
        printk("rsa stage=kat_sign ret=%d\n", ret);
        goto exit;
    }
    zassert_hash_eq(sig, rsa2048_signature, sizeof(rsa2048_signature),
                    "rsa stage=kat_sign_cmp");

    /* Known-answer test: verify OpenSSL-generated signature. */
    ret = mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256, 32, hash,
                                   rsa2048_signature);
    if (ret != 0) {
        printk("rsa stage=kat_verify ret=%d\n", ret);
        goto exit;
    }

exit:
    mbedtls_rsa_free(&rsa);
    return ret;
}

/* keygen + check_privkey + sha256(msg) + sign + verify，全流程校验生成密钥可用 */
int otbn_rsa_keygen_check(unsigned int nbits, uint32_t *elapsed_ms)
{
    int ret;
    mbedtls_rsa_context rsa;
    unsigned char hash[32];
    unsigned char sig[512];
    mbedtls_sha256_context sha;
    const unsigned char msg[] = "hello rsa otbn test";
    int64_t t0 = k_uptime_get();

    mbedtls_rsa_init(&rsa);

    ret = mbedtls_rsa_gen_key(&rsa, otbn_rsa_keygen_get_rng, NULL, nbits, 65537);
    if (ret != 0) {
        printk("RSA-%u keygen: mbedtls_rsa_gen_key failed ret=%d\n", nbits, ret);
        goto exit;
    }

    ret = mbedtls_rsa_check_privkey(&rsa);
    if (ret != 0) {
        printk("RSA-%u keygen: check_privkey failed ret=%d\n", nbits, ret);
        goto exit;
    }

    mbedtls_sha256_init(&sha);
    ret = mbedtls_sha256_starts(&sha, 0);
    if (ret == 0) {
        ret = mbedtls_sha256_update(&sha, msg, sizeof(msg) - 1);
    }
    if (ret == 0) {
        ret = mbedtls_sha256_finish(&sha, hash);
    }
    mbedtls_sha256_free(&sha);
    if (ret != 0) {
        goto exit;
    }

    ret = mbedtls_rsa_pkcs1_sign(&rsa, otbn_test_rng, NULL,
                                 MBEDTLS_MD_SHA256, 32, hash, sig);
    if (ret != 0) {
        printk("RSA-%u keygen: sign failed ret=%d\n", nbits, ret);
        goto exit;
    }

    ret = mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256, 32, hash, sig);
    if (ret != 0) {
        printk("RSA-%u keygen: verify failed ret=%d\n", nbits, ret);
        goto exit;
    }

exit:
    mbedtls_rsa_free(&rsa);
    if (elapsed_ms != NULL) {
        *elapsed_ms = (uint32_t)(k_uptime_get() - t0);
    }
    return ret;
}
#endif /* CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT */

/* ========================================================================
 * ECDSA（OTBN）：p256 定 k KAT + p384/sm2 genkey+sign+verify
 * ======================================================================== */
#if defined(CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT)
#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"

static int runIt_unhexify(unsigned char *obuf, const char *ibuf)
{
    unsigned char c, c2;
    int len = strlen(ibuf) / 2;

    while (*ibuf != 0)
    {
        c = *ibuf++;
        if (c >= '0' && c <= '9')
            c -= '0';
        else if (c >= 'a' && c <= 'f')
            c -= 'a' - 10;
        else if (c >= 'A' && c <= 'F')
            c -= 'A' - 10;
        else
            return -1;

        c2 = *ibuf++;
        if (c2 >= '0' && c2 <= '9')
            c2 -= '0';
        else if (c2 >= 'a' && c2 <= 'f')
            c2 -= 'a' - 10;
        else if (c2 >= 'A' && c2 <= 'F')
            c2 -= 'A' - 10;
        else
            return -1;

        *obuf++ = (c << 4) | c2;
    }

    return len;
}

/* P-256 确定性签名 KAT：固定 d/Q/k/hash，比对 r/s 并 verify */
static int ecdsa_p256_kat_test(void)
{
    int rc = 0;
    static const char *d_str = "DC51D3866A15BACDE33D96F992FCA99DA7E6EF0934E7097559C27F1614C88A7F";
    static const char *xQ_str = "2442A5CC0ECD015FA3CA31DC8E2BBC70BF42D60CBCA20085E0822CB04235E970";
    static const char *yQ_str = "6FC98BD7E50211A4A27102FA3549DF79EBCB4BF246B80945CDDFE7D509BBFD7D";
    static const char *k_str = "9E56F509196784D963D1C0A401510EE7ADA3DCC5DEE04B154BF61AF1D5A6DECE";
    static const char *hash_str = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
    static const char *r_str = "CB28E0999B9C7715FD0A80D8E47A77079716CBBF917DD72E97566EA1C066957C";
    static const char *s_str = "86FA3BB4E26CAD5BF90B7F81899256CE7594BB1EA0C89212748BFF3B3D5B0315";

    mbedtls_ecp_group pGrp;
    mbedtls_ecp_point pQ;
    mbedtls_mpi d;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_mpi rCheck;
    mbedtls_mpi sCheck;

    static uint8_t k_buf[66];
    uint8_t hash_buf[100];
    unsigned char *pHash = hash_buf;

    size_t hlen;
    mbedtls_ecp_group_init(&pGrp);
    mbedtls_ecp_point_init(&pQ);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_mpi_init(&rCheck);
    mbedtls_mpi_init(&sCheck);
    memset(pHash, 0, 66);
    memset(k_buf, 0, sizeof(k_buf));

    mbedtls_ecp_group_load(&pGrp, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_ecp_point_read_string(&pQ, 16, xQ_str, yQ_str);

    mbedtls_mpi_read_string(&d, 16, d_str);
    mbedtls_mpi_read_string(&rCheck, 16, r_str);
    mbedtls_mpi_read_string(&sCheck, 16, s_str);

    hlen = runIt_unhexify(pHash, hash_str);

    uint16_t rnd_length = runIt_unhexify(k_buf, k_str);

    /* Fix pRndBuf by shifting it left if necessary */
    if (pGrp.nbits % 8 != 0)
    {
        unsigned char shift = 8 - (pGrp.nbits % 8);
        size_t i;

        for (i = 0; i < rnd_length - 1; i++)
            k_buf[i] = k_buf[i] << shift | k_buf[i + 1] >> (8 - shift);

        k_buf[rnd_length - 1] <<= shift;
    }

    /* 定 k KAT：k 必须用上面准备的 k_buf（黄金 r/s 按此 k 算出，PC 已复核）。
     * 此前误用 otbn_get_fixed（0xccbbaa pattern）→ k 不对 → r 必 mismatch。 */
    if(mbedtls_ecdsa_sign(&pGrp, &r, &s, &d, pHash, hlen, otbn_rnd_buf, k_buf) != 0)
    {
        printk("ecdsa p256 kat sign failed\n");
        rc = -1;
        goto exit;
    }

    if(mbedtls_mpi_cmp_mpi(&r, &rCheck) != 0)
    {
        zassert_true(false, "ecdsa p256 kat r mismatch");
    }
    if(mbedtls_mpi_cmp_mpi(&s, &sCheck) != 0)
    {
        zassert_true(false, "ecdsa p256 kat s mismatch");
    }
    if(mbedtls_ecdsa_verify(&pGrp, pHash, hlen, &pQ, &rCheck, &sCheck) != 0)
    {
        zassert_true(false, "ecdsa p256 kat verify failed");
    }

exit:
    mbedtls_ecp_group_free(&pGrp);
    mbedtls_ecp_point_free(&pQ);

    mbedtls_mpi_free(&d);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&rCheck);
    mbedtls_mpi_free(&sCheck);

    return rc;
}

/* 通用 genkey + sign + verify（mbedtls_test ecdsa_test_curve 的功能路径，loops=1）。
 * 参数用 int 避免头文件前向声明 enum（mbedtls_ecp_group_id 值域即 int）。 */
int otbn_ecdsa_curve_once(int curve)
{
    int err = 0;
    mbedtls_ecdsa_context ctx;
    mbedtls_mpi r;
    mbedtls_mpi s;
    static const char *hash_str = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";

    if(!(curve == MBEDTLS_ECP_DP_SECP384R1 || curve == MBEDTLS_ECP_DP_SECP256R1 || curve == MBEDTLS_ECP_DP_SM2))
    {
        printk("This curve is not supported.\n");
        return -1;
    }

    mbedtls_ecp_group_init(&ctx.private_grp);
    mbedtls_ecp_group_load(&ctx.private_grp, curve);

    mbedtls_mpi_init(&ctx.private_Q.private_X);
    mbedtls_mpi_init(&ctx.private_Q.private_Y);
    mbedtls_mpi_init(&ctx.private_Q.private_Z);
    mbedtls_mpi_init(&ctx.private_d);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    err = mbedtls_ecdsa_genkey(&ctx, curve, otbn_get_random, NULL);
    if(err)
    {
        printk("ecc keygen failed curve=%d\n", curve);
        goto exit_test;
    }
    uint8_t hash_buf[100];
    unsigned char *pHash = hash_buf;
    size_t hlen;
    hlen = runIt_unhexify(pHash, hash_str);

    if(mbedtls_ecdsa_sign(&ctx.private_grp, &r, &s, &ctx.private_d, pHash, hlen, otbn_get_fixed, NULL) != 0)
    {
        err = -1;
        printk("ecdsa sign failed curve=%d\n", curve);
        goto exit_test;
    }

    if(mbedtls_ecdsa_verify(&ctx.private_grp, pHash, hlen, &ctx.private_Q, &r, &s) != 0)
    {
        err = -1;
        printk("ecdsa verify failed curve=%d\n", curve);
        goto exit_test;
    }

exit_test:
    mbedtls_ecp_group_free(&ctx.private_grp);
    mbedtls_mpi_free(&ctx.private_Q.private_X);
    mbedtls_mpi_free(&ctx.private_Q.private_Y);
    mbedtls_mpi_free(&ctx.private_Q.private_Z);
    mbedtls_mpi_free(&ctx.private_d);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);

    return err;
}
#endif /* CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT */

#if defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)
/*
 * RSA-4096 keygen（耗时最长且疑似挂死，见 tests/OTBN_TEST_STATUS.md BUG-4）：
 * 独立心跳线程每 30s 上报 keygen RNG 调用次数（=candidate 生成次数），
 * 用于区分"真挂死"与"素数搜索慢"。放在套件最后执行（z_keygen 套件，
 * 字母序在 api/mix/perf 之后）。
 */
static struct k_thread keygen_hb_thread;
static K_THREAD_STACK_DEFINE(keygen_hb_stack, 1024);
static volatile bool keygen_hb_stop;

static void keygen_hb_func(void *p1, void *p2, void *p3)
{
	(void)p1; (void)p2; (void)p3;

	while (!keygen_hb_stop) {
		k_sleep(K_SECONDS(30));
		if (!keygen_hb_stop) {
			ZT_INFO("api_rsa_keygen_4096", "progress_rng_calls=%u",
				otbn_keygen_rng_calls());
		}
	}
}

ZTEST(z_keygen, api_rsa_keygen_4096)
{
	uint32_t elapsed_ms = 0;
	int ret;

	keygen_hb_stop = false;
	k_thread_create(&keygen_hb_thread, keygen_hb_stack,
			K_THREAD_STACK_SIZEOF(keygen_hb_stack),
			keygen_hb_func, NULL, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);

	ret = otbn_rsa_keygen_check(4096, &elapsed_ms);

	keygen_hb_stop = true;
	k_thread_join(&keygen_hb_thread, K_FOREVER);

	ZT_INFO("api_rsa_keygen_4096", "nbits=%u,time_ms=%u,rng_calls=%u",
		4096u, elapsed_ms, otbn_keygen_rng_calls());
	zassert_equal(ret, 0, "rsa_keygen_4096 ret=%d", ret);
}
#endif /* CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT */

/* ========================================================================
 * 性能测量（INFO 上报，不因耗时判 FAIL）
 * ======================================================================== */
static uint8_t perf_buf[16384];   /* 16KB 全 0x5a，循环喂给 64KB 流（ztest 迁移后 RAM 预算） */

static int perf_sha256_64k(uint32_t *us)
{
    mbedtls_sha256_context ctx;
    uint8_t hash[SHA256_DIGEST_SIZE];
    int ret;

    memset(perf_buf, 0x5a, sizeof(perf_buf));

    uint32_t t0 = k_cycle_get_32();

    mbedtls_sha256_init(&ctx);
    ret = mbedtls_sha256_starts(&ctx, 0);
    if (ret == 0) {
        /* 1KB 分片，兼顾流式路径与吞吐；16KB 缓冲循环 4 遍 = 原 64KB
         * 全 0x5a 输入流（逐字节一致，哈希结果不变） */
        for (int i = 0; i < 64 && ret == 0; i++) {
            ret = mbedtls_sha256_update(&ctx, perf_buf + (i % 16) * 1024, 1024);
        }
    }
    if (ret == 0) {
        ret = mbedtls_sha256_finish(&ctx, hash);
    }
    mbedtls_sha256_free(&ctx);

    *us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
    return ret;
}

#if defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)
static int perf_rsa2048_sign(uint32_t *avg_us, int loops)
{
    int ret;
    mbedtls_rsa_context rsa;
    const unsigned char msg[] = "hello rsa otbn perf";
    unsigned char hash[32];
    unsigned char sig[512];
    mbedtls_sha256_context sha;

    ret = otbn_rsa_import_test_key(&rsa);
    if (ret != 0) {
        goto exit;
    }

    mbedtls_sha256_init(&sha);
    ret = mbedtls_sha256_starts(&sha, 0);
    if (ret == 0) {
        ret = mbedtls_sha256_update(&sha, msg, sizeof(msg) - 1);
    }
    if (ret == 0) {
        ret = mbedtls_sha256_finish(&sha, hash);
    }
    mbedtls_sha256_free(&sha);
    if (ret != 0) {
        goto exit;
    }

    uint32_t t0 = k_cycle_get_32();
    for (int i = 0; i < loops; i++) {
        ret = mbedtls_rsa_pkcs1_sign(&rsa, otbn_test_rng, NULL,
                                     MBEDTLS_MD_SHA256, 32, hash, sig);
        if (ret != 0) {
            goto exit;
        }
    }
    uint32_t total_us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
    *avg_us = total_us / (uint32_t)loops;

exit:
    mbedtls_rsa_free(&rsa);
    return ret;
}
#endif

#if defined(CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT)
static int perf_ecdsa_p256(uint32_t *sign_ops, uint32_t *verify_ops, int loops)
{
    int err = 0;
    mbedtls_ecdsa_context ctx;
    mbedtls_mpi r;
    mbedtls_mpi s;
    static const char *hash_str = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";

    mbedtls_ecp_group_init(&ctx.private_grp);
    mbedtls_ecp_group_load(&ctx.private_grp, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_mpi_init(&ctx.private_Q.private_X);
    mbedtls_mpi_init(&ctx.private_Q.private_Y);
    mbedtls_mpi_init(&ctx.private_Q.private_Z);
    mbedtls_mpi_init(&ctx.private_d);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    err = mbedtls_ecdsa_genkey(&ctx, MBEDTLS_ECP_DP_SECP256R1, otbn_get_random, NULL);
    if (err) {
        goto exit_test;
    }

    uint8_t hash_buf[100];
    size_t hlen = runIt_unhexify(hash_buf, hash_str);

    int64_t start_sign = k_uptime_get();
    for (int i = 0; i < loops; i++) {
        if (mbedtls_ecdsa_sign(&ctx.private_grp, &r, &s, &ctx.private_d,
                               hash_buf, hlen, otbn_get_fixed, NULL) != 0) {
            err = -1;
            goto exit_test;
        }
    }
    uint32_t sign_ms = (uint32_t)(k_uptime_get() - start_sign);
    if (sign_ms == 0) sign_ms = 1;

    int64_t start_verify = k_uptime_get();
    for (int i = 0; i < loops; i++) {
        if (mbedtls_ecdsa_verify(&ctx.private_grp, hash_buf, hlen,
                                 &ctx.private_Q, &r, &s) != 0) {
            err = -1;
            goto exit_test;
        }
    }
    uint32_t verify_ms = (uint32_t)(k_uptime_get() - start_verify);
    if (verify_ms == 0) verify_ms = 1;

    *sign_ops = (uint32_t)(loops * 1000ULL / sign_ms);
    *verify_ops = (uint32_t)(loops * 1000ULL / verify_ms);

exit_test:
    mbedtls_ecp_group_free(&ctx.private_grp);
    mbedtls_mpi_free(&ctx.private_Q.private_X);
    mbedtls_mpi_free(&ctx.private_Q.private_Y);
    mbedtls_mpi_free(&ctx.private_Q.private_Z);
    mbedtls_mpi_free(&ctx.private_d);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);

    return err;
}
#endif

/* ========================================================================
 * ZTEST 注册（套件按链接段 SORT_BY_NAME 升序执行：api → mix → perf →
 * z_keygen；套件内按测试名升序，源码声明序不影响执行序）
 * ======================================================================== */

/* ---- api 套件：hash KAT + 硬件引擎 KAT + RSA KAT/keygen 2048/3072 ---- */

ZTEST(api, api_aes_kat)
{
    int ret = aes_test();

    zassert_equal(ret, 0, "aes_test ret=%d", ret);
}

#if defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)
ZTEST(api, api_rsa2048_kat)
{
    int ret = test_rsa();

    zassert_equal(ret, 0, "test_rsa ret=%d", ret);
}

ZTEST(api, api_rsa_keygen_2048)
{
    uint32_t elapsed_ms = 0;
    int ret = otbn_rsa_keygen_check(2048, &elapsed_ms);

    ZT_INFO("api_rsa_keygen_2048", "nbits=%u,time_ms=%u", 2048u, elapsed_ms);
    zassert_equal(ret, 0, "rsa_keygen_2048 ret=%d", ret);
}

ZTEST(api, api_rsa_keygen_3072)
{
    uint32_t elapsed_ms = 0;
    int ret = otbn_rsa_keygen_check(3072, &elapsed_ms);

    ZT_INFO("api_rsa_keygen_3072", "nbits=%u,time_ms=%u", 3072u, elapsed_ms);
    zassert_equal(ret, 0, "rsa_keygen_3072 ret=%d", ret);
}
#endif /* CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT */

ZTEST(api, api_sha224_kat)
{
    int ret = test_sha224();

    zassert_equal(ret, 0, "test_sha224 ret=%d", ret);
}

ZTEST(api, api_sha256_kat)
{
    int ret = test_sha256();

    zassert_equal(ret, 0, "test_sha256 ret=%d", ret);
}

ZTEST(api, api_sha256_varlen)
{
    int ret = test_sha256_various_lengths();

    zassert_equal(ret, 0, "sha256_varlen ret=%d", ret);
}

ZTEST(api, api_sha384_kat)
{
    int ret = test_sha384();

    zassert_equal(ret, 0, "test_sha384 ret=%d", ret);
}

ZTEST(api, api_sha512_kat)
{
    int ret = test_sha512();

    zassert_equal(ret, 0, "test_sha512 ret=%d", ret);
}

ZTEST(api, api_sm3_kat)
{
    int ret = test_sm3();

    zassert_equal(ret, 0, "test_sm3 ret=%d", ret);
}

#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
ZTEST(api, api_sm4_kat)
{
    int ret = test_sm4();

    zassert_equal(ret, 0, "test_sm4 ret=%d", ret);
    if (ret == 0) {
        ret = sm4_ctr_test();
        zassert_equal(ret, 0, "sm4_ctr_test ret=%d", ret);
    }
}
#endif

/* ---- perf 套件：ECDSA KAT/p384/sm2 + 性能测量（mix 之后） ---- */

#if defined(CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT)
ZTEST(perf, api_ecdsa_p256_kat)
{
    int ret = ecdsa_p256_kat_test();

    zassert_equal(ret, 0, "ecdsa_p256_kat ret=%d", ret);
}

ZTEST(perf, api_ecdsa_p384)
{
    int ret = otbn_ecdsa_curve_once(MBEDTLS_ECP_DP_SECP384R1);

    zassert_equal(ret, 0, "ecdsa_p384 ret=%d", ret);
}

ZTEST(perf, api_ecdsa_sm2)
{
    int ret = otbn_ecdsa_curve_once(MBEDTLS_ECP_DP_SM2);

    zassert_equal(ret, 0, "ecdsa_sm2 ret=%d", ret);
}
#endif

ZTEST(perf, perf_sha256_64k)
{
    uint32_t us = 0;
    int ret = perf_sha256_64k(&us);

    ZT_INFO("perf_sha256_64k", "bytes=%u,us=%u,kbps=%u",
            65536u, us, us ? (65536u * 1000u / us) : 0u);
    zassert_equal(ret, 0, "perf_sha256_64k ret=%d", ret);
}

#if defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)
ZTEST(perf, perf_rsa2048_sign)
{
    uint32_t avg_us = 0;
    int ret = perf_rsa2048_sign(&avg_us, 5);

    ZT_INFO("perf_rsa2048_sign", "loops=%u,avg_us=%u", 5u, avg_us);
    zassert_equal(ret, 0, "perf_rsa2048_sign ret=%d", ret);
}
#endif

#if defined(CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT)
ZTEST(perf, perf_ecdsa_p256)
{
    uint32_t sign_ops = 0, verify_ops = 0;
    int ret = perf_ecdsa_p256(&sign_ops, &verify_ops, 10);

    ZT_INFO("perf_ecdsa_p256", "loops=%u,sign_ops=%u,verify_ops=%u",
            10u, sign_ops, verify_ops);
    zassert_equal(ret, 0, "perf_ecdsa_p256 ret=%d", ret);
}
#endif
