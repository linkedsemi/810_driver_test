/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN RSA 密钥生成测试套件。
 *
 * 被测：zephyr/soc/linkedsemi/lsqsh/otbn/otbn_rsa_keygen.c
 *   otbn_rsa_keygen() / otbn_rsa_check_prime() / otbn_rsa_key_from_pq() /
 *   otbn_rsa_gen_candidate() / otbn_rsa_modulus_bytes() / otbn_rsa_prime_bytes()
 * 并覆盖重构后的 mbedtls keygen hook：
 *   modules/crypto/mbedtls/library/rsa_alt.c::mbedtls_rsa_gen_key_otbn()，
 *   经 mbedtls_rsa_gen_key()（CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT=y）。
 *
 * 判据独立性：密钥正确性一律由 CPU 侧 mbedtls bignum 独立复算
 * （n==p*q、p/q 素性用 CPU Miller-Rabin、e*d≡1 mod lcm），不依赖被测模块的
 * 返回值；"OTBN 真的被用到"由 ls_otbn_imem_firmware_get()==KEYGEN 断言（非软件回落）。
 *
 * 两条硬约束（被测模块 API）：
 *   ① 所有输入/输出缓冲必须 4 字节对齐（否则 -EINVAL）—— 由 aligned_* 用例覆盖；
 *   ② 候选值被强制 ≡ 3 (mod 4)（gen_candidate 内部 |= 0x03）⇒ p、q ≡ 3 (mod 4)。
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "ztest_glue.h"

#include "otbn_rsa_keygen.h"
#include "ls_otbn_config.h"   /* ls_otbn_imem_firmware_get / OTBN_FIRMWARE_RSA_KEYGEN */

#include <mbedtls/bignum.h>
#include <mbedtls/rsa.h>

#include "ref_vector.h"       /* 客户 TPM 参考向量 KAT（tests/expected/ref_vector.h） */

/* 最大缓冲（4096 位：模数 512B / 素数 256B），文件级 static：不占 ztest 线程栈。 */
#define MAX_MOD  OTBN_RSA_KEYGEN_MODULUS_BYTES_MAX   /* 512 */
#define MAX_PRI  OTBN_RSA_KEYGEN_PRIME_BYTES_MAX     /* 256 */

static uint8_t g_p[MAX_PRI]  __aligned(4);
static uint8_t g_q[MAX_PRI]  __aligned(4);
static uint8_t g_n[MAX_MOD]  __aligned(4);
static uint8_t g_d[MAX_MOD]  __aligned(4);
static uint8_t g_n2[MAX_MOD] __aligned(4);
static uint8_t g_d2[MAX_MOD] __aligned(4);

ZTEST_SUITE(keygen, NULL, NULL, NULL, NULL, NULL);

/* ============================ 确定性 RNG ============================
 * 固定 seed 的 splitmix64 流：同一个 seed 复位后逐字节复现，用来验证
 * "固定 RNG 流 ⇒ 逐位相同的密钥对"（本模块存在的理由）。 */
typedef struct {
	uint64_t state;
} det_rng_t;

static void det_rng_reset(det_rng_t *r, uint64_t seed)
{
	r->state = seed;
}

