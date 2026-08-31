/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 3：OTBN 固件混用复现测试（已知问题：RSA 密钥生成与 SHA256 来回混用
 * 会导致结果出错）。
 *
 * 背景：OTBN 是独占式协处理器，hash（otbn_hash.c）与 RSA（rsa_alt.c）/
 * ECDSA（ecdsa_alt.c）各自加载不同 IMEM 固件、共用同一块 DMEM。混用场景的
 * 正确性依赖两层状态恢复：
 *   1. hash 中间态保存在 CPU 侧 ctx->state_bytes，每次 update/final 前
 *      restore_state() 写回 DMEM；
 *   2. IMEM 固件加载用 s_last_firmware_id 静态缓存跳过重复加载——
 *      若 RSA keygen 在两次 hash 操作之间改写了 IMEM 而缓存未失效，
 *      后续 hash 会在 RSA 固件上执行，结果出错（本组测试的复现目标）。
 *
 * 每个 mix 测试都自带独立基准（同一消息先单独算出 KAT/参考值），混用后的
 * 结果必须与基准逐字节一致，差异即判 FAIL 并给出可归因 reason（§13.5）。
 *
 * 纯 ztest：每项 mix 场景是一个 ZTEST(mix, <name>)，断言 zassert_*，数值
 * 上报 ZT_INFO（§13.4，断言之前）；结果记账由 common/ztest_glue 自动完成。
 * "mix" 套件按链接段 SORT_BY_NAME 升序排在 api 之后、perf/z_keygen 之前
 * （字母序 api < mix < perf < z_keygen，与原始阶段序 api → mix → 后续 一致）。
 */

#include "otbn_test.h"
#include "ztest_glue.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/sm3_alt.h"

#if defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)
#include "mbedtls/rsa.h"
#endif

#if defined(CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT)
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#endif

ZTEST_SUITE(mix, NULL, NULL, NULL, NULL, NULL);

#define SHA256_DIGEST_SIZE 32
#define SHA512_DIGEST_SIZE 64
#define SM3_DIGEST_SIZE 32

/* 混用测试统一消息：56 字节经典向量输入（覆盖多块/填充边界） */
static const char mix_msg[] =
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

/* sha256(mix_msg) 官方 KAT */
static const uint8_t mix_msg_sha256[SHA256_DIGEST_SIZE] = {
    0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
    0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
    0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
    0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1
};

/* sha512(mix_msg) —— 同输入的 128 字节块算法对照 */
static const uint8_t mix_msg_sha512[SHA512_DIGEST_SIZE] = {
    0x20, 0x4a, 0x8f, 0xc6, 0xdd, 0xa8, 0x2f, 0x0a,
    0x0c, 0xed, 0x7b, 0xeb, 0x8e, 0x08, 0xa4, 0x16,
    0x57, 0xc1, 0x6e, 0xf4, 0x68, 0xb2, 0x28, 0xa8,
    0x27, 0x9b, 0xe3, 0x31, 0xa7, 0x03, 0xc3, 0x35,
    0x96, 0xfd, 0x15, 0xc1, 0x3b, 0x1b, 0x07, 0xf9,
    0xaa, 0x1d, 0x3b, 0xea, 0x57, 0x78, 0x9c, 0xa0,
    0x31, 0xad, 0x85, 0xc7, 0xa7, 0x1d, 0xd7, 0x03,
    0x54, 0xec, 0x63, 0x12, 0x38, 0xca, 0x34, 0x45
};

/* sm3(mix_msg) 官方 KAT */
static const uint8_t mix_msg_sm3[SM3_DIGEST_SIZE] = {
    0x63, 0x9b, 0x6c, 0xc5, 0xe6, 0x4d, 0x9e, 0x37,
    0xa3, 0x90, 0xb1, 0x92, 0xdf, 0x4f, 0xa1, 0xea,
    0x07, 0x20, 0xab, 0x74, 0x7f, 0xf6, 0x92, 0xb9,
    0xf3, 0x8c, 0x4e, 0x66, 0xad, 0x7b, 0x8c, 0x05
};

#if defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)

