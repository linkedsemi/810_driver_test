/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * cipher 套件：AES-128-CBC 加解密 + SM4-ECB/CBC 加解密（标准向量）。
 *
 * 判定 = 标准向量逐字节比对 + 解密回环（expected/cipher_vectors.h）。
 * AES 走 mbedtls 通用 cipher API；SM4 走 LINKEDSEMI sm4_alt 硬件接口
 * （仅 CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT=y 的 case 编译：
 * case_hw_otbn_mix）。sm4_alt 只有 ECB/CTR 原语、无 CBC API（共享树现状，
 * 未改动），SM4-CBC 在测试层用 ECB 原语组合（CBC 语义标准链接），并与
 * OpenSSL 生成的标准 CBC 向量比对——ECB 原语 + 链接逻辑一起被 KAT 覆盖。
 */

#include "mbedtls_test_protocol.h"
#include "mbedtls_test.h"
#include "ztest_glue.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <mbedtls/aes.h>
#include <mbedtls/gcm.h>
#include "cipher_vectors.h"
#include "cipher_vectors_extra.h"

#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
#include <mbedtls/sm4_alt.h>
#define MT_SM4_BLOCK 16
#endif

ZTEST_SUITE(cipher, NULL, NULL, NULL, NULL, NULL);

/* ---- AES-128-CBC：NIST SP800-38A F.2.1（4 块，加密向量 + 解密回环） ---- */
ZTEST(cipher, aes128_cbc)
{
	mbedtls_aes_context aes;
	unsigned char key[16], iv0[16], iv[16], pt[64], ct[64], dec[64];
	uint32_t t0, us;
	int ret;

	/* mbedtls_aes_crypt_cbc 原地更新 IV → 加/解密各用一份拷贝，
	 * iv0 保持初始向量原件 */
	ret = mt_unhexify(key, MT_AES128CBC_KEY);
	if (ret == 16) {
		ret = mt_unhexify(iv0, MT_AES128CBC_IV);
	}
	if (ret == 16) {
		ret = mt_unhexify(pt, MT_AES128CBC_PT);
	}
	zassert_equal(ret, 64, "aes vector unhexify ret=%d", ret);
	memcpy(iv, iv0, sizeof(iv));

	t0 = k_cycle_get_32();

	mbedtls_aes_init(&aes);
	ret = mbedtls_aes_setkey_enc(&aes, key, 128);
	if (ret == 0) {
		ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT,
					    sizeof(pt), iv, pt, ct);
	}
	if (ret == 0) {
		uint8_t exp_ct[64];

		zassert_equal(mt_unhexify(exp_ct, MT_AES128CBC_CT), 64,
			      "aes ct hex invalid");
		mt_assert_mem_eq(ct, exp_ct, sizeof(pt), "aes128-cbc enc");
	}

	/* 解密回环：setkey_dec（AES 解密轮密钥路径），IV 用初始向量原件拷贝 */
	if (ret == 0) {
		memcpy(iv, iv0, sizeof(iv));
		ret = mbedtls_aes_setkey_dec(&aes, key, 128);
		if (ret == 0) {
			ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT,
						    sizeof(ct), iv, ct, dec);
		}
		if (ret == 0) {
			mt_assert_mem_eq(dec, pt, sizeof(pt), "aes128-cbc dec");
		}
	}
	mbedtls_aes_free(&aes);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("aes128_cbc", "algo=aes-128-cbc,bytes=%u,us=%u",
		(unsigned int)sizeof(pt), us);
	zassert_equal(ret, 0, "aes128_cbc ret=%d", ret);
}

#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
/* ---- SM4-ECB：draft-ribose-cfrg-sm4-10 A.2.1.1（加密向量 + 解密回环） ----
 * sm4_alt 硬件路径按 uint32_t 访问缓冲 → 缓冲 4 字节对齐。 */
ZTEST(cipher, sm4_ecb)
{
	mbedtls_sm4_context sm4;
	unsigned char key[16] __attribute__((aligned(4)));
	unsigned char pt[32] __attribute__((aligned(4)));
	unsigned char enc[32] __attribute__((aligned(4)));
	unsigned char dec[32] __attribute__((aligned(4)));
	uint32_t t0, us;
	int ret;

	zassert_equal(mt_unhexify(key, MT_SM4_KEY), 16, "sm4 key hex invalid");
	zassert_equal(mt_unhexify(pt, MT_SM4_PT), 32, "sm4 pt hex invalid");

	t0 = k_cycle_get_32();

	mbedtls_sm4_init(&sm4);
	ret = mbedtls_sm4_setkey(key);
	if (ret == 0) {
		ret = mbedtls_sm4_ecb_encrypt(&sm4, enc, pt, sizeof(pt));
	}
	if (ret == 0) {
		uint8_t exp_ct[32];

		zassert_equal(mt_unhexify(exp_ct, MT_SM4_ECB_CT), 32,
			      "sm4 ecb ct hex invalid");
		mt_assert_mem_eq(enc, exp_ct, sizeof(pt), "sm4-ecb enc");
	}
	if (ret == 0) {
		ret = mbedtls_sm4_ecb_decrypt(&sm4, dec, enc, sizeof(enc));
		if (ret == 0) {
			mt_assert_mem_eq(dec, pt, sizeof(pt), "sm4-ecb dec");
		}
	}
	mbedtls_sm4_free(&sm4);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("sm4_ecb", "algo=sm4-ecb,bytes=%u,us=%u",
		(unsigned int)sizeof(pt), us);
	zassert_equal(ret, 0, "sm4_ecb ret=%d", ret);
}

/* ---- SM4-CBC：CBC 语义用 sm4_alt 的 ECB 原语在测试层组合（见文件头），
 * 与 OpenSSL 生成的标准 CBC 向量比对 + 解密回环。 ---- */
