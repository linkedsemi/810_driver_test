/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 鲁棒性测试：异常/临界输入、状态污染、并发共享上下文。
 *
 * 判定 = 不 HardFault、返回码符合文档或保持静默失败；
 * 不追求所有异常都返回 -EINVAL。
 */

#include "mbedtls_test_protocol.h"
#include "mbedtls_test.h"
#include "ztest_glue.h"
#include "pointops_vectors.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <mbedtls/aes.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/rsa.h>
#include <mbedtls/memory_buffer_alloc.h>   /* P1a：堆水位/泄漏监控（见 heap_stress_keygen） */

extern const unsigned char rsa_msg[];
extern int rsa_import_test_key(mbedtls_rsa_context *rsa);

#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN)
#include <otbn_pointops.h>
#define MT_HAVE_POINTOPS_ROBUST 1
#endif

#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
#include <mbedtls/sm4_alt.h>
#endif

ZTEST_SUITE(robustness, NULL, NULL, NULL, NULL, NULL);

/* ---- SHA-256 异常参数 ---- */
ZTEST(robustness, sha256_bad_params)
{
	mbedtls_sha256_context ctx;
	unsigned char md[32];
	int ret;

	/* An is224 flag that is neither 0 nor 1 is the documented bad-input case:
	 * mbedtls_sha256_starts() is specified to return
	 * MBEDTLS_ERR_SHA256_BAD_INPUT_DATA for it. */
	mbedtls_sha256_init(&ctx);
	ret = mbedtls_sha256_starts(&ctx, 2);
	zassert_not_equal(ret, 0,
			  "sha256_starts(is224=2) should fail (MBEDTLS_ERR_SHA256_BAD_INPUT_DATA)");

	/* len=0 合法（空 update） */
	ret = mbedtls_sha256_starts(&ctx, 0);
	zassert_equal(ret, 0, "sha256_starts init ret=%d", ret);
	ret = mbedtls_sha256_update(&ctx, NULL, 0);
	zassert_equal(ret, 0, "sha256_update len=0 ret=%d", ret);

	/* finish 前 reset 再 update */
	ret = mbedtls_sha256_starts(&ctx, 0);
	zassert_equal(ret, 0, "sha256 re-starts ret=%d", ret);
	ret = mbedtls_sha256_update(&ctx, (const unsigned char *)"abc", 3);
	zassert_equal(ret, 0, "sha256 update abc ret=%d", ret);
	ret = mbedtls_sha256_finish(&ctx, md);
	zassert_equal(ret, 0, "sha256 finish ret=%d", ret);

	mbedtls_sha256_free(&ctx);

	/* NOTE (2026-09-29): NULL-ctx / NULL-input assertions were removed from
	 * this shared case. mbedtls documents ctx as "must not be NULL"
	 * (include/mbedtls/sha256.h) and only whitelists NULL for the few
	 * functions that say so (e.g. free); upstream mbedtls_sha256_starts()
	 * therefore dereferences it unchecked, so the *software baseline* -- this
	 * suite's reference -- crashes rather than returning an error. The
	 * LinkedSemi ALT does reject NULL; that extra guarantee is covered by
	 * `sha_alt_null_params` in hash_test.c, compiled only when the ALT is on. */
}

/* ---- SHA-384 异常参数 ---- */
ZTEST(robustness, sha384_bad_params)
{
	mbedtls_sha512_context ctx;
	int ret;

	/* An is384 flag that is neither 0 nor 1 is the documented bad-input case
	 * for mbedtls_sha512_starts(). */
	mbedtls_sha512_init(&ctx);
	ret = mbedtls_sha512_starts(&ctx, 2);
	zassert_not_equal(ret, 0,
			  "sha384_starts(is384=2) should fail (MBEDTLS_ERR_SHA512_BAD_INPUT_DATA)");

	ret = mbedtls_sha512_starts(&ctx, 1);
	zassert_equal(ret, 0, "sha384_starts init ret=%d", ret);
	ret = mbedtls_sha512_update(&ctx, (const unsigned char *)"abc", 3);
	zassert_equal(ret, 0, "sha384 update abc ret=%d", ret);

	mbedtls_sha512_free(&ctx);

	/* NOTE (2026-09-29): the NULL-ctx / NULL-input assertions that used to be
	 * here were removed for the same reason as in sha256_bad_params -- mbedtls
	 * documents ctx as "must not be NULL", so the software baseline faults
	 * instead of returning an error. See sha_alt_null_params (hash_test.c). */
}

/* ---- AES 异常参数 ---- */
ZTEST(robustness, aes_bad_params)
{
	mbedtls_aes_context ctx;
	unsigned char key[16];
	int ret;

	memset(key, 0xab, sizeof(key));

	/* Wrong key length is the ONLY bad-input case mbedtls actually promises:
	 * mbedtls_aes_setkey_enc() is specified to return
	 * MBEDTLS_ERR_AES_INVALID_KEY_LENGTH for anything other than 128/192/256
	 * (include/mbedtls/aes.h). */
	mbedtls_aes_init(&ctx);
	ret = mbedtls_aes_setkey_enc(&ctx, key, 64);
	zassert_not_equal(ret, 0,
			  "aes setkey bad bits should fail (MBEDTLS_ERR_AES_INVALID_KEY_LENGTH)");

	/* A valid key length must still succeed. */
	ret = mbedtls_aes_setkey_enc(&ctx, key, 128);
	zassert_equal(ret, 0, "aes setkey ret=%d", ret);

	mbedtls_aes_free(&ctx);

	/* NOTE (2026-09-29): the previous version of this case also asserted
	 * NULL-pointer robustness here (NULL ctx to setkey_enc, NULL input to
	 * crypt_cbc). Those assertions were moved out, because they demand
	 * behaviour mbedtls does not specify: upstream mbedtls_aes_setkey_enc()
	 * dereferences ctx and key with no NULL check (library/aes.c), so the
	 * software baseline -- the reference this suite compares against --
	 * faults instead of returning an error. Asserting it here made the
	 * software case fail on a requirement it can never meet.
	 * The LinkedSemi ALT *does* guarantee NULL-safety; that stronger,
	 * ALT-specific guarantee is covered by `aes_alt_null_params` in
	 * cipher_test.c (compiled only when the ALT is enabled). */
}

/* ---- SM4 异常参数 ---- */
#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
ZTEST(robustness, sm4_bad_params)
{
	mbedtls_sm4_context ctx;
	unsigned char key[16] __attribute__((aligned(4)));
	unsigned char iv[16] __attribute__((aligned(4)));
	unsigned char pt[16] __attribute__((aligned(4)));
	unsigned char ct[16] __attribute__((aligned(4)));
	int ret;

	memset(key, 0xab, sizeof(key));
	memset(iv, 0, sizeof(iv));
	memset(pt, 0, sizeof(pt));

	mbedtls_sm4_init(&ctx);
	ret = mbedtls_sm4_setkey(key);
	zassert_equal(ret, 0, "sm4 setkey ret=%d", ret);
	ret = mbedtls_sm4_setiv(&ctx, iv);
	zassert_equal(ret, 0, "sm4 setiv ret=%d", ret);

	ret = mbedtls_sm4_ctr_crypto(&ctx, ct, NULL, 16);
	zassert_not_equal(ret, 0, "sm4 ctr NULL input should fail");

	/* ECB 原语走内联寄存器操作（不经 HAL），须自行拒绝非法入参 */
	ret = mbedtls_sm4_ecb_encrypt(NULL, ct, pt, 16);
	zassert_not_equal(ret, 0, "sm4 ecb NULL ctx should fail");
	ret = mbedtls_sm4_ecb_encrypt(&ctx, ct, NULL, 16);
	zassert_not_equal(ret, 0, "sm4 ecb NULL input should fail");
	ret = mbedtls_sm4_ecb_encrypt(&ctx, NULL, pt, 16);
	zassert_not_equal(ret, 0, "sm4 ecb NULL output should fail");
	ret = mbedtls_sm4_ecb_decrypt(&ctx, ct, NULL, 16);
	zassert_not_equal(ret, 0, "sm4 ecb dec NULL input should fail");
	ret = mbedtls_sm4_ecb_decrypt(&ctx, NULL, pt, 16);
	zassert_not_equal(ret, 0, "sm4 ecb dec NULL output should fail");

	/* 长度 0 与非 16 整块：进硬件前即须拒绝（CALC_LEN 下溢） */
	ret = mbedtls_sm4_ecb_encrypt(&ctx, ct, pt, 0);
	zassert_not_equal(ret, 0, "sm4 ecb len=0 should fail");
	ret = mbedtls_sm4_ecb_encrypt(&ctx, ct, pt, 20);
	zassert_not_equal(ret, 0, "sm4 ecb non-block len should fail");

	/* 合法入参仍须成功——确认校验未误伤正常路径 */
	ret = mbedtls_sm4_ecb_encrypt(&ctx, ct, pt, 16);
	zassert_equal(ret, 0, "sm4 ecb valid ret=%d", ret);
	ret = mbedtls_sm4_ecb_decrypt(&ctx, pt, ct, 16);
	zassert_equal(ret, 0, "sm4 ecb dec valid ret=%d", ret);

	mbedtls_sm4_free(&ctx);
}
#endif

/* ---- ECDSA 异常/负例 ---- */
ZTEST(robustness, ecdsa_tamper_verify)
{
	mbedtls_ecp_group grp;
	mbedtls_ecp_point Q;
	mbedtls_mpi d, r, s;
	unsigned char hash[32];
	int ret;

	mbedtls_ecp_group_init(&grp);
	mbedtls_ecp_point_init(&Q);
	mbedtls_mpi_init(&d);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
	if (ret == 0) {
		ret = mbedtls_ecp_gen_keypair(&grp, &d, &Q, mt_rng_trng, NULL);
	}
	memset(hash, 0x55, sizeof(hash));
	if (ret == 0) {
		ret = mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, sizeof(hash),
				       mt_rng_fixed, NULL);
	}
	if (ret == 0) {
		ret = mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &Q, &r, &s);
		zassert_equal(ret, 0, "ecdsa verify good ret=%d", ret);
	}

	/* 改消息后 verify 应失败 */
	if (ret == 0) {
		hash[0] ^= 0x01;
		ret = mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &Q, &r, &s);
		zassert_not_equal(ret, 0, "ecdsa verify tampered should fail");
		hash[0] ^= 0x01;
	}

	mbedtls_ecp_group_free(&grp);
	mbedtls_ecp_point_free(&Q);
	mbedtls_mpi_free(&d);
	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);

	/* NOTE (2026-09-29): a "verify with NULL group must fail" assertion used to
	 * be here. It was removed because mbedtls documents grp as an initialised
	 * group -- NULL is a caller contract violation, not an error path -- and
	 * upstream mbedtls_ecdsa_verify_restartable() dereferences it unchecked
	 * (library/ecdsa.c), so the software baseline faults instead of returning
	 * an error. The LinkedSemi OTBN ECDSA ALT does reject NULL; that extra
	 * guarantee belongs with the ALT-gated hardening coverage, not here. */
}

/* ---- RSA 异常/负例 ---- */
ZTEST(robustness, rsa_key_mismatch)
{
	mbedtls_rsa_context rsa_pub;
	unsigned char n[256];
	int ret;

	mbedtls_rsa_init(&rsa_pub);

	/* 构造只含公钥 (n,e) 的上下文 */
	memcpy(n, rsa2048_n, sizeof(rsa2048_n));
	ret = mbedtls_rsa_import_raw(&rsa_pub,
				     n, sizeof(n),
				     NULL, 0,
				     NULL, 0,
				     NULL, 0,
				     rsa2048_e, sizeof(rsa2048_e));
	if (ret == 0) {
		ret = mbedtls_rsa_complete(&rsa_pub);
	}
	zassert_equal(ret, 0, "rsa import pub ret=%d", ret);

	/* 公钥上下文缺少 d/p/q 等私钥分量，check_privkey 应失败 */
	ret = mbedtls_rsa_check_privkey(&rsa_pub);
	zassert_not_equal(ret, 0, "rsa check_privkey pub-only should fail");

	mbedtls_rsa_free(&rsa_pub);
}

/* ---- RSA keygen 非 F4 回退软件 ---- */
ZTEST(robustness, rsa_keygen_non_f4_fallback)
{
	mbedtls_rsa_context rsa;
	int ret;

	mbedtls_rsa_init(&rsa);
	/* public exponent = 3，应仍成功（可能回退软件路径） */
	ret = mbedtls_rsa_gen_key(&rsa, mt_rng_trng, NULL, 2048, 3);
	zassert_equal(ret, 0, "rsa genkey e=3 ret=%d", ret);

	if (ret == 0) {
		ret = mbedtls_rsa_check_privkey(&rsa);
		zassert_equal(ret, 0, "rsa check e=3 ret=%d", ret);
	}

	mbedtls_rsa_free(&rsa);
}

/* 双线程共享同一 SHA-256 ctx（THREADING_ALT 互斥生效）。
 * 仅在纯软件 SHA-256 路径下执行：HW/OTBN alt 驱动层在跨线程复用同一 ctx
 * 时会死锁（doneLock 快照/引擎状态与线程切换互锁，实测 case_hw_otbn_mix
 * 子线程 update 挂死），属驱动限制，非本测试目标；HW/OTBN 路径的线程安全
 * 由 concurrency_test.c 的独立上下文并发 suite 覆盖。
 */
#if defined(CONFIG_MBEDTLS_THREADING_LINKEDSEMI_ALT) && \
	!defined(CONFIG_MBEDTLS_SHA224_SHA256_LINKEDSEMI_HARDWARE_ALT) && \
	!defined(CONFIG_MBEDTLS_SHA224_SHA256_LINKEDSEMI_OTBN_ALT)
static K_THREAD_STACK_DEFINE(rb_sha_stack, 24576);
static struct k_thread rb_sha_thread;
static volatile int rb_sha_ret;
static mbedtls_sha256_context rb_sha_ctx;
static unsigned char rb_sha_md[32];

static void rb_sha_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	int ret = 0;
	for (int i = 0; i < 10 && ret == 0; i++) {
		ret = mbedtls_sha256_update(&rb_sha_ctx,
					    (const unsigned char *)"thread", 6);
	}
	rb_sha_ret = ret;
}

ZTEST(robustness, shared_sha256_ctx_thread)
{
	int ret;

	mbedtls_sha256_init(&rb_sha_ctx);
	ret = mbedtls_sha256_starts(&rb_sha_ctx, 0);
	zassert_equal(ret, 0, "shared sha starts ret=%d", ret);

	rb_sha_ret = -1;
	k_thread_create(&rb_sha_thread, rb_sha_stack,
			K_THREAD_STACK_SIZEOF(rb_sha_stack),
			rb_sha_thread_fn, NULL, NULL, NULL,
			K_PRIO_PREEMPT(-1), 0, K_NO_WAIT);

	/* 子线程优先级更高：先 yield 让子线程完成全部 update */
	k_yield();

	/* 主线程同步更新同一 ctx */
	for (int i = 0; i < 10 && ret == 0; i++) {
		ret = mbedtls_sha256_update(&rb_sha_ctx,
					    (const unsigned char *)"main", 4);
	}

	/* 10s 超时兜底：若驱动层锁嵌套死锁，不让整个 suite 挂死 */
	ret = k_thread_join(&rb_sha_thread, K_MSEC(10000));
	zassert_equal(ret, 0, "shared sha ctx thread join timed out (deadlock?)");

	/* 最后由主线程 finish（避免子线程在 finish 中触发引擎时与主线程互锁） */
	if (ret == 0) {
		ret = mbedtls_sha256_finish(&rb_sha_ctx, rb_sha_md);
	}

	/* 不崩溃即视为 THREADING_ALT 互斥生效；结果不具确定性 */
	zassert_true(ret == 0 || rb_sha_ret == 0,
		     "shared sha ctx crash/ret=%d/%d", ret, rb_sha_ret);
	mbedtls_sha256_free(&rb_sha_ctx);
}
#endif

/* ---- otbn_pointops 鲁棒性 ---- */
#ifdef MT_HAVE_POINTOPS_ROBUST

/* 非法 curve 值 */
#define MT_BAD_CURVE ((enum ls_otbn_pointops_curve)0xFF)

ZTEST(robustness, pointops_field_bytes_bad)
{
	size_t b;
	int ret;

	ret = ls_otbn_pointops_field_bytes(MT_BAD_CURVE, &b);
	zassert_not_equal(ret, 0, "field_bytes bad curve should fail");

	ret = ls_otbn_pointops_field_bytes(LS_OTBN_POINTOPS_CURVE_P256, NULL);
	zassert_not_equal(ret, 0, "field_bytes NULL bytes should fail");
}

