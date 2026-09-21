/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 1：mbedtls selftest 套件（移植自 samples/mbedtls_selftest/src/main.c，
 * 测试内容保持一致，每个套件一个 ZTEST，纯 ztest 框架执行）。
 *
 * 差异点（相对原 sample）：
 *   - main() 的平台检查（SIZE_MAX / padding bits / NULL 表示）保留在 calloc 测试
 *     前的 sanity 段落，失败计入 st_calloc；
 *   - 每个套件一个 ZTEST 注册（zassert_equal 判定 ret==0），verbose 输出原样
 *     保留（失败细节靠 mbedtls_printf 打出的 "FAILED" 行定位）；
 *   - 套件是否编译由原 sample 的 ifdefs 决定，未编译的套件不注册（对应槽位
 *     保持 NOT_RUN，dump_results.py 内存兜底按链接序读）；
 *   - mbedtls heap（MBEDTLS_MEMORY_BUFFER_ALLOC_C）在 "st" 套件 setup 里 init
 *     一次；st_memory_buffer_alloc 单独放 "st_heap" 套件——它先
 *     mbedtls_memory_buffer_alloc_free()（清零全局 heap 上下文）再 self-test，
 *     之后任何 mbedtls 调用都会拿不到堆，必须最后跑（字母序 st < st_heap）。
 *
 * 纯 ztest：结果记账由 common/ztest_glue 的 zt_run_all 自动完成（测试执行序 =
 * 链接段 SORT_BY_NAME 升序，非源码声明序）。
 */

#include "mbedtls/build_info.h"

#include "mbedtls/entropy.h"
#include "mbedtls/hmac_drbg.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/dhm.h"
#include "mbedtls/gcm.h"
#include "mbedtls/ccm.h"
#include "mbedtls/cmac.h"
#include "mbedtls/md5.h"
#include "mbedtls/ripemd160.h"
#include "mbedtls/sha1.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/sha3.h"
#include "mbedtls/des.h"
#include "mbedtls/aes.h"
#include "mbedtls/camellia.h"
#include "mbedtls/aria.h"
#include "mbedtls/chacha20.h"
#include "mbedtls/poly1305.h"
#include "mbedtls/chachapoly.h"
#include "mbedtls/base64.h"
#include "mbedtls/bignum.h"
#include "mbedtls/rsa.h"
#include "mbedtls/x509.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecjpake.h"
#include "mbedtls/timing.h"
#include "mbedtls/nist_kw.h"
#include "mbedtls/debug.h"

#include <limits.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "mbedtls/platform.h"

#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
#include "mbedtls/memory_buffer_alloc.h"
#endif

/* ========== mbedtls heap（MBEDTLS_MEMORY_BUFFER_ALLOC_C） ========== */

#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
static unsigned char st_heap_buf[32768];

/* 整个 selftest 期间共享的 mbedtls heap：在 "st" 套件首个测试前 init 一次；
 * st_heap 套件的 memory_buffer_alloc 测试会把它 free（清零），故必须最后跑。
 * 注：ztest 的 setup_fn 签名是 void *(*)(void)，必须返回 void*（此处无 fixture，
 * 返回 NULL）。 */
static void *st_heap_init(void)
{
	mbedtls_memory_buffer_alloc_init(st_heap_buf, sizeof(st_heap_buf));
	return NULL;
}
#endif /* MBEDTLS_MEMORY_BUFFER_ALLOC_C */

#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
ZTEST_SUITE(st, NULL, st_heap_init, NULL, NULL, NULL);
#else
ZTEST_SUITE(st, NULL, NULL, NULL, NULL, NULL);
#endif
/* memory_buffer_alloc 必须最后执行（free 后 heap 不可用），单独套件；
 * 字母序 st < st_heap，保证在 st 全部测试之后。 */
ZTEST_SUITE(st_heap, NULL, NULL, NULL, NULL, NULL);

#if defined MBEDTLS_SELF_TEST
/* Sanity check for malloc. This is not expected to fail, and is rather
 * intended to display potentially useful information about the platform,
 * in particular the behavior of malloc(0). */
static int calloc_self_test(int verbose)
{
    int failures = 0;
    void *empty1 = mbedtls_calloc(0, 1);
    void *empty2 = mbedtls_calloc(0, 1);
    void *buffer1 = mbedtls_calloc(1, 1);
    void *buffer2 = mbedtls_calloc(1, 1);
    unsigned int buffer_3_size = 256;
    unsigned int buffer_4_size = 4097; /* Allocate more than the usual page size */
    unsigned char *buffer3 = mbedtls_calloc(buffer_3_size, 1);
    unsigned char *buffer4 = mbedtls_calloc(buffer_4_size, 1);

    if (empty1 == NULL && empty2 == NULL) {
        if (verbose) {
            mbedtls_printf("  CALLOC(0,1): passed (NULL)\n");
        }
    } else if (empty1 == NULL || empty2 == NULL) {
        if (verbose) {
            mbedtls_printf("  CALLOC(0,1): failed (mix of NULL and non-NULL)\n");
        }
        ++failures;
    } else if (empty1 == empty2) {
        if (verbose) {
            mbedtls_printf("  CALLOC(0,1): passed (same non-null)\n");
        }
        empty2 = NULL;
    } else {
        if (verbose) {
            mbedtls_printf("  CALLOC(0,1): passed (distinct non-null)\n");
        }
    }

    mbedtls_free(empty1);
    mbedtls_free(empty2);

    empty1 = mbedtls_calloc(1, 0);
    empty2 = mbedtls_calloc(1, 0);
    if (empty1 == NULL && empty2 == NULL) {
        if (verbose) {
            mbedtls_printf("  CALLOC(1,0): passed (NULL)\n");
        }
    } else if (empty1 == NULL || empty2 == NULL) {
        if (verbose) {
            mbedtls_printf("  CALLOC(1,0): failed (mix of NULL and non-NULL)\n");
        }
        ++failures;
    } else if (empty1 == empty2) {
        if (verbose) {
            mbedtls_printf("  CALLOC(1,0): passed (same non-null)\n");
        }
        empty2 = NULL;
    } else {
        if (verbose) {
            mbedtls_printf("  CALLOC(1,0): passed (distinct non-null)\n");
        }
    }

    if (buffer1 == NULL || buffer2 == NULL) {
        mbedtls_printf("  CALLOC(1): failed (NULL)\n");
        ++failures;
    } else if (buffer1 == buffer2) {
        mbedtls_printf("  CALLOC(1): failed (same buffer twice)\n");
        ++failures;
        buffer2 = NULL;
    } else {
        if (verbose) {
            mbedtls_printf("  CALLOC(1): passed\n");
        }
    }

    mbedtls_free(buffer1);
    buffer1 = mbedtls_calloc(1, 1);
    if (buffer1 == NULL) {
        mbedtls_printf("  CALLOC(1 again): failed (NULL)\n");
        ++failures;
    } else {
        if (verbose) {
            mbedtls_printf("  CALLOC(1 again): passed\n");
        }
    }

    if (buffer3) {
        for (unsigned int i = 0; i < buffer_3_size; i++) {
            if (buffer3[i] != 0) {
                ++failures;
                if (verbose) {
                    mbedtls_printf("  CALLOC(%u): failed (memory not initialized to 0)\n",
                           buffer_3_size);
                }
                break;
            }
        }
    }

    if (buffer4) {
        for (unsigned int i = 0; i < buffer_4_size; i++) {
            if (buffer4[i] != 0) {
                ++failures;
                if (verbose) {
                    mbedtls_printf("  CALLOC(%u): failed (memory not initialized to 0)\n",
                           buffer_4_size);
                }
                break;
            }
        }
    }

    if (verbose != 0) {
        mbedtls_printf("\n");
    }
    mbedtls_free(empty1);
    mbedtls_free(empty2);
    mbedtls_free(buffer1);
    mbedtls_free(buffer2);
    mbedtls_free(buffer3);
    mbedtls_free(buffer4);
    return failures;
}
#endif /* MBEDTLS_SELF_TEST */

static int test_snprintf(size_t n, const char *ref_buf, int ref_ret)
{
    int ret;
    char buf[10] = "xxxxxxxxx";
    const char ref[10] = "xxxxxxxxx";

    ret = mbedtls_snprintf(buf, n, "%s", "123");
    if (ret < 0 || (size_t) ret >= n) {
        ret = -1;
    }

    if (strncmp(ref_buf, buf, sizeof(buf)) != 0 ||
        ref_ret != ret ||
        memcmp(buf + n, ref + n, sizeof(buf) - n) != 0) {
        return 1;
    }

    return 0;
}

static int run_test_snprintf(void)
{
    return test_snprintf(0, "xxxxxxxxx",  -1) != 0 ||
           test_snprintf(1, "",           -1) != 0 ||
           test_snprintf(2, "1",          -1) != 0 ||
           test_snprintf(3, "12",         -1) != 0 ||
           test_snprintf(4, "123",         3) != 0 ||
           test_snprintf(5, "123",         3) != 0;
}

/* 平台 sanity 检查（原 sample main() 开头段落）：结果并入 st_calloc 测试。 */
static int platform_sanity_check(void)
{
    int failures = 0;
    void *pointer;

    if (SIZE_MAX < INT_MAX || SIZE_MAX < UINT_MAX) {
        mbedtls_printf("SIZE_MAX must be at least as big as INT_MAX and UINT_MAX\n");
        failures++;
    }

    if (sizeof(int) < 4) {
        mbedtls_printf("int must be at least 32 bits\n");
        failures++;
    }

    if (sizeof(size_t) < 4) {
        mbedtls_printf("size_t must be at least 32 bits\n");
        failures++;
    }

    uint32_t endian_test = 0x12345678;
    char *p = (char *) &endian_test;
    if (!(p[0] == 0x12 && p[1] == 0x34 && p[2] == 0x56 && p[3] == 0x78) &&
        !(p[3] == 0x12 && p[2] == 0x34 && p[1] == 0x56 && p[0] == 0x78)) {
        mbedtls_printf("Mixed-endian platforms are not supported\n");
        failures++;
    }

    memset(&pointer, 0, sizeof(void *));
    if (pointer != NULL) {
        mbedtls_printf("all-bits-zero is not a NULL pointer\n");
        failures++;
    }

    /* snprintf 参考向量（run_test_snprintf 内含 6 组确定性用例）不匹配即计入
     * failures。旧版只打 `[warn]`、不累加 ⇒ 该子检查**永不可 FAIL**
     * （测试有效性审计 A 组 P1-1）。
     * 选"恢复 failures++"而非"删除向量"的理由：本工程未定义
     * MBEDTLS_PLATFORM_SNPRINTF_ALT（prj.conf 无对应 Kconfig），mbedtls_snprintf
     * 即 libc（picolibc）的 snprintf；对常量入参，C99 语义下的返回/截断/NUL 保留
     * 是确定的，参考向量在合规平台必然通过 ⇒ 不匹配属**真实平台缺陷**，应当 FAIL。 */
    if (run_test_snprintf() != 0) {
        mbedtls_printf("  SNPRINTF: failed (run_test_snprintf reference vector mismatch)\n");
        failures++;
    }

    return failures;
}

#if defined(MBEDTLS_SELF_TEST) && defined(MBEDTLS_ENTROPY_C)
#if defined(MBEDTLS_ENTROPY_NV_SEED) && !defined(MBEDTLS_NO_PLATFORM_ENTROPY)
static void create_entropy_seed_file(void)
{
    int result;
    size_t output_len = 0;
    unsigned char seed_value[MBEDTLS_ENTROPY_BLOCK_SIZE];

    /* Attempt to read the entropy seed file. If this fails - attempt to write
     * to the file to ensure one is present. */
    result = mbedtls_platform_std_nv_seed_read(seed_value,
                                                MBEDTLS_ENTROPY_BLOCK_SIZE);
    if (0 == result) {
        return;
    }

    result = mbedtls_platform_entropy_poll(NULL,
                                            seed_value,
                                            MBEDTLS_ENTROPY_BLOCK_SIZE,
                                            &output_len);
    if (0 != result) {
        return;
    }

    if (MBEDTLS_ENTROPY_BLOCK_SIZE != output_len) {
        return;
    }

    mbedtls_platform_std_nv_seed_write(seed_value, MBEDTLS_ENTROPY_BLOCK_SIZE);
}
#endif

static int mbedtls_entropy_self_test_wrapper(int verbose)
{
#if defined(MBEDTLS_ENTROPY_NV_SEED) && !defined(MBEDTLS_NO_PLATFORM_ENTROPY)
    create_entropy_seed_file();
#endif
    return mbedtls_entropy_self_test(verbose);
}
#endif

#if defined(MBEDTLS_SELF_TEST)
#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
static int mbedtls_memory_buffer_alloc_free_and_self_test(int verbose)
{
    if (verbose != 0) {
#if defined(MBEDTLS_MEMORY_DEBUG)
        mbedtls_memory_buffer_alloc_status();
#endif
    }
    mbedtls_memory_buffer_alloc_free();
    return mbedtls_memory_buffer_alloc_self_test(verbose);
}
#endif

/* ========== ZTEST 注册（每套件一项；执行序 = 链接段 SORT_BY_NAME 升序） ========== */

ZTEST(st, st_calloc)
{
    int ret = platform_sanity_check();

    if (ret == 0) {
        ret = calloc_self_test(1);
    }
    zassert_equal(ret, 0, "calloc self_test ret=%d", ret);
}

#if defined(MBEDTLS_MD5_C)
ZTEST(st, st_md5)
{
    int ret = mbedtls_md5_self_test(1);

    zassert_equal(ret, 0, "mbedtls_md5_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_RIPEMD160_C)
ZTEST(st, st_ripemd160)
{
    int ret = mbedtls_ripemd160_self_test(1);

    zassert_equal(ret, 0, "mbedtls_ripemd160_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_SHA1_C)
ZTEST(st, st_sha1)
{
    int ret = mbedtls_sha1_self_test(1);

    zassert_equal(ret, 0, "mbedtls_sha1_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_SHA224_C)
ZTEST(st, st_sha224)
{
    int ret = mbedtls_sha224_self_test(1);

    zassert_equal(ret, 0, "mbedtls_sha224_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_SHA256_C)
ZTEST(st, st_sha256)
{
    int ret = mbedtls_sha256_self_test(1);

    zassert_equal(ret, 0, "mbedtls_sha256_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_SHA384_C)
ZTEST(st, st_sha384)
{
    int ret = mbedtls_sha384_self_test(1);

    zassert_equal(ret, 0, "mbedtls_sha384_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_SHA512_C)
ZTEST(st, st_sha512)
{
    int ret = mbedtls_sha512_self_test(1);

    zassert_equal(ret, 0, "mbedtls_sha512_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_SHA3_C)
ZTEST(st, st_sha3)
{
    int ret = mbedtls_sha3_self_test(1);

    zassert_equal(ret, 0, "mbedtls_sha3_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_DES_C)
ZTEST(st, st_des)
{
    int ret = mbedtls_des_self_test(1);

    zassert_equal(ret, 0, "mbedtls_des_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_AES_C)
ZTEST(st, st_aes)
{
    int ret = mbedtls_aes_self_test(1);

    zassert_equal(ret, 0, "mbedtls_aes_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_GCM_C) && defined(MBEDTLS_AES_C)
ZTEST(st, st_gcm)
{
    int ret = mbedtls_gcm_self_test(1);

    zassert_equal(ret, 0, "mbedtls_gcm_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_CCM_C) && defined(MBEDTLS_AES_C)
ZTEST(st, st_ccm)
{
    int ret = mbedtls_ccm_self_test(1);

    zassert_equal(ret, 0, "mbedtls_ccm_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_NIST_KW_C) && defined(MBEDTLS_AES_C)
ZTEST(st, st_nist_kw)
{
    int ret = mbedtls_nist_kw_self_test(1);

    zassert_equal(ret, 0, "mbedtls_nist_kw_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_CMAC_C)
ZTEST(st, st_cmac)
{
    int ret = mbedtls_cmac_self_test(1);

    zassert_equal(ret, 0, "mbedtls_cmac_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_CHACHA20_C)
ZTEST(st, st_chacha20)
{
    int ret = mbedtls_chacha20_self_test(1);

    zassert_equal(ret, 0, "mbedtls_chacha20_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_POLY1305_C)
ZTEST(st, st_poly1305)
{
    int ret = mbedtls_poly1305_self_test(1);

    zassert_equal(ret, 0, "mbedtls_poly1305_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_CHACHAPOLY_C)
ZTEST(st, st_chachapoly)
{
    int ret = mbedtls_chachapoly_self_test(1);

    zassert_equal(ret, 0, "mbedtls_chachapoly_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_BASE64_C)
ZTEST(st, st_base64)
{
    int ret = mbedtls_base64_self_test(1);

    zassert_equal(ret, 0, "mbedtls_base64_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_BIGNUM_C)
ZTEST(st, st_mpi)
{
    int ret = mbedtls_mpi_self_test(1);

    zassert_equal(ret, 0, "mbedtls_mpi_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_RSA_C)
/* st_rsa 用的是共享树 rsa.c 内硬编码的 RSA-1024（KEY_LEN 128）⇒ ctx->len==128，
 * 而 OTBN hook 门限是 256/384/512 字节（rsa_alt.c:157-159），hook 直接
 * return -ENOTSUP 后**静默回落软件** ⇒ 本项是**纯软件 RSA 基线**，对 OTBN
 * 零覆盖（结果与关掉 CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT 逐字节相同）。
 * 覆盖声明已相应降级，见 tests/COVERAGE.md「修订」节；OTBN RSA 通路的判别力
 * 由 projects/mbedtls_test 的 rsa2048_otbn_hit 提供。 */
ZTEST(st, st_rsa)
{
    int ret = mbedtls_rsa_self_test(1);

    zassert_equal(ret, 0, "mbedtls_rsa_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_CAMELLIA_C)
ZTEST(st, st_camellia)
{
    int ret = mbedtls_camellia_self_test(1);

    zassert_equal(ret, 0, "mbedtls_camellia_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_ARIA_C)
ZTEST(st, st_aria)
{
    int ret = mbedtls_aria_self_test(1);

    zassert_equal(ret, 0, "mbedtls_aria_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_CTR_DRBG_C)
ZTEST(st, st_ctr_drbg)
{
    int ret = mbedtls_ctr_drbg_self_test(1);

    zassert_equal(ret, 0, "mbedtls_ctr_drbg_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_HMAC_DRBG_C)
ZTEST(st, st_hmac_drbg)
{
    int ret = mbedtls_hmac_drbg_self_test(1);

    zassert_equal(ret, 0, "mbedtls_hmac_drbg_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_ECP_C)
ZTEST(st, st_ecp)
{
    int ret = mbedtls_ecp_self_test(1);

    zassert_equal(ret, 0, "mbedtls_ecp_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_ECJPAKE_C)
ZTEST(st, st_ecjpake)
{
    int ret = mbedtls_ecjpake_self_test(1);

    zassert_equal(ret, 0, "mbedtls_ecjpake_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_DHM_C)
ZTEST(st, st_dhm)
{
    int ret = mbedtls_dhm_self_test(1);

    zassert_equal(ret, 0, "mbedtls_dhm_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_ENTROPY_C)
ZTEST(st, st_entropy)
{
    int ret = mbedtls_entropy_self_test_wrapper(1);

    zassert_equal(ret, 0, "mbedtls_entropy_self_test ret=%d", ret);
}
#endif

#if defined(MBEDTLS_PKCS5_C)
ZTEST(st, st_pkcs5)
{
    int ret = mbedtls_pkcs5_self_test(1);

    zassert_equal(ret, 0, "mbedtls_pkcs5_self_test ret=%d", ret);
}
#endif

/* Heap test comes last（"st_heap" 套件：free 后 heap 不可再用于后续测试） */
#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
ZTEST(st_heap, st_memory_buffer_alloc)
{
    int ret = mbedtls_memory_buffer_alloc_free_and_self_test(1);

    zassert_equal(ret, 0, "mbedtls_memory_buffer_alloc_self_test ret=%d", ret);
}
#endif
#endif /* MBEDTLS_SELF_TEST */