static int sm4_cbc_crypt(mbedtls_sm4_context *sm4, unsigned char *out,
			 const unsigned char *in, size_t len,
			 const unsigned char *iv, bool encrypt)
{
	unsigned char chain[MT_SM4_BLOCK] __attribute__((aligned(4)));
	int ret = 0;

	memcpy(chain, iv, sizeof(chain));

	for (size_t off = 0; off < len && ret == 0; off += MT_SM4_BLOCK) {
		unsigned char block[MT_SM4_BLOCK] __attribute__((aligned(4)));
		unsigned char x[MT_SM4_BLOCK] __attribute__((aligned(4)));

		memcpy(block, in + off, MT_SM4_BLOCK);
		if (encrypt) {
			for (int i = 0; i < MT_SM4_BLOCK; i++) {
				x[i] = block[i] ^ chain[i];
			}
			ret = mbedtls_sm4_ecb_encrypt(sm4, out + off, x, MT_SM4_BLOCK);
			memcpy(chain, out + off, MT_SM4_BLOCK);
		} else {
			ret = mbedtls_sm4_ecb_decrypt(sm4, x, in + off, MT_SM4_BLOCK);
			if (ret == 0) {
				for (int i = 0; i < MT_SM4_BLOCK; i++) {
					out[off + i] = x[i] ^ chain[i];
				}
				memcpy(chain, in + off, MT_SM4_BLOCK);
			}
		}
	}
	return ret;
}

ZTEST(cipher, sm4_cbc)
{
	mbedtls_sm4_context sm4;
	unsigned char key[16] __attribute__((aligned(4)));
	unsigned char iv[16] __attribute__((aligned(4)));
	unsigned char pt[32] __attribute__((aligned(4)));
	unsigned char enc[32] __attribute__((aligned(4)));
	unsigned char dec[32] __attribute__((aligned(4)));
	uint32_t t0, us;
	int ret;

	zassert_equal(mt_unhexify(key, MT_SM4_KEY), 16, "sm4 key hex invalid");
	zassert_equal(mt_unhexify(pt, MT_SM4_PT), 32, "sm4 pt hex invalid");
	memcpy(iv, key, sizeof(iv));   /* 本向量 key=IV */

	t0 = k_cycle_get_32();

	mbedtls_sm4_init(&sm4);
	ret = mbedtls_sm4_setkey(key);
	if (ret == 0) {
		ret = sm4_cbc_crypt(&sm4, enc, pt, sizeof(pt), iv, true);
	}
	if (ret == 0) {
		uint8_t exp_ct[32];

		zassert_equal(mt_unhexify(exp_ct, MT_SM4_CBC_CT), 32,
			      "sm4 cbc ct hex invalid");
		mt_assert_mem_eq(enc, exp_ct, sizeof(pt), "sm4-cbc enc");
	}
	if (ret == 0) {
		ret = sm4_cbc_crypt(&sm4, dec, enc, sizeof(enc), iv, false);
		if (ret == 0) {
			mt_assert_mem_eq(dec, pt, sizeof(pt), "sm4-cbc dec");
		}
	}
	mbedtls_sm4_free(&sm4);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("sm4_cbc", "algo=sm4-cbc,bytes=%u,us=%u",
		(unsigned int)sizeof(pt), us);
	zassert_equal(ret, 0, "sm4_cbc ret=%d", ret);
}

/* ---- SM4-CTR：draft-ribose-cfrg-sm4-10 A.2.5.1 风格向量 ---- */
ZTEST(cipher, sm4_ctr)
{
	mbedtls_sm4_context sm4;
	unsigned char key[16] __attribute__((aligned(4)));
	unsigned char iv[16] __attribute__((aligned(4)));
	unsigned char pt[32] __attribute__((aligned(4)));
	unsigned char enc[32] __attribute__((aligned(4)));
	unsigned char dec[32] __attribute__((aligned(4)));
	uint32_t t0, us;
	int ret;

	zassert_equal(mt_unhexify(key, MT_SM4CTR_KEY), 16, "sm4 ctr key hex invalid");
	zassert_equal(mt_unhexify(iv, MT_SM4CTR_IV), 16, "sm4 ctr iv hex invalid");
	zassert_equal(mt_unhexify(pt, MT_SM4CTR_PT), 32, "sm4 ctr pt hex invalid");

	t0 = k_cycle_get_32();

	mbedtls_sm4_init(&sm4);
	ret = mbedtls_sm4_setkey(key);
	if (ret == 0) {
		ret = mbedtls_sm4_setiv(&sm4, iv);
	}
	if (ret == 0) {
		ret = mbedtls_sm4_ctr_crypto(&sm4, enc, pt, sizeof(pt));
	}
	if (ret == 0) {
		uint8_t exp_ct[32];

		zassert_equal(mt_unhexify(exp_ct, MT_SM4CTR_CT), 32,
			      "sm4 ctr ct hex invalid");
		mt_assert_mem_eq(enc, exp_ct, sizeof(pt), "sm4-ctr enc");
	}
	if (ret == 0) {
		ret = mbedtls_sm4_setiv(&sm4, iv);
		if (ret == 0) {
			ret = mbedtls_sm4_ctr_crypto(&sm4, dec, enc, sizeof(enc));
		}
		if (ret == 0) {
			mt_assert_mem_eq(dec, pt, sizeof(pt), "sm4-ctr dec");
		}
	}
	mbedtls_sm4_free(&sm4);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("sm4_ctr", "algo=sm4-ctr,bytes=%u,us=%u",
		(unsigned int)sizeof(pt), us);
	zassert_equal(ret, 0, "sm4_ctr ret=%d", ret);
}
#endif /* CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT */

/* ---- AES-128-CTR：NIST SP800-38A F.5.1（4 块，加密向量 + 解密回环） ---- */
ZTEST(cipher, aes128_ctr)
{
	mbedtls_aes_context aes;
	unsigned char key[16], iv0[16], iv[16], pt[64], ct[64], dec[64];
	unsigned char stream_block[16];
	size_t iv_off = 0;
	uint32_t t0, us;
	int ret;

	ret = mt_unhexify(key, MT_AES128CTR_KEY);
	if (ret == 16) {
		ret = mt_unhexify(iv0, MT_AES128CTR_IV);
	}
	if (ret == 16) {
		ret = mt_unhexify(pt, MT_AES128CTR_PT);
	}
	zassert_equal(ret, 64, "aes ctr vector unhexify ret=%d", ret);
	memcpy(iv, iv0, sizeof(iv));

	t0 = k_cycle_get_32();

	mbedtls_aes_init(&aes);
	ret = mbedtls_aes_setkey_enc(&aes, key, 128);
	if (ret == 0) {
		iv_off = 0;
		memset(stream_block, 0, sizeof(stream_block));
		ret = mbedtls_aes_crypt_ctr(&aes, sizeof(pt), &iv_off,
					    iv, stream_block, pt, ct);
	}
	if (ret == 0) {
		uint8_t exp_ct[64];

		zassert_equal(mt_unhexify(exp_ct, MT_AES128CTR_CT), 64,
			      "aes ctr ct hex invalid");
		mt_assert_mem_eq(ct, exp_ct, sizeof(pt), "aes128-ctr enc");
	}

	if (ret == 0) {
		memcpy(iv, iv0, sizeof(iv));
		ret = mbedtls_aes_setkey_enc(&aes, key, 128);
		if (ret == 0) {
			iv_off = 0;
			memset(stream_block, 0, sizeof(stream_block));
			ret = mbedtls_aes_crypt_ctr(&aes, sizeof(ct), &iv_off,
						    iv, stream_block, ct, dec);
		}
		if (ret == 0) {
			mt_assert_mem_eq(dec, pt, sizeof(pt), "aes128-ctr dec");
		}
	}
	mbedtls_aes_free(&aes);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("aes128_ctr", "algo=aes-128-ctr,bytes=%u,us=%u",
		(unsigned int)sizeof(pt), us);
	zassert_equal(ret, 0, "aes128_ctr ret=%d", ret);
}

/* ---- AES-128-GCM：NIST GCMVS-like，2 块 + AAD，tag 验证 ---- */
ZTEST(cipher, aes128_gcm)
{
	mbedtls_gcm_context gcm;
	unsigned char key[16], iv[16], pt[32], aad[16], ct[32], tag[16], dec[32];
	uint32_t t0, us;
	int ret;

	ret = mt_unhexify(key, MT_AES128GCM_KEY);
	if (ret == 16) {
		ret = mt_unhexify(iv, MT_AES128GCM_IV);
	}
	if (ret == 16) {
		ret = mt_unhexify(pt, MT_AES128GCM_PT);
	}
	if (ret == 32) {
		ret = mt_unhexify(aad, MT_AES128GCM_AAD);
	}
	zassert_equal(ret, 16, "aes gcm vector unhexify ret=%d", ret);

	t0 = k_cycle_get_32();

	mbedtls_gcm_init(&gcm);
	ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
	if (ret == 0) {
		ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
						sizeof(pt), iv, sizeof(iv),
						aad, sizeof(aad), pt, ct,
						sizeof(tag), tag);
	}
	if (ret == 0) {
		uint8_t exp_ct[32], exp_tag[16];

		zassert_equal(mt_unhexify(exp_ct, MT_AES128GCM_CT), 32,
			      "aes gcm ct hex invalid");
		zassert_equal(mt_unhexify(exp_tag, MT_AES128GCM_TAG), 16,
			      "aes gcm tag hex invalid");
		mt_assert_mem_eq(ct, exp_ct, sizeof(pt), "aes128-gcm enc");
		mt_assert_mem_eq(tag, exp_tag, sizeof(tag), "aes128-gcm tag");
	}
	if (ret == 0) {
		ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_DECRYPT,
						sizeof(ct), iv, sizeof(iv),
						aad, sizeof(aad), ct, dec,
						sizeof(tag), tag);
		if (ret == 0) {
			mt_assert_mem_eq(dec, pt, sizeof(pt), "aes128-gcm dec");
		}
	}
	mbedtls_gcm_free(&gcm);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("aes128_gcm", "algo=aes-128-gcm,bytes=%u,us=%u",
		(unsigned int)sizeof(pt), us);
	zassert_equal(ret, 0, "aes128_gcm ret=%d", ret);
}

/* ================== AES-CTR ALT 契约用例（2026-09-29 新增） ==================
 *
 * 背景：客户反馈 CONFIG_MBEDTLS_CIPHER_AES_LINKEDSEMI=y 下
 * modules/crypto/mbedtls/library/aes_alt.c:358 的 mbedtls_aes_crypt_ctr()
 * 未实现 mbedtls 要求的 CTR 调用状态管理（nc_off / stream_block /
 * nonce_counter 三个状态参数一个都没用），且循环体假定长度是 16 的整数倍、
 * 输入输出满足 32 位对齐 —— 非整块输入会越界读写。
 *
 * 既有 aes128_ctr 只覆盖「单次调用 + 4 个整块 + iv_off=0」这一条恰好可能
 * 正确的路径，且只断言密文，故对本缺陷零覆盖。下列各项按 mbedtls 官方
 * 契约补齐。期望值全部取自 NIST SP800-38A F.5.1 公开向量（见
 * expected/cipher_vectors_extra.h，由生成器自校验），不取自被测实现。
 *
 * 写法约定：每个测试项**先算标志、先打 INFO、后断言** —— zassert 会中止
 * 整个测试函数，若把断言放前面，后面的诊断信息就进不了报告。
 */

/* 16 字节 buffer → 小写 hex 字符串（供 INFO 诊断用） */
static void mt_hex(char *dst, size_t dstsz, const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len && (2 * i + 3) <= dstsz; i++) {
		snprintf(dst + 2 * i, 3, "%02x", buf[i]);
	}
	dst[2 * i] = '\0';
}

/* ---- 判定 ALT 的 ECB 数据字节序契约（FIPS-197 附录 C.1） ----
 *
 * aes_alt.c 三条路径对同一组 DATA/RES 寄存器用了互相矛盾的字节序约定：
 *   ECB (:149-159) 不做转换、且是唯一传 CRYPT_TYPE=0x2 的；
 *   CBC (:200-211) 与 CTR (:383-393) 做 __builtin_bswap32、传 CRYPT_TYPE=0x0。
 * 同一硬件不可能两种约定都对。
 *
 * 判别不变量（与硬件字节序无关）：单块 CBC(IV=0) 的密文、以及单块
 * CTR(counter=明文块, 明文=0) 的输出，都必须等于 AES-ECB(同一块)。
 * 三者但凡有一个偏离 FIPS-197 C.1，就说明该路径的字节序契约错。
 * 注意 mbedtls 3.6.2 的 GCM 经 block_cipher.c:189 落到 mbedtls_aes_crypt_ecb，
 * 故 ECB 的判定同时决定 GCM/CFB/OFB/XTS 是否正确。
 */
