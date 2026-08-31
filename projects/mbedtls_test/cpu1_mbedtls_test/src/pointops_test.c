/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN ECC point operations 直接 API 测试（P-256/P-384/P-521）。
 *
 * 被测接口：ls_otbn_pointops_field_bytes / scalar_mult / base_mult / point_add。
 * 向量来源：linkedsemi_zephyr_project/samples/otbn_pointops/src/test_vectors.h
 * （little-endian，已硬件交叉验证）。
 *
 * 仅在 CONFIG_MBEDTLS_LINKEDSEMI_OTBN=y 的 case 编译；纯软 case 不链接。
 */

#include "mbedtls_test_protocol.h"
#include "mbedtls_test.h"
#include "ztest_glue.h"
#include "pointops_vectors.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <otbn_pointops.h>

#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN)
#define MT_HAVE_POINTOPS 1
#endif

#ifdef MT_HAVE_POINTOPS

ZTEST_SUITE(pointops, NULL, NULL, NULL, NULL, NULL);

struct mt_pointops_vector {
	enum ls_otbn_pointops_curve curve;
	const char *name;
	size_t n;
	const uint8_t *k;
	const uint8_t *px, *py;
	const uint8_t *gx, *gy;
	const uint8_t *bm_x, *bm_y;
	const uint8_t *sm_x, *sm_y;
	const uint8_t *pa_x, *pa_y;
};

static const struct mt_pointops_vector mt_pointops_vecs[] = {
	{
		.curve = LS_OTBN_POINTOPS_CURVE_P256,
		.name = "P-256", .n = 32,
		.k = p256_k,
		.px = p256_px, .py = p256_py,
		.gx = p256_gx, .gy = p256_gy,
		.bm_x = p256_bm_x, .bm_y = p256_bm_y,
		.sm_x = p256_sm_x, .sm_y = p256_sm_y,
		.pa_x = p256_pa_x, .pa_y = p256_pa_y,
	},
	{
		.curve = LS_OTBN_POINTOPS_CURVE_P384,
		.name = "P-384", .n = 48,
		.k = p384_k,
		.px = p384_px, .py = p384_py,
		.gx = p384_gx, .gy = p384_gy,
		.bm_x = p384_bm_x, .bm_y = p384_bm_y,
		.sm_x = p384_sm_x, .sm_y = p384_sm_y,
		.pa_x = p384_pa_x, .pa_y = p384_pa_y,
	},
	{
		.curve = LS_OTBN_POINTOPS_CURVE_P521,
		.name = "P-521", .n = 66,
		.k = p521_k,
		.px = p521_px, .py = p521_py,
		.gx = p521_gx, .gy = p521_gy,
		.bm_x = p521_bm_x, .bm_y = p521_bm_y,
		.sm_x = p521_sm_x, .sm_y = p521_sm_y,
		.pa_x = p521_pa_x, .pa_y = p521_pa_y,
	},
};

/* ---- field_bytes：各曲线返回正确字节数 ---- */
ZTEST(pointops, field_bytes)
{
	size_t b;
	int ret;

	ret = ls_otbn_pointops_field_bytes(LS_OTBN_POINTOPS_CURVE_P256, &b);
	zassert_equal(ret, 0, "field_bytes P256 ret=%d", ret);
	zassert_equal(b, 32, "P256 bytes=%u", (unsigned int)b);

	ret = ls_otbn_pointops_field_bytes(LS_OTBN_POINTOPS_CURVE_P384, &b);
	zassert_equal(ret, 0, "field_bytes P384 ret=%d", ret);
	zassert_equal(b, 48, "P384 bytes=%u", (unsigned int)b);

	ret = ls_otbn_pointops_field_bytes(LS_OTBN_POINTOPS_CURVE_P521, &b);
	zassert_equal(ret, 0, "field_bytes P521 ret=%d", ret);
	zassert_equal(b, 66, "P521 bytes=%u", (unsigned int)b);
}