static uint64_t det_rng_u64(det_rng_t *r)
{
	uint64_t z = (r->state += 0x9E3779B97F4A7C15ULL);

	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

static int det_rng(void *ctx, uint8_t *buf, size_t len)
{
	det_rng_t *r = (det_rng_t *)ctx;
	uint64_t cur = 0;

	for (size_t i = 0; i < len; i++) {
		if ((i & 7) == 0) {
			cur = det_rng_u64(r);
		}
		buf[i] = (uint8_t)(cur >> ((i & 7) * 8));
	}
	return 0;
}

/* 供 CPU 侧 Miller-Rabin 用的 RNG（内容不要求确定，只要合法非 NULL）。 */
static det_rng_t g_prime_rng;

/* ============================ 错误路径 RNG ============================ */
static int err_rng_eio(void *ctx, uint8_t *buf, size_t len)
{
	(void)ctx;
	(void)buf;
	(void)len;
	return -EIO;
}

static int err_rng_eperm(void *ctx, uint8_t *buf, size_t len)
{
	(void)ctx;
	(void)buf;
	(void)len;
	return -EPERM;
}

/* ============================ 请求构造 ============================ */
static void req_init(otbn_rsa_keygen_req_t *req, otbn_rsa_keygen_size_t size,
		     otbn_rsa_rng_cb rng, void *ctx)
{
	req->size = size;
	req->rng = rng;
	req->rng_ctx = ctx;
	req->p_le = g_p;
	req->q_le = g_q;
	req->n_le = g_n;
	req->d_le = g_d;
}

/* CPU 侧独立复算：n==p*q、p/q 素性（Miller-Rabin）、e*d≡1 mod lcm。
 * 期望来源：RSA 数学定义（③级推断），但复算完全独立于被测模块。 */
static void check_key_consistency(otbn_rsa_keygen_size_t size)
{
	uint32_t pb = otbn_rsa_prime_bytes(size);
	uint32_t mb = otbn_rsa_modulus_bytes(size);
	mbedtls_mpi P, Q, N, D, E, T, P1, Q1, G, L, T2;
	int rc;

	/* p、q ≡ 3 (mod 4)：LE 低字节低 2 位（gen_candidate 的 |= 0x03 约束） */
	zassert_equal(g_p[0] & 0x03, 0x03, "p %% 4 != 3 (low byte 0x%02x)", g_p[0]);
	zassert_equal(g_q[0] & 0x03, 0x03, "q %% 4 != 3 (low byte 0x%02x)", g_q[0]);

	mbedtls_mpi_init(&P);
	mbedtls_mpi_init(&Q);
	mbedtls_mpi_init(&N);
	mbedtls_mpi_init(&D);
	mbedtls_mpi_init(&E);
	mbedtls_mpi_init(&T);
	mbedtls_mpi_init(&P1);
	mbedtls_mpi_init(&Q1);
	mbedtls_mpi_init(&G);
	mbedtls_mpi_init(&L);
	mbedtls_mpi_init(&T2);

	rc = mbedtls_mpi_read_binary_le(&P, g_p, pb);
	rc |= mbedtls_mpi_read_binary_le(&Q, g_q, pb);
	rc |= mbedtls_mpi_read_binary_le(&N, g_n, mb);
	rc |= mbedtls_mpi_read_binary_le(&D, g_d, mb);
	zassert_equal(rc, 0, "mpi read failed rc=%d", rc);

	/* n == p*q */
	rc = mbedtls_mpi_mul_mpi(&T, &P, &Q);
	zassert_equal(rc, 0, "mul rc=%d", rc);
	zassert_equal(mbedtls_mpi_cmp_mpi(&T, &N), 0, "n != p*q");

	/* 素性：CPU 侧 Miller-Rabin（16 轮 ⇒ 误判概率 ≤ 2^-32） */
	det_rng_reset(&g_prime_rng, 0x0102030405060708ULL);
	zassert_equal(mbedtls_mpi_is_prime_ext(&P, 16, det_rng, &g_prime_rng), 0,
		      "p 被判为合数（CPU Miller-Rabin）");
	zassert_equal(mbedtls_mpi_is_prime_ext(&Q, 16, det_rng, &g_prime_rng), 0,
		      "q 被判为合数（CPU Miller-Rabin）");

	/* e*d ≡ 1 (mod lcm(p-1, q-1)) */
	rc = mbedtls_mpi_lset(&E, OTBN_RSA_KEYGEN_EXPONENT);
	rc |= mbedtls_mpi_sub_int(&P1, &P, 1);
	rc |= mbedtls_mpi_sub_int(&Q1, &Q, 1);
	rc |= mbedtls_mpi_gcd(&G, &P1, &Q1);
	rc |= mbedtls_mpi_mul_mpi(&L, &P1, &Q1);
	zassert_equal(rc, 0, "lcm prep rc=%d", rc);
	rc = mbedtls_mpi_div_mpi(&T2, NULL, &L, &G);       /* T2 = lcm */
	zassert_equal(rc, 0, "lcm div rc=%d", rc);
	rc = mbedtls_mpi_mul_mpi(&T, &E, &D);
	rc |= mbedtls_mpi_mod_mpi(&T, &T, &T2);
	zassert_equal(rc, 0, "e*d mod rc=%d", rc);
	zassert_equal(mbedtls_mpi_cmp_int(&T, 1), 0, "e*d mod lcm != 1");

	mbedtls_mpi_free(&P);
	mbedtls_mpi_free(&Q);
	mbedtls_mpi_free(&N);
	mbedtls_mpi_free(&D);
	mbedtls_mpi_free(&E);
	mbedtls_mpi_free(&T);
	mbedtls_mpi_free(&P1);
	mbedtls_mpi_free(&Q1);
	mbedtls_mpi_free(&G);
	mbedtls_mpi_free(&L);
	mbedtls_mpi_free(&T2);
}

/* 便捷：跑一次 keygen（det RNG，固定 seed 由调用方复位）并断言成功。 */
static int keygen_2048(det_rng_t *rng, uint64_t seed)
{
	otbn_rsa_keygen_req_t req;

	det_rng_reset(rng, seed);
	req_init(&req, OTBN_RSA_KEYGEN_2048, det_rng, rng);
	return otbn_rsa_keygen(&req);
}

/* ================================ 用例 ================================ */

/* 尺寸换算 + 候选生成约束（3 mod 4 / 最高两位置位）+ 生成器自身的确定性。 */
ZTEST(keygen, candidate_residue_and_topbits)
{
	det_rng_t rng;
	uint32_t pb = otbn_rsa_prime_bytes(OTBN_RSA_KEYGEN_2048);
	int ret;

	zassert_equal(otbn_rsa_modulus_bytes(OTBN_RSA_KEYGEN_2048), 256, "2048 mod bytes");
	zassert_equal(otbn_rsa_modulus_bytes(OTBN_RSA_KEYGEN_3072), 384, "3072 mod bytes");
	zassert_equal(otbn_rsa_modulus_bytes(OTBN_RSA_KEYGEN_4096), 512, "4096 mod bytes");
	zassert_equal(otbn_rsa_prime_bytes(OTBN_RSA_KEYGEN_2048), 128, "2048 prime bytes");
	zassert_equal(otbn_rsa_prime_bytes(OTBN_RSA_KEYGEN_3072), 192, "3072 prime bytes");
	zassert_equal(otbn_rsa_prime_bytes(OTBN_RSA_KEYGEN_4096), 256, "4096 prime bytes");
	zassert_equal(otbn_rsa_modulus_bytes((otbn_rsa_keygen_size_t)99), 0,
		      "invalid size modulus_bytes != 0");
	zassert_equal(otbn_rsa_prime_bytes((otbn_rsa_keygen_size_t)99), 0,
		      "invalid size prime_bytes != 0");

	det_rng_reset(&rng, 0x1122334455667788ULL);
	ret = otbn_rsa_gen_candidate(OTBN_RSA_KEYGEN_2048, det_rng, &rng, g_p);
	zassert_equal(ret, 0, "gen_candidate ret=%d", ret);

	zassert_equal(g_p[0] & 0x03, 0x03,
		      "candidate 非奇数/非 3 mod 4: low=0x%02x", g_p[0]);
	zassert_equal(g_p[pb - 1] & 0xC0, 0xC0,
		      "candidate 最高两 bit 未置位: 0x%02x", g_p[pb - 1]);

	/* 同 seed 复位 ⇒ 逐字节相同的候选（确定性基础） */
	memcpy(g_n, g_p, pb);
	det_rng_reset(&rng, 0x1122334455667788ULL);
	ret = otbn_rsa_gen_candidate(OTBN_RSA_KEYGEN_2048, det_rng, &rng, g_q);
	zassert_equal(ret, 0, "gen_candidate#2 ret=%d", ret);
	zassert_mem_equal(g_q, g_n, pb, "gen_candidate 非确定性");
}

/* 直调 API：check_prime（真素数 → true / 大合数 → false）与 key_from_pq（复算一致）。
 * 用 1 次 keygen 取到真素数 p、q，再喂给两个 helper。 */
ZTEST(keygen, direct_apis_ok)
{
	det_rng_t rng;
	otbn_rsa_keygen_req_t req;
	static uint8_t comp[MAX_PRI] __aligned(4);
	bool pp = false;
	int ret;

	det_rng_reset(&rng, 0xDEADBEEFCAFEF00DULL);
	req_init(&req, OTBN_RSA_KEYGEN_2048, det_rng, &rng);
	ret = otbn_rsa_keygen(&req);
	zassert_equal(ret, 0, "keygen ret=%d", ret);
	if (ret != 0) {
		return;
	}

	/* 集成证据：IMEM 已切到 KEYGEN 固件（紧跟 keygen 读取；任何后续
	 * hash/ECC/modexp 操作都会改写该共享状态）⇒ 证明未走软件回落。 */
	zassert_equal((unsigned)ls_otbn_imem_firmware_get(),
		      (unsigned)OTBN_FIRMWARE_RSA_KEYGEN,
		      "IMEM firmware=%u != RSA_KEYGEN(%u)",
		      (unsigned)ls_otbn_imem_firmware_get(),
		      (unsigned)OTBN_FIRMWARE_RSA_KEYGEN);

	/* check_prime(生成的素数 p) → probably_prime == true */
	ret = otbn_rsa_check_prime(OTBN_RSA_KEYGEN_2048, g_p, &pp);
	zassert_equal(ret, 0, "check_prime(p) ret=%d", ret);
	zassert_true(pp, "生成的 p 被判合数");

	/* check_prime(全 1 大合数，≡3 mod 4: 2^1024-1 = (2^512-1)(2^512+1)) → false */
	memset(comp, 0xFF, sizeof(comp));
	pp = true;
	ret = otbn_rsa_check_prime(OTBN_RSA_KEYGEN_2048, comp, &pp);
	zassert_equal(ret, 0, "check_prime(2^1024-1) ret=%d", ret);
	zassert_false(pp, "2^1024-1 被判素数");

	/* key_from_pq 复算：与 keygen 输出逐字节一致（纯函数） */
	ret = otbn_rsa_key_from_pq(OTBN_RSA_KEYGEN_2048, g_p, g_q, g_n2, g_d2);
	zassert_equal(ret, 0, "key_from_pq ret=%d", ret);
	zassert_mem_equal(g_n2, g_n, 256, "key_from_pq n 与 keygen 不一致");
	zassert_mem_equal(g_d2, g_d, 256, "key_from_pq d 与 keygen 不一致");
}

/* mbedtls 路径：mbedtls_rsa_gen_key() 走 OTBN hook 成功 + check_privkey 通过。 */
ZTEST(keygen, mbedtls_gen_key_ok)
{
	mbedtls_rsa_context rsa;
	det_rng_t rng;
	int ret;

	det_rng_reset(&rng, 0x0FEDCBA987654321ULL);
	mbedtls_rsa_init(&rsa);
	ret = mbedtls_rsa_gen_key(&rsa, det_rng, &rng, 2048, 65537);
	ZT_INFO("mbedtls_gen_key_ok", "api=mbedtls_rsa_gen_key,keylen=2048,ret=%d", ret);
	zassert_equal(ret, 0, "mbedtls_rsa_gen_key ret=%d", ret);
	if (ret == 0) {
		ret = mbedtls_rsa_check_privkey(&rsa);
		zassert_equal(ret, 0, "mbedtls_rsa_check_privkey ret=%d", ret);
	}
	mbedtls_rsa_free(&rsa);
}

/* 2048：生成成功 + 全量正确性（n=p*q / 素性 / 3 mod 4 / e*d≡1 mod lcm）。 */
ZTEST(keygen, rsa_2048_correctness)
{
	det_rng_t rng;
	int ret = keygen_2048(&rng, 0xA5A5A5A5A5A5A5A5ULL);

	zassert_equal(ret, 0, "2048 keygen ret=%d", ret);
	if (ret != 0) {
		return;
	}
	check_key_consistency(OTBN_RSA_KEYGEN_2048);
}

/* ★ 确定性：同一固定 RNG 流跑两次，p/q/n/d 逐位相同。 */
ZTEST(keygen, rsa_2048_deterministic)
{
	det_rng_t rng;
	otbn_rsa_keygen_req_t req;
	static uint8_t p1[MAX_PRI], q1[MAX_PRI], n1[MAX_MOD], d1[MAX_MOD];
	int ret;

	/* 第 1 次 */
	det_rng_reset(&rng, 0x5A5A5A5A01234567ULL);
	req_init(&req, OTBN_RSA_KEYGEN_2048, det_rng, &rng);
	ret = otbn_rsa_keygen(&req);
	zassert_equal(ret, 0, "keygen#1 ret=%d", ret);
	if (ret != 0) {
		return;
	}
	memcpy(p1, g_p, 128);
	memcpy(q1, g_q, 128);
	memcpy(n1, g_n, 256);
	memcpy(d1, g_d, 256);

	/* 第 2 次：同 seed 复位 ⇒ 必须逐位相同 */
	det_rng_reset(&rng, 0x5A5A5A5A01234567ULL);
	req_init(&req, OTBN_RSA_KEYGEN_2048, det_rng, &rng);
	ret = otbn_rsa_keygen(&req);
	zassert_equal(ret, 0, "keygen#2 ret=%d", ret);
	if (ret != 0) {
		return;
	}
	zassert_mem_equal(g_p, p1, 128, "p 不确定（同 RNG 流两次不同）");
	zassert_mem_equal(g_q, q1, 128, "q 不确定");
	zassert_mem_equal(g_n, n1, 256, "n 不确定");
	zassert_mem_equal(g_d, d1, 256, "d 不确定");
}

/* 3072：生成成功 + n=p*q + 3 mod 4。 */
ZTEST(keygen, rsa_3072_ok)
{
	det_rng_t rng;
	otbn_rsa_keygen_req_t req;
	mbedtls_mpi P, Q, N, T;
	int ret;

	det_rng_reset(&rng, 0x13579BDF2468ACE0ULL);
	req_init(&req, OTBN_RSA_KEYGEN_3072, det_rng, &rng);
	ret = otbn_rsa_keygen(&req);
	zassert_equal(ret, 0, "3072 keygen ret=%d", ret);
	if (ret != 0) {
		return;
	}
	zassert_equal(g_p[0] & 0x03, 0x03, "3072 p %% 4 != 3");
	zassert_equal(g_q[0] & 0x03, 0x03, "3072 q %% 4 != 3");

	mbedtls_mpi_init(&P);
	mbedtls_mpi_init(&Q);
	mbedtls_mpi_init(&N);
	mbedtls_mpi_init(&T);
	mbedtls_mpi_read_binary_le(&P, g_p, 192);
	mbedtls_mpi_read_binary_le(&Q, g_q, 192);
	mbedtls_mpi_read_binary_le(&N, g_n, 384);
	mbedtls_mpi_mul_mpi(&T, &P, &Q);
	zassert_equal(mbedtls_mpi_cmp_mpi(&T, &N), 0, "3072 n != p*q");
	mbedtls_mpi_free(&P);
	mbedtls_mpi_free(&Q);
	mbedtls_mpi_free(&N);
	mbedtls_mpi_free(&T);
}

/* 4096：生成成功 + n=p*q（慢；采集窗口/卡死阈值已按此放宽）。 */
ZTEST(keygen, rsa_4096_ok)
{
	det_rng_t rng;
	otbn_rsa_keygen_req_t req;
	mbedtls_mpi P, Q, N, T;
	int ret;

	det_rng_reset(&rng, 0x2468ACE013579BDFULL);
	req_init(&req, OTBN_RSA_KEYGEN_4096, det_rng, &rng);
	ret = otbn_rsa_keygen(&req);
	zassert_equal(ret, 0, "4096 keygen ret=%d", ret);
	if (ret != 0) {
		return;
	}
	zassert_equal(g_p[0] & 0x03, 0x03, "4096 p %% 4 != 3");
	zassert_equal(g_q[0] & 0x03, 0x03, "4096 q %% 4 != 3");

	mbedtls_mpi_init(&P);
	mbedtls_mpi_init(&Q);
	mbedtls_mpi_init(&N);
	mbedtls_mpi_init(&T);
	mbedtls_mpi_read_binary_le(&P, g_p, 256);
	mbedtls_mpi_read_binary_le(&Q, g_q, 256);
	mbedtls_mpi_read_binary_le(&N, g_n, 512);
	mbedtls_mpi_mul_mpi(&T, &P, &Q);
	zassert_equal(mbedtls_mpi_cmp_mpi(&T, &N), 0, "4096 n != p*q");
	mbedtls_mpi_free(&P);
	mbedtls_mpi_free(&Q);
	mbedtls_mpi_free(&N);
	mbedtls_mpi_free(&T);
}

/* 错误路径：rng 回调返回的错误码必须原样上抛（不吞、不换成别的码）。 */
ZTEST(keygen, rng_error_propagates)
{
	otbn_rsa_keygen_req_t req;
	int ret;

	req_init(&req, OTBN_RSA_KEYGEN_2048, err_rng_eio, NULL);
	ret = otbn_rsa_keygen(&req);
	zassert_equal(ret, -EIO, "rng -EIO 上抛错误: got %d", ret);

	req_init(&req, OTBN_RSA_KEYGEN_2048, err_rng_eperm, NULL);
	ret = otbn_rsa_keygen(&req);
	zassert_equal(ret, -EPERM, "rng -EPERM 上抛错误: got %d", ret);
}

/* session 不泄漏：连续 10 次 2048 全部成功（泄漏则第 2 次即 -EBUSY）。 */
ZTEST(keygen, session_no_leak)
{
	det_rng_t rng;
	otbn_rsa_keygen_req_t req;
	int ret;
	int n_ok = 0;

	for (int i = 0; i < 10; i++) {
		det_rng_reset(&rng, 0x1111000000000000ULL + (uint64_t)i);
		req_init(&req, OTBN_RSA_KEYGEN_2048, det_rng, &rng);
		ret = otbn_rsa_keygen(&req);
		zassert_equal(ret, 0, "第 %d 次 keygen ret=%d（session 泄漏?）", i + 1, ret);
		if (ret != 0) {
			break;
		}
		n_ok++;
	}
	ZT_INFO("session_no_leak", "iterations=%d,ok=%d", 10, n_ok);
	zassert_equal(n_ok, 10, "连续 keygen 成功次数 %d != 10", n_ok);
}

/* 硬约束①：非 4 字节对齐的缓冲一律 -EINVAL。
 * 逐指针各测一次（p/n 由 keygen 校验；check_prime/key_from_pq 各测其一）。 */
ZTEST(keygen, unaligned_rejected)
{
	otbn_rsa_keygen_req_t req;
	det_rng_t rng;
	static uint8_t raw_p[MAX_PRI + 8] __aligned(4);
	static uint8_t raw_n[MAX_MOD + 8] __aligned(4);
	bool pp = false;
	int ret;

	det_rng_reset(&rng, 0x2222222222222222ULL);

	/* keygen: p_le 未对齐 */
	req_init(&req, OTBN_RSA_KEYGEN_2048, det_rng, &rng);
	req.p_le = raw_p + 1;
	ret = otbn_rsa_keygen(&req);
	zassert_equal(ret, -EINVAL, "未对齐 p_le 应 -EINVAL, got %d", ret);

	/* keygen: n_le 未对齐 */
	req_init(&req, OTBN_RSA_KEYGEN_2048, det_rng, &rng);
	req.n_le = raw_n + 1;
	ret = otbn_rsa_keygen(&req);
	zassert_equal(ret, -EINVAL, "未对齐 n_le 应 -EINVAL, got %d", ret);

	/* check_prime: 候选未对齐 */
	ret = otbn_rsa_check_prime(OTBN_RSA_KEYGEN_2048, raw_p + 1, &pp);
	zassert_equal(ret, -EINVAL, "未对齐 check_prime 应 -EINVAL, got %d", ret);

	/* key_from_pq: n_le 输出未对齐 */
	ret = otbn_rsa_key_from_pq(OTBN_RSA_KEYGEN_2048, g_p, g_q, raw_n + 1, g_d);
	zassert_equal(ret, -EINVAL, "未对齐 key_from_pq 应 -EINVAL, got %d", ret);
}

/* 把 256B 缓冲的首 16 字节与末 16 字节拼成一行十六进制（KAT 不匹配时的现场证据）。 */
static void hex_edges(const uint8_t *buf, size_t len, char *out, size_t out_sz)
{
	int n = 0;

	for (size_t i = 0; i < 16 && i < len; i++) {
		n += snprintf(out + n, out_sz - (size_t)n, "%02x", buf[i]);
	}
	n += snprintf(out + n, out_sz - (size_t)n, "..");
	for (size_t i = (len > 16 ? len - 16 : 0); i < len; i++) {
		n += snprintf(out + n, out_sz - (size_t)n, "%02x", buf[i]);
	}
}

/*
 * 客户 TPM（TCG 参考实现，deterministic 路径）产出的 RSA-2048 密钥对 KAT。
 *
 * 为什么这条值钱：它是**外部独立来源**的 N/d，不是由我们自己的代码算出——
 * 直接把 otbn_rsa_key_from_pq() 的输出钉死在客户密钥对上（逐字节）。
 *
 * 关键事实（主机侧已验证）：P ≡ 3 (mod 4)，但 Q ≡ 1 (mod 4)。
 * ⇒ key_from_pq 是纯算术（不判素性），结构上应能逐位产出参考 N、d；
 *    但**整条 keygen 全流程结构上不可能产出这对密钥** —— 候选值流不同是一层，
 *    更硬的一层是 CHECK_PRIME 对 ≡1 (mod 4) 恒判合数。下面的 ③④ 就是把这个
 *    接口约束钉成回归（③ P 判素；④ Q 判合数 —— 既定约束，不是 bug）。
 */
ZTEST(keygen, ref_vector_kat)
{
	static uint8_t kat_p[128] __aligned(4);
	static uint8_t kat_q[128] __aligned(4);
	static uint8_t kat_n[256] __aligned(4);
	static uint8_t kat_d[256] __aligned(4);
	char n_act[80], n_exp[80], d_act[80], d_exp[80];
	bool ok = false;
	int ret;

	/* 参考数组是 static const（对齐仅 1）；模块要求 4 字节对齐 ⇒ 拷进对齐缓冲，
	 * 向量本体保持逐字节不动的原件。 */
	memcpy(kat_p, ref_p_le, sizeof(kat_p));
	memcpy(kat_q, ref_q_le, sizeof(kat_q));

	/* ①② key_from_pq 必须逐位产出参考 N 与 d */
	ret = otbn_rsa_key_from_pq(OTBN_RSA_KEYGEN_2048, kat_p, kat_q, kat_n, kat_d);
	zassert_equal(ret, 0, "ref_vector: key_from_pq ret=%d", ret);
	if (ret == 0) {
		int n_eq = (memcmp(kat_n, ref_n_le, sizeof(kat_n)) == 0);
		int d_eq = (memcmp(kat_d, ref_d_le, sizeof(kat_d)) == 0);

		ZT_INFO("ref_vector_kat", "key_from_pq_ret=0,n_byte_equal=%d,d_byte_equal=%d",
			n_eq, d_eq);

		if (!n_eq) {
			hex_edges(kat_n, sizeof(kat_n), n_act, sizeof(n_act));
			hex_edges(ref_n_le, sizeof(ref_n_le), n_exp, sizeof(n_exp));
			ZT_INFO("ref_vector_kat", "n_act=%s", n_act);
			ZT_INFO("ref_vector_kat", "n_exp=%s", n_exp);
		}
		if (!d_eq) {
			hex_edges(kat_d, sizeof(kat_d), d_act, sizeof(d_act));
			hex_edges(ref_d_le, sizeof(ref_d_le), d_exp, sizeof(d_exp));
			ZT_INFO("ref_vector_kat", "d_act=%s", d_act);
			ZT_INFO("ref_vector_kat", "d_exp=%s", d_exp);
		}

		zassert_mem_equal(kat_n, ref_n_le, sizeof(kat_n),
				  "ref N 不匹配（首末 16B 见 INFO n_act/n_exp）");
		zassert_mem_equal(kat_d, ref_d_le, sizeof(kat_d),
				  "ref d 不匹配（首末 16B 见 INFO d_act/d_exp）");
	}

	/* ③ CHECK_PRIME(P) == true（P 是素数且 ≡ 3 mod 4） */
	ok = false;
	ret = otbn_rsa_check_prime(OTBN_RSA_KEYGEN_2048, kat_p, &ok);
	zassert_equal(ret, 0, "ref check_prime(P) ret=%d", ret);
	zassert_true(ok, "参考 P 应判为素数（P ≡ 3 mod 4）");

	/* ④ CHECK_PRIME(Q) == false。★ 这是 CHECK_PRIME 的既定接口约束（只接受
	 * ≡3 mod 4 的候选），**不是 bug**：Q 本身是素数（主机侧已验证），但
	 * Q ≡ 1 (mod 4) ⇒ 固件按约束恒判合数。本项把该约束钉成回归。 */
	ok = true;
	ret = otbn_rsa_check_prime(OTBN_RSA_KEYGEN_2048, kat_q, &ok);
	zassert_equal(ret, 0, "ref check_prime(Q) ret=%d", ret);
	zassert_false(ok,
		      "CHECK_PRIME 对 ≡1 mod 4 的候选应恒判合数（既定接口约束）");
}