ZTEST(cipher, aes_ecb_order)
{
	mbedtls_aes_context aes;
	unsigned char key[16], pt[16], nist[16];
	unsigned char by_ecb[16], by_cbc[16], by_ctr[16];
	unsigned char iv[16], counter[16], zeros[16], sb[16];
	char h_ecb[40], h_cbc[40], h_ctr[40], h_nist[40];
	int eq_ecb, eq_cbc, eq_ctr;
	size_t off;
	int ret;

	zassert_equal(mt_unhexify(key, MT_AES128ECB_KEY), 16, "ecb key hex invalid");
	zassert_equal(mt_unhexify(pt, MT_AES128ECB_PT), 16, "ecb pt hex invalid");
	zassert_equal(mt_unhexify(nist, MT_AES128ECB_CT), 16, "ecb ct hex invalid");

	mbedtls_aes_init(&aes);
	zassert_equal(mbedtls_aes_setkey_enc(&aes, key, 128), 0, "ecb setkey failed");

	ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, pt, by_ecb);
	zassert_equal(ret, 0, "ecb ret=%d", ret);

	/* CBC 单块、IV=0 → 等于 AES-ECB(pt) */
	memset(iv, 0, sizeof(iv));
	ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, 16, iv, pt, by_cbc);
	zassert_equal(ret, 0, "cbc(1blk) ret=%d", ret);

	/* CTR 单块、counter=pt、明文全 0 → 输出即 keystream = AES-ECB(pt) */
	memcpy(counter, pt, sizeof(counter));
	memset(zeros, 0, sizeof(zeros));
	memset(sb, 0, sizeof(sb));
	off = 0;
	ret = mbedtls_aes_crypt_ctr(&aes, 16, &off, counter, sb, zeros, by_ctr);
	zassert_equal(ret, 0, "ctr(1blk) ret=%d", ret);

	mbedtls_aes_free(&aes);

	eq_ecb = (memcmp(by_ecb, nist, 16) == 0);
	eq_cbc = (memcmp(by_cbc, nist, 16) == 0);
	eq_ctr = (memcmp(by_ctr, nist, 16) == 0);

	mt_hex(h_ecb, sizeof(h_ecb), by_ecb, 16);
	mt_hex(h_cbc, sizeof(h_cbc), by_cbc, 16);
	mt_hex(h_ctr, sizeof(h_ctr), by_ctr, 16);
	mt_hex(h_nist, sizeof(h_nist), nist, 16);

	ZT_INFO("aes_ecb_order",
		"ecb_eq_nist=%d,cbc_eq_nist=%d,ctr_eq_nist=%d,ecb=%s,cbc=%s,ctr=%s,nist=%s",
		eq_ecb, eq_cbc, eq_ctr, h_ecb, h_cbc, h_ctr, h_nist);

	zassert_true(eq_ecb, "AES-ECB deviates from FIPS-197 C.1: got %s / expect %s",
		     h_ecb, h_nist);
	zassert_true(eq_cbc, "AES-CBC(1blk,IV=0) deviates from FIPS-197 C.1: got %s / expect %s",
		     h_cbc, h_nist);
	zassert_true(eq_ctr, "AES-CTR(1blk,counter=pt) deviates from FIPS-197 C.1: got %s / expect %s",
		     h_ctr, h_nist);
}

/* ---- CTR 分段等价 + 调用状态（mbedtls 契约核心） ----
 *
 * 同一 key/counter/context 下，7+16+41 三次调用的结果必须与一次加密 64 字节
 * 逐字节相同；且每次调用后 nc_off / nonce_counter / stream_block 必须与
 * mbedtls 软件基线（aes.c:1437-1475）一致。
 * 期望状态由 NIST counter 序列直接推出：生成第 1 个 block 后 counter
 * 由 ...FEFF 递增为 ...FF00，生成第 2 个后为 ...FF01；stream_block 即
 * 对应的 keystream 块。
 */
ZTEST(cipher, aes128_ctr_seg)
{
	mbedtls_aes_context aes;
	unsigned char key[16], iv0[16], pt[64], ct[64], exp_ct[64];
	unsigned char ks[64], cnt1[16], cnt2[16];
	unsigned char counter[16], sb[16];
	size_t off1, off2, off3;
	int st1, st2;
	int ret;

	zassert_equal(mt_unhexify(key, MT_AES128CTR_KEY), 16, "ctr key hex invalid");
	zassert_equal(mt_unhexify(iv0, MT_AES128CTR_IV), 16, "ctr iv hex invalid");
	zassert_equal(mt_unhexify(pt, MT_AES128CTR_PT), 64, "ctr pt hex invalid");
	zassert_equal(mt_unhexify(exp_ct, MT_AES128CTR_CT), 64, "ctr ct hex invalid");
	zassert_equal(mt_unhexify(ks, MT_AES128CTR_KS), 64, "ctr ks hex invalid");
	zassert_equal(mt_unhexify(cnt1, MT_AES128CTR_CNT1), 16, "ctr cnt1 hex invalid");
	zassert_equal(mt_unhexify(cnt2, MT_AES128CTR_CNT2), 16, "ctr cnt2 hex invalid");

	mbedtls_aes_init(&aes);
	zassert_equal(mbedtls_aes_setkey_enc(&aes, key, 128), 0, "ctr setkey failed");

	memcpy(counter, iv0, sizeof(counter));
	memset(sb, 0, sizeof(sb));
	off1 = 0;

	ret = mbedtls_aes_crypt_ctr(&aes, 7, &off1, counter, sb, pt, ct);
	zassert_equal(ret, 0, "seg1(7B) ret=%d", ret);

	/* 生成 1 个 block：nc_off=7，counter=CNT1，stream_block=ks[0..15] */
	st1 = (off1 == 7) && (memcmp(counter, cnt1, 16) == 0) &&
	      (memcmp(sb, ks, 16) == 0);

	off2 = off1;
	ret = mbedtls_aes_crypt_ctr(&aes, 16, &off2, counter, sb, pt + 7, ct + 7);
	zassert_equal(ret, 0, "seg2(16B) ret=%d", ret);

	/* 消耗余下 9 字节 + 新生成 1 个 block：nc_off=7，counter=CNT2，sb=ks[16..31] */
	st2 = (off2 == 7) && (memcmp(counter, cnt2, 16) == 0) &&
	      (memcmp(sb, ks + 16, 16) == 0);

	off3 = off2;
	ret = mbedtls_aes_crypt_ctr(&aes, 41, &off3, counter, sb, pt + 23, ct + 23);
	zassert_equal(ret, 0, "seg3(41B) ret=%d", ret);

	mbedtls_aes_free(&aes);

	ZT_INFO("aes128_ctr_seg",
		"algo=aes-128-ctr,seg=7+16+41,bytes=64,seg_eq_nist=%d,state_after7=%d,state_after23=%d,nc_off_final=%u",
		(memcmp(ct, exp_ct, 64) == 0), st1, st2, (unsigned int)off3);

	mt_assert_mem_eq(ct, exp_ct, 64, "aes128-ctr segmented 7+16+41 vs NIST F.5.1");

	zassert_true(st1, "state after 7B call wrong: nc_off=%u (expect 7)",
		     (unsigned int)off1);
	zassert_true(st2, "state after 23B cumulative wrong: nc_off=%u (expect 7)",
		     (unsigned int)off2);
	zassert_equal(off3, 0, "nc_off after 64B cumulative =%u (expect 0)", (unsigned int)off3);
}