/* ---- base_mult：R = k * G ---- */
ZTEST(pointops, base_mult)
{
	uint8_t rx[66], ry[66];
	int ret = 0;
	uint32_t t0 = k_cycle_get_32();

	for (size_t i = 0; i < ARRAY_SIZE(mt_pointops_vecs) && ret == 0; i++) {
		const struct mt_pointops_vector *v = &mt_pointops_vecs[i];

		ret = ls_otbn_pointops_base_mult(v->curve, v->k, rx, ry);
		if (ret == 0) {
			mt_assert_mem_eq(rx, v->bm_x, v->n,
					 "%s base_mult x", v->name);
			mt_assert_mem_eq(ry, v->bm_y, v->n,
					 "%s base_mult y", v->name);
		}
	}

	uint32_t us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("pointops_base_mult", "curves=%u,us=%u",
		(unsigned int)ARRAY_SIZE(mt_pointops_vecs), us);
	zassert_equal(ret, 0, "pointops base_mult ret=%d", ret);
}

/* ---- scalar_mult：R = k * P ---- */
ZTEST(pointops, scalar_mult)
{
	uint8_t rx[66], ry[66];
	int ret = 0;
	uint32_t t0 = k_cycle_get_32();

	for (size_t i = 0; i < ARRAY_SIZE(mt_pointops_vecs) && ret == 0; i++) {
		const struct mt_pointops_vector *v = &mt_pointops_vecs[i];

		ret = ls_otbn_pointops_scalar_mult(v->curve, v->k,
						   v->px, v->py, rx, ry);
		if (ret == 0) {
			mt_assert_mem_eq(rx, v->sm_x, v->n,
					 "%s scalar_mult x", v->name);
			mt_assert_mem_eq(ry, v->sm_y, v->n,
					 "%s scalar_mult y", v->name);
		}
	}

	uint32_t us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("pointops_scalar_mult", "curves=%u,us=%u",
		(unsigned int)ARRAY_SIZE(mt_pointops_vecs), us);
	zassert_equal(ret, 0, "pointops scalar_mult ret=%d", ret);
}

/* ---- point_add：R = G + P ---- */
ZTEST(pointops, point_add)
{
	uint8_t rx[66], ry[66];
	int ret = 0;
	uint32_t t0 = k_cycle_get_32();

	for (size_t i = 0; i < ARRAY_SIZE(mt_pointops_vecs) && ret == 0; i++) {
		const struct mt_pointops_vector *v = &mt_pointops_vecs[i];

		ret = ls_otbn_pointops_point_add(v->curve,
						 v->gx, v->gy,
						 v->px, v->py,
						 rx, ry);
		if (ret == 0) {
			mt_assert_mem_eq(rx, v->pa_x, v->n,
					 "%s point_add x", v->name);
			mt_assert_mem_eq(ry, v->pa_y, v->n,
					 "%s point_add y", v->name);
		}
	}

	uint32_t us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("pointops_point_add", "curves=%u,us=%u",
		(unsigned int)ARRAY_SIZE(mt_pointops_vecs), us);
	zassert_equal(ret, 0, "pointops point_add ret=%d", ret);
}

/* ---- 高强度组合：3 曲线 x 3 运算连续调用，验证 session 重入无状态污染 ---- */
ZTEST(pointops, stress_reentry)
{
	uint8_t rx[66], ry[66];
	int ret = 0;
	uint32_t t0 = k_cycle_get_32();

	for (int round = 0; round < 3 && ret == 0; round++) {
		for (size_t i = 0; i < ARRAY_SIZE(mt_pointops_vecs) && ret == 0; i++) {
			const struct mt_pointops_vector *v = &mt_pointops_vecs[i];

			ret = ls_otbn_pointops_base_mult(v->curve, v->k, rx, ry);
			if (ret == 0) {
				ret = ls_otbn_pointops_scalar_mult(v->curve, v->k,
								   v->px, v->py, rx, ry);
			}
			if (ret == 0) {
				ret = ls_otbn_pointops_point_add(v->curve,
								 v->gx, v->gy,
								 v->px, v->py,
								 rx, ry);
			}
		}
	}

	uint32_t us = (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - t0);
	ZT_INFO("pointops_stress_reentry", "rounds=3,curves=%u,us=%u",
		(unsigned int)ARRAY_SIZE(mt_pointops_vecs), us);
	zassert_equal(ret, 0, "pointops stress_reentry ret=%d", ret);
}

#endif /* MT_HAVE_POINTOPS */