/* 一次性 sha256（one-shot 基准/混用后用例共用） */
static int sha256_oneshot(const uint8_t *data, size_t len, uint8_t out[SHA256_DIGEST_SIZE])
{
    mbedtls_sha256_context ctx;
    int ret;

    mbedtls_sha256_init(&ctx);
    ret = mbedtls_sha256_starts(&ctx, 0);
    if (ret == 0) {
        ret = mbedtls_sha256_update(&ctx, data, len);
    }
    if (ret == 0) {
        ret = mbedtls_sha256_finish(&ctx, out);
    }
    mbedtls_sha256_free(&ctx);
    return ret;
}

/*
 * mix 1：sha256 KAT → RSA-2048 keygen → sha256 KAT。
 * 复现点：keygen 把 OTBN IMEM 换成 RSA 固件；其后首个 hash 若因
 * s_last_firmware_id 缓存未失效而跳过固件重载，第二次 sha256 结果出错。
 */
ZTEST(mix, mix_sha256_keygen_seq)
{
    uint8_t h1[SHA256_DIGEST_SIZE], h2[SHA256_DIGEST_SIZE];
    uint32_t keygen_ms = 0;
    int ret;

    ret = sha256_oneshot((const uint8_t *)mix_msg, sizeof(mix_msg) - 1, h1);
    zassert_equal(ret, 0, "sha256#1 ret=%d", ret);
    zassert_hash_eq(h1, mix_msg_sha256, SHA256_DIGEST_SIZE, "sha256#1");

    ret = otbn_rsa_keygen_check(2048, &keygen_ms);
    zassert_equal(ret, 0, "keygen2048 ret=%d", ret);

    ret = sha256_oneshot((const uint8_t *)mix_msg, sizeof(mix_msg) - 1, h2);
    zassert_equal(ret, 0, "sha256#2 ret=%d", ret);
    zassert_hash_eq(h2, mix_msg_sha256, SHA256_DIGEST_SIZE, "sha256#2");

    ZT_INFO("mix_sha256_keygen_seq", "keygen_ms=%u", keygen_ms);

    zassert_hash_eq(h2, h1, SHA256_DIGEST_SIZE, "sha256#1vs#2");
}

/*
 * mix 2：sha256 流式（update 一半）→ RSA-2048 keygen → update 完 → finish。
 * 复现点：流式 hash 的中间态/DMEM 消息缓冲被 keygen 的 DMEM 写入覆盖，
 * 或 keygen 后固件未重载，finish 结果与 KAT 不符。
 */
ZTEST(mix, mix_sha256_stream_keygen)
{
    mbedtls_sha256_context ctx;
    uint8_t hash[SHA256_DIGEST_SIZE];
    uint32_t keygen_ms = 0;
    size_t half = (sizeof(mix_msg) - 1) / 2;
    int ret;

    mbedtls_sha256_init(&ctx);
    ret = mbedtls_sha256_starts(&ctx, 0);
    zassert_equal(ret, 0, "stream starts ret=%d", ret);

    ret = mbedtls_sha256_update(&ctx, (const uint8_t *)mix_msg, half);
    zassert_equal(ret, 0, "stream update#1 ret=%d", ret);

    /* 流式 hash 上下文挂着，插入完整 keygen（含 OTBN session/固件切换） */
    {
        int kret = otbn_rsa_keygen_check(2048, &keygen_ms);

        zassert_equal(kret, 0, "keygen2048 ret=%d", kret);
    }

    ret = mbedtls_sha256_update(&ctx, (const uint8_t *)mix_msg + half,
                                (sizeof(mix_msg) - 1) - half);
    zassert_equal(ret, 0, "stream update#2 ret=%d", ret);

    ret = mbedtls_sha256_finish(&ctx, hash);
    zassert_equal(ret, 0, "stream finish ret=%d", ret);
    mbedtls_sha256_free(&ctx);

    ZT_INFO("mix_sha256_stream_keygen", "keygen_ms=%u", keygen_ms);

    zassert_hash_eq(hash, mix_msg_sha256, SHA256_DIGEST_SIZE, "stream_digest");
}

/*
 * mix 3：keygen → sha256 KAT → keygen。
 * 复现点（反向）：keygen 之后跑 hash 再 keygen，第二次 keygen 的
 * DMEM/IMEM 现场被 hash 固件破坏，生成密钥应仍能通过 check_privkey +
 * sign/verify 全流程校验。
 */