/* ---- CTR 非整块长度 + 越界哨兵 ----
 *
 * 契约：任意长度都必须只访问 [0,len) —— 既有实现按 16 字节整块无条件读写，
 * 1/15/17/31 字节都会越界。哨兵字节被改写即判失败。
 */
ZTEST(cipher, aes128_ctr_len)
{
	static const size_t lens[] = { 1, 15, 17, 31 };
	mbedtls_aes_context aes;
	unsigned char key[16], iv0[16], pt[64], exp_ct[64];
	unsigned char counter[16], sb[16];
	unsigned char g[64 + 2];
	int ok_len[4], ok_guard[4];
	size_t off;
	uint32_t t0, us;
	int i, ret;

	zassert_equal(mt_unhexify(key, MT_AES128CTR_KEY), 16, "ctr key hex invalid");
	zassert_equal(mt_unhexify(iv0, MT_AES128CTR_IV), 16, "ctr iv hex invalid");
	zassert_equal(mt_unhexify(pt, MT_AES128CTR_PT), 64, "ctr pt hex invalid");
	zassert_equal(mt_unhexify(exp_ct, MT_AES128CTR_CT), 64, "ctr ct hex invalid");

	mbedtls_aes_init(&aes);
	zassert_equal(mbedtls_aes_setkey_enc(&aes, key, 128), 0, "ctr setkey failed");

	t0 = k_cycle_get_32();

	for (i = 0; i < 4; i++) {
		size_t len = lens[i];

		memset(g, 0xA5, sizeof(g));
		memcpy(counter, iv0, sizeof(counter));
		memset(sb, 0, sizeof(sb));
		off = 0;

		ret = mbedtls_aes_crypt_ctr(&aes, len, &off, counter, sb, pt, g + 1);
		if (ret != 0) {
			zassert_equal(ret, 0, "len=%u ret=%d", (unsigned int)len, ret);
		}

		ok_len[i] = (memcmp(g + 1, exp_ct, len) == 0);
		ok_guard[i] = (g[0] == 0xA5) && (g[len + 1] == 0xA5);
	}

	mbedtls_aes_free(&aes);

	us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);

	ZT_INFO("aes128_ctr_len",
		"algo=aes-128-ctr,lens=1_15_17_31,len1=%d,len15=%d,len17=%d,len31=%d,guard_ok=%d,us=%u",
		ok_len[0], ok_len[1], ok_len[2], ok_len[3],
		(ok_guard[0] && ok_guard[1] && ok_guard[2] && ok_guard[3]), us);

	zassert_true(ok_len[0], "CTR len=1 ciphertext != NIST F.5.1");
	zassert_true(ok_len[1], "CTR len=15 ciphertext != NIST F.5.1");
	zassert_true(ok_len[2], "CTR len=17 ciphertext != NIST F.5.1");
	zassert_true(ok_len[3], "CTR len=31 ciphertext != NIST F.5.1");
	zassert_true(ok_guard[0] && ok_guard[1] && ok_guard[2] && ok_guard[3],
		     "CTR out-of-bounds on non-block length (buffer guard bytes overwritten)");
}

/* ---- CTR 原地（input == output） ---- */
ZTEST(cipher, aes128_ctr_inplace)
{
	mbedtls_aes_context aes;
	unsigned char key[16], iv0[16], pt[64], exp_ct[64];
	unsigned char counter[16], sb[16];
	unsigned char buf[64];
	int eq;
	size_t off;
	int ret;

	zassert_equal(mt_unhexify(key, MT_AES128CTR_KEY), 16, "ctr key hex invalid");
	zassert_equal(mt_unhexify(iv0, MT_AES128CTR_IV), 16, "ctr iv hex invalid");
	zassert_equal(mt_unhexify(pt, MT_AES128CTR_PT), 64, "ctr pt hex invalid");
	zassert_equal(mt_unhexify(exp_ct, MT_AES128CTR_CT), 64, "ctr ct hex invalid");

	mbedtls_aes_init(&aes);
	zassert_equal(mbedtls_aes_setkey_enc(&aes, key, 128), 0, "ctr setkey failed");

	memcpy(buf, pt, sizeof(buf));
	memcpy(counter, iv0, sizeof(counter));
	memset(sb, 0, sizeof(sb));
	off = 0;

	ret = mbedtls_aes_crypt_ctr(&aes, sizeof(buf), &off, counter, sb, buf, buf);
	zassert_equal(ret, 0, "in-place ret=%d", ret);

	mbedtls_aes_free(&aes);

	eq = (memcmp(buf, exp_ct, sizeof(buf)) == 0);

	ZT_INFO("aes128_ctr_inplace", "algo=aes-128-ctr,bytes=64,inplace_eq_nist=%d", eq);

	mt_assert_mem_eq(buf, exp_ct, sizeof(buf), "aes128-ctr in-place vs NIST F.5.1");
}

/* ---- CTR 未对齐输入/输出 ----
 *
 * mbedtls 不要求调用方缓冲区满足任何对齐；既有实现强转 (uint32_t*)，
 * 未对齐输入在 RISC-V 上可能直接触发 load/store-address-misaligned。
 * 本项放最后 —— 若真触发异常，不影响前面各项的结果上报。
 */
ZTEST(cipher, aes128_ctr_unaligned)
{
	mbedtls_aes_context aes;
	unsigned char key[16], iv0[16], pt_raw[64], exp_ct[64];
	unsigned char pt[64 + 1], out[64 + 1];
	unsigned char counter[16], sb[16];
	int eq;
	size_t off;
	int ret;

	zassert_equal(mt_unhexify(key, MT_AES128CTR_KEY), 16, "ctr key hex invalid");
	zassert_equal(mt_unhexify(iv0, MT_AES128CTR_IV), 16, "ctr iv hex invalid");
	zassert_equal(mt_unhexify(pt_raw, MT_AES128CTR_PT), 64, "ctr pt hex invalid");
	zassert_equal(mt_unhexify(exp_ct, MT_AES128CTR_CT), 64, "ctr ct hex invalid");

	/* 输入输出都偏移 1 字节 → 必然不是 4 字节对齐 */
	memcpy(pt + 1, pt_raw, 64);

	mbedtls_aes_init(&aes);
	zassert_equal(mbedtls_aes_setkey_enc(&aes, key, 128), 0, "ctr setkey failed");

	memcpy(counter, iv0, sizeof(counter));
	memset(sb, 0, sizeof(sb));
	off = 0;

	ret = mbedtls_aes_crypt_ctr(&aes, 64, &off, counter, sb, pt + 1, out + 1);
	zassert_equal(ret, 0, "unaligned ret=%d", ret);

	mbedtls_aes_free(&aes);

	eq = (memcmp(out + 1, exp_ct, 64) == 0);

	ZT_INFO("aes128_ctr_unaligned", "algo=aes-128-ctr,bytes=64,unaligned_eq_nist=%d", eq);

	mt_assert_mem_eq(out + 1, exp_ct, 64, "aes128-ctr unaligned buffer vs NIST F.5.1");
}

/* ---- AES-128-XTS：IEEE P1619/D16 向量 1-3（KAT + 解密回环 + 未整块回环） ----
 *
 * 本项是 O-42(4) 的回归门禁：驱动原先把 XTS 的 key1 存进**文件级全局量**、
 * 且只存一个指向调用者缓冲的指针 —— 多 context 并存时互相覆盖，调用者改动
 * 或释放 key 缓冲后即悬垂。官方把 key1 存进 ctx->crypt。
 *
 * 真源：mbedtls tests/suites/test_suite_aes.xts.data（IEEE P1619/D16，
 * 出处 http://grouper.ieee.org/groups/1619/email/pdf00086.pdf）。
 * 该向量已由 tests/tmp/gen_cipher_vectors.py 内**按 XTS 定义从零实现**的
 * 参考实现复现验证（见生成器 assert）。
 *
 * 消息用 ASCII：本项目串口采集环节会把测试源码里的非 ASCII 字节损坏成
 * U+FFFD 替换串（runner 2026-09-29 实测），中文标签在报告里读不出来。
 */
#if defined(MBEDTLS_CIPHER_MODE_XTS)
ZTEST(cipher, aes128_xts)
{
	static const struct {
		const char *key, *du, *pt, *ct;
	} vec[3] = {
		{ MT_AES128XTS1_KEY, MT_AES128XTS1_DU, MT_AES128XTS1_PT, MT_AES128XTS1_CT },
		{ MT_AES128XTS2_KEY, MT_AES128XTS2_DU, MT_AES128XTS2_PT, MT_AES128XTS2_CT },
		{ MT_AES128XTS3_KEY, MT_AES128XTS3_DU, MT_AES128XTS3_PT, MT_AES128XTS3_CT },
	};
	mbedtls_aes_xts_context xts;
	unsigned char key[32], du[16], pt[32], exp_ct[32];
	unsigned char ct[32], dec[32];
	unsigned char big_pt[55], big_ct[55], big_dec[55];
	int kat_ok[3], dec_ok[3], steal_ok;
	int i, r1, r2, r3, r4;

	for (i = 0; i < 3; i++) {
		kat_ok[i] = 0;
		dec_ok[i] = 0;

		zassert_equal(mt_unhexify(key, vec[i].key), 32, "xts v%d key hex", i + 1);
		zassert_equal(mt_unhexify(du, vec[i].du), 16, "xts v%d du hex", i + 1);
		zassert_equal(mt_unhexify(pt, vec[i].pt), 32, "xts v%d pt hex", i + 1);
		zassert_equal(mt_unhexify(exp_ct, vec[i].ct), 32, "xts v%d ct hex", i + 1);

		mbedtls_aes_xts_init(&xts);
		r1 = mbedtls_aes_xts_setkey_enc(&xts, key, 256);
		r2 = mbedtls_aes_crypt_xts(&xts, MBEDTLS_AES_ENCRYPT, 32, du, pt, ct);
		mbedtls_aes_xts_free(&xts);
		kat_ok[i] = (r1 == 0) && (r2 == 0) && (memcmp(ct, exp_ct, 32) == 0);

		/* 用 KAT 的密文解回明文（不是本实现自己加密的结果） */
		mbedtls_aes_xts_init(&xts);
		r3 = mbedtls_aes_xts_setkey_dec(&xts, key, 256);
		r4 = mbedtls_aes_crypt_xts(&xts, MBEDTLS_AES_DECRYPT, 32, du, exp_ct, dec);
		mbedtls_aes_xts_free(&xts);
		dec_ok[i] = (r3 == 0) && (r4 == 0) && (memcmp(dec, pt, 32) == 0);
	}

	/* 未整块长度 55B -> 走 ciphertext stealing 分支，加解密须回环一致 */
	for (i = 0; i < 55; i++) {
		big_pt[i] = (unsigned char)(i * 7 + 3);
	}
	zassert_equal(mt_unhexify(key, MT_AES128XTS2_KEY), 32, "xts key hex");
	zassert_equal(mt_unhexify(du, MT_AES128XTS2_DU), 16, "xts du hex");

	mbedtls_aes_xts_init(&xts);
	r1 = mbedtls_aes_xts_setkey_enc(&xts, key, 256);
	r2 = mbedtls_aes_crypt_xts(&xts, MBEDTLS_AES_ENCRYPT, 55, du, big_pt, big_ct);
	mbedtls_aes_xts_free(&xts);
	mbedtls_aes_xts_init(&xts);
	r3 = mbedtls_aes_xts_setkey_dec(&xts, key, 256);
	r4 = mbedtls_aes_crypt_xts(&xts, MBEDTLS_AES_DECRYPT, 55, du, big_ct, big_dec);
	mbedtls_aes_xts_free(&xts);

	/* 密文不得等于明文（防"加密是恒等"的退化通过） */
	steal_ok = (r1 == 0) && (r2 == 0) && (r3 == 0) && (r4 == 0) &&
		   (memcmp(big_dec, big_pt, 55) == 0) &&
		   (memcmp(big_ct, big_pt, 55) != 0);

	ZT_INFO("aes128_xts",
		"algo=aes-128-xts,kat1=%d,kat2=%d,kat3=%d,dec1=%d,dec2=%d,dec3=%d,steal55=%d",
		kat_ok[0], kat_ok[1], kat_ok[2], dec_ok[0], dec_ok[1], dec_ok[2], steal_ok);

	zassert_true(kat_ok[0], "xts IEEE1619 v1 KAT mismatch");
	zassert_true(kat_ok[1], "xts IEEE1619 v2 KAT mismatch");
	zassert_true(kat_ok[2], "xts IEEE1619 v3 KAT mismatch");
	zassert_true(dec_ok[0], "xts IEEE1619 v1 decrypt mismatch");
	zassert_true(dec_ok[1], "xts IEEE1619 v2 decrypt mismatch");
	zassert_true(dec_ok[2], "xts IEEE1619 v3 decrypt mismatch");
	zassert_true(steal_ok, "xts 55B ciphertext-stealing round trip failed");
}