ZTEST(robustness, pointops_scalar_mult_bad)
{
	uint8_t rx[32], ry[32];
	int ret;

	ret = ls_otbn_pointops_scalar_mult(LS_OTBN_POINTOPS_CURVE_P256,
					   NULL, p256_px, p256_py,
					   rx, ry);
	zassert_not_equal(ret, 0, "scalar_mult NULL scalar should fail");

	ret = ls_otbn_pointops_scalar_mult(MT_BAD_CURVE,
					   p256_k, p256_px, p256_py,
					   rx, ry);
	zassert_not_equal(ret, 0, "scalar_mult bad curve should fail");
}

ZTEST(robustness, pointops_base_mult_bad)
{
	uint8_t rx[32], ry[32];
	int ret;

	ret = ls_otbn_pointops_base_mult(LS_OTBN_POINTOPS_CURVE_P256, NULL,
					 rx, ry);
	zassert_not_equal(ret, 0, "base_mult NULL scalar should fail");

	ret = ls_otbn_pointops_base_mult(MT_BAD_CURVE, p256_k, rx, ry);
	zassert_not_equal(ret, 0, "base_mult bad curve should fail");
}

ZTEST(robustness, pointops_point_add_bad)
{
	uint8_t rx[32], ry[32];
	int ret;

	ret = ls_otbn_pointops_point_add(LS_OTBN_POINTOPS_CURVE_P256,
					 p256_gx, p256_gy, NULL, p256_py,
					 rx, ry);
	zassert_not_equal(ret, 0, "point_add NULL qx should fail");

	ret = ls_otbn_pointops_point_add(MT_BAD_CURVE,
					 p256_gx, p256_gy,
					 p256_px, p256_py,
					 rx, ry);
	zassert_not_equal(ret, 0, "point_add bad curve should fail");
}

/* 【已删除 2026-09-18】原 `pointops_scalar_edge`（scalar 全 0 / 全 0xFF）：
 * 其唯一断言是 `zassert_true(ret == 0 || ret != 0, ...)` —— 对任意 ret 恒真，
 * **永不可能 FAIL**（测试有效性审计 A 组 P0-2）。删除而非改写，理由：
 *   - `ls_otbn_pointops_base_mult()`（otbn_pointops.c:895-921）对 scalar=0
 *     不做校验，直接送 OTBN 固件执行；"k=0 ⇒ 无穷远点"的**编码形式**
 *     （全 0 / 带 flag / 固件直接报错）无驱动或固件契约可依，无法独立算出黄金值；
 *   - 按审计纪律"支撑不了就删，不要编一个假断言"，本项不保留替代断言。
 * 覆盖影响与依据见 tests/COVERAGE.md「pointops_scalar_edge」条目。 */

#endif /* MT_HAVE_POINTOPS_ROBUST */

/* ==================================================================
 * P1a 新增（2026-09-17）：MBED-04 —— 重复密钥生成的堆水位 / 泄漏监控
 *
 * 依赖（本工程 prj.conf 已加）：`CONFIG_MBEDTLS_ENABLE_HEAP=y` +
 * `CONFIG_MBEDTLS_MEMORY_DEBUG=y`。
 *   - 堆本体 = `CONFIG_MBEDTLS_HEAP_SIZE`(65536) 的静态 buffer，
 *     由 zephyr/modules/mbedtls/zephyr_init.c 在
 *     `MBEDTLS_ENABLE_HEAP && MBEDTLS_MEMORY_BUFFER_ALLOC_C` 下调
 *     `mbedtls_memory_buffer_alloc_init()` 建立。
 *   - `cur_get` / `max_get` / `verify` 的实现与计数字段
 *     （`total_used` / `header_count` / `maximum_used`）都在库源码
 *     `memory_buffer_alloc.c` 的 `MBEDTLS_MEMORY_DEBUG` 之下，
 *     故必须先开 `CONFIG_MBEDTLS_MEMORY_DEBUG`（它只加计数、不改分配行为）。
 *
 * 判据：完整跑「group_load → N 次 P-256 keygen（每次立即释放）→ group_free」
 * 之后，堆占用必须**回落到进入测试时的基线**（无净增长 = 无泄漏）；
 * 且逐轮占用不得单调上涨。
 * ================================================================== */
#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C) && defined(MBEDTLS_MEMORY_DEBUG)

ZTEST(robustness, heap_stress_keygen)
{
	const int loops = 16;
	mbedtls_ecp_group grp;
	size_t pre_used = 0;
	size_t pre_blocks = 0;
	size_t base_used = 0;
	size_t base_blocks = 0;
	size_t post_used = 0;
	size_t post_blocks = 0;
	size_t first_after = 0;
	size_t max_after = 0;
	size_t peak_used = 0;
	size_t peak_blocks = 0;
	int ret;
	int i;

	zassert_equal(mbedtls_memory_buffer_alloc_verify(), 0,
		      "heap header chain broken before keygen stress");

	/* 进入测试时的堆基线 */
	mbedtls_memory_buffer_alloc_cur_get(&pre_used, &pre_blocks);

	mbedtls_ecp_group_init(&grp);
	ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
	zassert_equal(ret, 0, "ecp_group_load ret %d", ret);

	/* group_load 之后的常驻占用（grp 自身持有的 P/A/B/G） */
	mbedtls_memory_buffer_alloc_cur_get(&base_used, &base_blocks);

	for (i = 0; i < loops; i++) {
		mbedtls_mpi d;
		mbedtls_ecp_point Q;
		size_t cur_used = 0;
		size_t cur_blocks = 0;

		mbedtls_mpi_init(&d);
		mbedtls_ecp_point_init(&Q);

		/* TRNG 真随机：私钥/候选生成不能用确定性 LCG */
		ret = mbedtls_ecp_gen_keypair(&grp, &d, &Q, mt_rng_trng, NULL);
		zassert_equal(ret, 0, "ecp_gen_keypair #%d ret %d", i, ret);

		mbedtls_mpi_free(&d);
		mbedtls_ecp_point_free(&Q);

		mbedtls_memory_buffer_alloc_cur_get(&cur_used, &cur_blocks);
		if (i == 0) {
			first_after = cur_used;
		}
		if (cur_used > max_after) {
			max_after = cur_used;
		}
	}

	mbedtls_memory_buffer_alloc_max_get(&peak_used, &peak_blocks);
	mbedtls_ecp_group_free(&grp);
	mbedtls_memory_buffer_alloc_cur_get(&post_used, &post_blocks);

	zassert_equal(mbedtls_memory_buffer_alloc_verify(), 0,
		      "heap header chain broken after keygen stress");

	ZT_INFO("heap_stress_keygen",
		"loops=%d,pre_used=%u,base_used=%u,first_after=%u,max_after=%u,"
		"post_used=%u,peak_used=%u,peak_blocks=%u,heap=%u",
		loops, (unsigned)pre_used, (unsigned)base_used,
		(unsigned)first_after, (unsigned)max_after, (unsigned)post_used,
		(unsigned)peak_used, (unsigned)peak_blocks,
		(unsigned)CONFIG_MBEDTLS_HEAP_SIZE);

	/* 判据 1：逐轮占用不得上涨（第 1 轮后允许一次性常驻，之后必须持平） */
	zassert_true(max_after <= first_after,
		     "heap grew across keygen rounds: max_after=%u > first_after=%u",
		     (unsigned)max_after, (unsigned)first_after);

	/* 判据 2（核心泄漏判据）：整个 group_load+keygen+group_free 周期结束后，
	 * 堆占用必须回落到进入测试时的基线，不得有净增长 */
	zassert_true(post_used <= pre_used,
		     "heap leak: post_used=%u > pre_used=%u (net growth %d bytes)",
		     (unsigned)post_used, (unsigned)pre_used,
		     (int)post_used - (int)pre_used);

	/* 判据 3：峰值必须留在堆容量之内（未触顶，测试本身有效） */
	zassert_true(peak_used < (size_t)CONFIG_MBEDTLS_HEAP_SIZE,
		     "heap peak %u reached capacity %u",
		     (unsigned)peak_used, (unsigned)CONFIG_MBEDTLS_HEAP_SIZE);
}

#endif /* MBEDTLS_MEMORY_BUFFER_ALLOC_C && MBEDTLS_MEMORY_DEBUG */