ZTEST(mix, mix_keygen_sha256_keygen)
{
    uint8_t h[SHA256_DIGEST_SIZE];
    uint32_t kg1_ms = 0, kg2_ms = 0;
    int ret;

    ret = otbn_rsa_keygen_check(2048, &kg1_ms);
    zassert_equal(ret, 0, "keygen#1 ret=%d", ret);

    ret = sha256_oneshot((const uint8_t *)mix_msg, sizeof(mix_msg) - 1, h);
    zassert_equal(ret, 0, "sha256 ret=%d", ret);
    zassert_hash_eq(h, mix_msg_sha256, SHA256_DIGEST_SIZE, "sha256");

    /* keygen_check 内含 check_privkey + sign + verify */
    ret = otbn_rsa_keygen_check(2048, &kg2_ms);
    zassert_equal(ret, 0, "keygen#2 ret=%d", ret);

    ZT_INFO("mix_keygen_sha256_keygen", "keygen1_ms=%u,keygen2_ms=%u",
            kg1_ms, kg2_ms);
}

/*
 * mix 4：sha256 流式中插入 RSA modexp（sign/verify，固定导入 key）。
 * 复现点：RSA_MODEXP 固件与 hash 固件切换时 hash 流式上下文应无损。
 */
ZTEST(mix, mix_sign_sha256_stream)
{
    mbedtls_sha256_context ctx;
    mbedtls_rsa_context rsa;
    uint8_t hash[SHA256_DIGEST_SIZE];
    uint8_t sig[512];
    const unsigned char sign_msg[] = "mix sign sha256 stream";
    uint8_t sign_hash[SHA256_DIGEST_SIZE];
    size_t half = (sizeof(mix_msg) - 1) / 2;
    int ret;

    ret = otbn_rsa_import_test_key(&rsa);
    zassert_equal(ret, 0, "rsa import ret=%d", ret);

    /* 签名用的 digest 先行 one-shot 算出 */
    ret = sha256_oneshot(sign_msg, sizeof(sign_msg) - 1, sign_hash);
    zassert_equal(ret, 0, "sign_hash ret=%d", ret);

    mbedtls_sha256_init(&ctx);
    ret = mbedtls_sha256_starts(&ctx, 0);
    zassert_equal(ret, 0, "stream starts ret=%d", ret);

    ret = mbedtls_sha256_update(&ctx, (const uint8_t *)mix_msg, half);
    zassert_equal(ret, 0, "stream update#1 ret=%d", ret);

    /* 流式 hash 挂着，插入 RSA modexp sign + verify */
    {
        int sret = mbedtls_rsa_pkcs1_sign(&rsa, otbn_test_rng, NULL,
                                          MBEDTLS_MD_SHA256, 32, sign_hash, sig);

        zassert_equal(sret, 0, "rsa sign ret=%d", sret);

        sret = mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256, 32,
                                        sign_hash, sig);
        zassert_equal(sret, 0, "rsa verify ret=%d", sret);
    }

    ret = mbedtls_sha256_update(&ctx, (const uint8_t *)mix_msg + half,
                                (sizeof(mix_msg) - 1) - half);
    zassert_equal(ret, 0, "stream update#2 ret=%d", ret);

    ret = mbedtls_sha256_finish(&ctx, hash);
    zassert_equal(ret, 0, "stream finish ret=%d", ret);
    mbedtls_sha256_free(&ctx);
    mbedtls_rsa_free(&rsa);

    zassert_hash_eq(hash, mix_msg_sha256, SHA256_DIGEST_SIZE, "stream_digest");
}

/*
 * mix 5：sha512 流式中插入 RSA-2048 keygen（128 字节块算法同款场景）。
 */