/* ---- AES-128-XTS 双 context 交错 ----
 *
 * O-42(4) 的直接门禁：两个 XTS context 绑定不同密钥后**交错使用**，各自仍须
 * 产出自己的 KAT。旧实现把 key1 存在文件级全局量里，后 setkey 的会覆盖前一个，
 * 于是第一个 context 会用第二个的密钥算出错误密文。
 */
ZTEST(cipher, aes128_xts_multi_ctx)
{
	mbedtls_aes_xts_context a, b;
	unsigned char ka[32], kb[32], dua[16], dub[16];
	unsigned char pa[32], pb[32], ea[32], eb[32];
	unsigned char oa[32], ob[32];
	int ka_ok, kb_ok, r;

	zassert_equal(mt_unhexify(ka, MT_AES128XTS2_KEY), 32, "xts a key hex");
	zassert_equal(mt_unhexify(dua, MT_AES128XTS2_DU), 16, "xts a du hex");
	zassert_equal(mt_unhexify(pa, MT_AES128XTS2_PT), 32, "xts a pt hex");
	zassert_equal(mt_unhexify(ea, MT_AES128XTS2_CT), 32, "xts a ct hex");
	zassert_equal(mt_unhexify(kb, MT_AES128XTS3_KEY), 32, "xts b key hex");
	zassert_equal(mt_unhexify(dub, MT_AES128XTS3_DU), 16, "xts b du hex");
	zassert_equal(mt_unhexify(pb, MT_AES128XTS3_PT), 32, "xts b pt hex");
	zassert_equal(mt_unhexify(eb, MT_AES128XTS3_CT), 32, "xts b ct hex");

	/* 两个 context 先后 setkey —— 旧实现此步即已互相覆盖 */
	mbedtls_aes_xts_init(&a);
	mbedtls_aes_xts_init(&b);
	zassert_equal(mbedtls_aes_xts_setkey_enc(&a, ka, 256), 0, "xts a setkey");
	zassert_equal(mbedtls_aes_xts_setkey_enc(&b, kb, 256), 0, "xts b setkey");

	/* 交错：b -> a -> b -> a */
	r = mbedtls_aes_crypt_xts(&b, MBEDTLS_AES_ENCRYPT, 32, dub, pb, ob);
	kb_ok = (r == 0) && (memcmp(ob, eb, 32) == 0);
	r = mbedtls_aes_crypt_xts(&a, MBEDTLS_AES_ENCRYPT, 32, dua, pa, oa);
	ka_ok = (r == 0) && (memcmp(oa, ea, 32) == 0);
	memset(ob, 0, sizeof(ob));
	r = mbedtls_aes_crypt_xts(&b, MBEDTLS_AES_ENCRYPT, 32, dub, pb, ob);
	kb_ok = kb_ok && (r == 0) && (memcmp(ob, eb, 32) == 0);

	mbedtls_aes_xts_free(&a);
	mbedtls_aes_xts_free(&b);

	ZT_INFO("aes128_xts_multi_ctx",
		"algo=aes-128-xts,ctx_a_eq_kat=%d,ctx_b_eq_kat=%d", ka_ok, kb_ok);

	zassert_true(ka_ok, "xts ctx a output != its own KAT after interleaving with ctx b");
	zassert_true(kb_ok, "xts ctx b output != its own KAT after interleaving with ctx a");
}
#endif /* MBEDTLS_CIPHER_MODE_XTS */

/* ---- ALT NULL 入参加固（只在 LINKEDSEMI 硬件 AES 下编入） ----
 *
 * 这是**超出 mbedtls 契约**的加固承诺：上游 mbedtls_aes_setkey_enc() 对 ctx/key
 * 不做 NULL 检查（library/aes.c 直接解引用 ctx->buf 与 key），软件实现会 fault
 * 而不是返回错误码。因此"NULL 入参返回错误码"只由 LINKEDSEMI 的 ALT 提供，
 * 跑在纯软件 case 上无意义 —— 故用 CONFIG_MBEDTLS_CIPHER_AES_LINKEDSEMI 门控。
 *
 * 这条覆盖原先混在 robustness_test.c 的 `aes_bad_params` 里，导致纯软件
 * case 因一条它永远无法满足的断言而失败；现拆到此处（见该文件的说明注释）。
 *
 * 判据：每一项都必须**返回非 0**，而不是把测试线程打死。若驱动解引用 NULL，
 * 线程被内核杀掉、ztest 结果停在初值，此时由判定链 R9（执行窗口内出现
 * ZEPHYR FATAL ERROR 判 FAIL）兜住，不会假通过。
 */
#if defined(CONFIG_MBEDTLS_CIPHER_AES_LINKEDSEMI)
ZTEST(cipher, aes_alt_null_params)
{
	mbedtls_aes_context ctx;
	unsigned char key[16], iv[16], in[16], out[16];
	unsigned char nc[16], sb[16];
	int r_setkey_ctx, r_setkey_key, r_cbc_in, r_cbc_out;
	int r_ecb_in, r_ecb_out, r_ctr;

	memset(key, 0xab, sizeof(key));
	memset(iv, 0, sizeof(iv));
	memset(in, 0, sizeof(in));
	memset(nc, 0, sizeof(nc));
	memset(sb, 0, sizeof(sb));

	mbedtls_aes_init(&ctx);
	zassert_equal(mbedtls_aes_setkey_enc(&ctx, key, 128), 0,
		      "baseline setkey failed");

	r_setkey_ctx = mbedtls_aes_setkey_enc(NULL, key, 128);
	r_setkey_key = mbedtls_aes_setkey_enc(&ctx, NULL, 128);
	r_cbc_in = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, 16, iv, NULL, out);
	r_cbc_out = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, 16, iv, in, NULL);
	r_ecb_in = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, NULL, out);
	r_ecb_out = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, NULL);
	r_ctr = mbedtls_aes_crypt_ctr(&ctx, 16, NULL, nc, sb, in, out);

	mbedtls_aes_free(&ctx);

	ZT_INFO("aes_alt_null_params",
		"setkey_null_ctx=%d,setkey_null_key=%d,cbc_null_in=%d,cbc_null_out=%d,ecb_null_in=%d,ecb_null_out=%d,ctr_null_ncoff=%d",
		r_setkey_ctx, r_setkey_key, r_cbc_in, r_cbc_out,
		r_ecb_in, r_ecb_out, r_ctr);

	zassert_not_equal(r_setkey_ctx, 0, "setkey(NULL ctx) must return an error, not fault");
	zassert_not_equal(r_setkey_key, 0, "setkey(NULL key) must return an error, not fault");
	zassert_not_equal(r_cbc_in, 0, "crypt_cbc(NULL input) must return an error, not fault");
	zassert_not_equal(r_cbc_out, 0, "crypt_cbc(NULL output) must return an error, not fault");
	zassert_not_equal(r_ecb_in, 0, "crypt_ecb(NULL input) must return an error, not fault");
	zassert_not_equal(r_ecb_out, 0, "crypt_ecb(NULL output) must return an error, not fault");
	zassert_not_equal(r_ctr, 0, "crypt_ctr(NULL nc_off) must return an error, not fault");
}
#endif /* CONFIG_MBEDTLS_CIPHER_AES_LINKEDSEMI */

/* ---- ECB/CBC 未对齐缓冲（覆盖 aes_alt.c 的字节访问器回退分支） ----
 *
 * 驱动为对齐的调用方走字访问快路径，未对齐时回退到可移植访问器（在 RISC-V 上
 * 是逐字节读写）。这条回退分支此前只被 CTR 间接覆盖（它走 ECB 核心），
 * **CBC 的回退分支没有别处碰得到** —— 故补本项。
 */
ZTEST(cipher, aes_ecb_cbc_unaligned)
{
	mbedtls_aes_context aes;
	unsigned char key[16], iv0[16], pt_raw[64], ct_raw[64], exp[16];
	unsigned char in[64 + 1], out[64 + 1], ivbuf[16 + 1];
	int ecb_ok, cbc_ok, ret;

	/* ECB：FIPS-197 附录 C.1，输入输出各偏移 1 字节 */
	zassert_equal(mt_unhexify(key, MT_AES128ECB_KEY), 16, "ecb key hex invalid");
	zassert_equal(mt_unhexify(pt_raw, MT_AES128ECB_PT), 16, "ecb pt hex invalid");
	zassert_equal(mt_unhexify(exp, MT_AES128ECB_CT), 16, "ecb ct hex invalid");

	mbedtls_aes_init(&aes);
	zassert_equal(mbedtls_aes_setkey_enc(&aes, key, 128), 0, "ecb setkey failed");

	memcpy(in + 1, pt_raw, 16);
	ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, in + 1, out + 1);
	ecb_ok = (ret == 0) && (memcmp(out + 1, exp, 16) == 0);

	/* CBC：NIST F.2.1 前 2 块；输入/输出/IV 三者全部未对齐 */
	zassert_equal(mt_unhexify(key, MT_AES128CBC_KEY), 16, "cbc key hex invalid");
	zassert_equal(mt_unhexify(iv0, MT_AES128CBC_IV), 16, "cbc iv hex invalid");
	zassert_equal(mt_unhexify(pt_raw, MT_AES128CBC_PT), 64, "cbc pt hex invalid");
	zassert_equal(mt_unhexify(ct_raw, MT_AES128CBC_CT), 64, "cbc ct hex invalid");

	memcpy(in + 1, pt_raw, 32);
	memcpy(ivbuf + 1, iv0, 16);

	zassert_equal(mbedtls_aes_setkey_enc(&aes, key, 128), 0, "cbc setkey failed");
	ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, 32,
				    ivbuf + 1, in + 1, out + 1);
	cbc_ok = (ret == 0) && (memcmp(out + 1, ct_raw, 32) == 0);

	mbedtls_aes_free(&aes);

	ZT_INFO("aes_ecb_cbc_unaligned", "ecb_unaligned_ok=%d,cbc_unaligned_ok=%d",
		ecb_ok, cbc_ok);

	zassert_true(ecb_ok, "AES-ECB with unaligned buffers != FIPS-197 C.1");
	zassert_true(cbc_ok, "AES-CBC with unaligned buffers != NIST F.2.1");
}