ZTEST(mix, mix_sha512_stream_keygen)
{
    mbedtls_sha512_context ctx;
    uint8_t hash[SHA512_DIGEST_SIZE];
    uint32_t keygen_ms = 0;
    size_t half = (sizeof(mix_msg) - 1) / 2;
    int ret;

    mbedtls_sha512_init(&ctx);
    ret = mbedtls_sha512_starts(&ctx, 0);
    zassert_equal(ret, 0, "sha512 starts ret=%d", ret);

    ret = mbedtls_sha512_update(&ctx, (const uint8_t *)mix_msg, half);
    zassert_equal(ret, 0, "sha512 update#1 ret=%d", ret);

    {
        int kret = otbn_rsa_keygen_check(2048, &keygen_ms);

        zassert_equal(kret, 0, "keygen2048 ret=%d", kret);
    }

    ret = mbedtls_sha512_update(&ctx, (const uint8_t *)mix_msg + half,
                                (sizeof(mix_msg) - 1) - half);
    zassert_equal(ret, 0, "sha512 update#2 ret=%d", ret);

    ret = mbedtls_sha512_finish(&ctx, hash);
    zassert_equal(ret, 0, "sha512 finish ret=%d", ret);
    mbedtls_sha512_free(&ctx);

    ZT_INFO("mix_sha512_stream_keygen", "keygen_ms=%u", keygen_ms);

    zassert_hash_eq(hash, mix_msg_sha512, SHA512_DIGEST_SIZE, "sha512_digest");
}

/*
 * mix 6：sm3 流式中插入 RSA-2048 keygen（国密 hash 同款场景）。
 */
ZTEST(mix, mix_sm3_stream_keygen)
{
    mbedtls_sm3_context ctx;
    uint8_t hash[SM3_DIGEST_SIZE];
    uint32_t keygen_ms = 0;
    size_t half = (sizeof(mix_msg) - 1) / 2;
    int ret;

    mbedtls_sm3_init(&ctx);
    ret = mbedtls_sm3_starts(&ctx);
    zassert_equal(ret, 0, "sm3 starts ret=%d", ret);

    ret = mbedtls_sm3_update(&ctx, (const uint8_t *)mix_msg, half);
    zassert_equal(ret, 0, "sm3 update#1 ret=%d", ret);

    {
        int kret = otbn_rsa_keygen_check(2048, &keygen_ms);

        zassert_equal(kret, 0, "keygen2048 ret=%d", kret);
    }

    ret = mbedtls_sm3_update(&ctx, (const uint8_t *)mix_msg + half,
                             (sizeof(mix_msg) - 1) - half);
    zassert_equal(ret, 0, "sm3 update#2 ret=%d", ret);

    ret = mbedtls_sm3_finish(&ctx, hash);
    zassert_equal(ret, 0, "sm3 finish ret=%d", ret);
    mbedtls_sm3_free(&ctx);

    ZT_INFO("mix_sm3_stream_keygen", "keygen_ms=%u", keygen_ms);

    zassert_hash_eq(hash, mix_msg_sm3, SM3_DIGEST_SIZE, "sm3_digest");
}
#endif /* CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT */

#if defined(CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT)
/*
 * mix 7：sha256 流式中插入 ECDSA P-256 genkey+sign+verify。
 * 复现点：ECDSA 固件与 hash 固件切换时 hash 流式上下文应无损。
 */
ZTEST(mix, mix_ecdsa_sha256_stream)
{
    mbedtls_sha256_context ctx;
    uint8_t hash[SHA256_DIGEST_SIZE];
    size_t half = (sizeof(mix_msg) - 1) / 2;
    int ret;

    mbedtls_sha256_init(&ctx);
    ret = mbedtls_sha256_starts(&ctx, 0);
    zassert_equal(ret, 0, "stream starts ret=%d", ret);

    ret = mbedtls_sha256_update(&ctx, (const uint8_t *)mix_msg, half);
    zassert_equal(ret, 0, "stream update#1 ret=%d", ret);

    {
        int eret = otbn_ecdsa_curve_once(MBEDTLS_ECP_DP_SECP256R1);

        zassert_equal(eret, 0, "ecdsa p256 ret=%d", eret);
    }

    ret = mbedtls_sha256_update(&ctx, (const uint8_t *)mix_msg + half,
                                (sizeof(mix_msg) - 1) - half);
    zassert_equal(ret, 0, "stream update#2 ret=%d", ret);

    ret = mbedtls_sha256_finish(&ctx, hash);
    zassert_equal(ret, 0, "stream finish ret=%d", ret);
    mbedtls_sha256_free(&ctx);

    zassert_hash_eq(hash, mix_msg_sha256, SHA256_DIGEST_SIZE, "stream_digest");
}
#endif /* CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT */
