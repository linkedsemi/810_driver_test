/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN ECDSA delegation -- private-key slot signing (case_slot).
 *
 * This is a separate translation unit from the default matrix, selected by
 * -DOTBN_DELEG_SLOT=1 (both cores), so the slot suite owns its own item set
 * and link order.
 *
 * CPU2 (controller, delegation CLIENT): mbedtls_ls_otbn_ecdsa_sign_slot()
 * sends only a slot id + digest; cpu1 does the signing.
 *
 * CPU1 (target, delegation SERVER): the slots are registered in test_main().
 * The shared items call otbn_deleg_mbedtls_ecdsa_sign_slot() directly with a local
 * request, which exercises the same handler a delegated request reaches.
 *
 * Both cores verify the resulting signature with the known public key. The
 * client's software verify is the independent oracle. On the server,
 * mbedtls_ecdsa_verify() for P256/P384 is the local OTBN alt (common-mode with
 * the sign); SM2 uses the independent software equation on both cores. The
 * server never re-converts the private scalar in the test body -- the handler
 * owns that.
 *
 * Cache contract: a direct local handler call must present cache-line aligned
 * input buffers that were flushed first, exactly like the client stub. An
 * unaligned stack buffer makes cache_invd discard the dirty rest of its line
 * (observed as a kernel oops).
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"

#include "mbedtls_otbn_ecdsa_mbox.h"
#include "mbedtls_otbn_ecdsa_slot_mbox.h"
#include "otbn_delegation.h"
#include "ztest_glue.h"
#include "otbn_delegation_slot_vectors.h"

#define SLOT_P256A   0u
#define SLOT_P384    1u
#define SLOT_SM2     2u
#define SLOT_P256B   3u
#define SLOT_SCRATCH 4u /* server-only registration/race scratch */
#define SLOT_EMPTY   7u /* never registered by any item */
/* The table size is a serving-core setting; the client only needs an id that
 * is guaranteed to be out of range, so it does not depend on the server's
 * CONFIG_LS_OTBN_ECDSA_SLOT_MBOX_COUNT. */
#define SLOT_OOR_CLIENT 0xFFFFFFFFu
#define SLOT_OOR_SERVER CONFIG_LS_OTBN_ECDSA_SLOT_MBOX_COUNT

#define MAX_SIG_LEN 48

struct concurrent_ctx {
	volatile int rc;
};

static size_t slot_curve_size(mbedtls_ecp_group_id gid)
{
	if (gid == MBEDTLS_ECP_DP_SECP384R1) {
		return 48;
	}
	if (gid == MBEDTLS_ECP_DP_SECP256R1 || gid == MBEDTLS_ECP_DP_SM2) {
		return 32;
	}
	return 0;
}

struct fixed_rng_ctx {
	const uint8_t *bytes;
	size_t len;
};

static int fixed_rng(void *p, unsigned char *out, size_t len)
{
	const struct fixed_rng_ctx *rng = p;

	if (len != rng->len) {
		return LS_OTBN_ERR_INVAL;
	}
	memcpy(out, rng->bytes, len);
	return 0;
}

/*
 * Same return contract on both roles: 0 on success, the API's negative code
 * otherwise. r_out/s_out receive the little-endian signature on success.
 */
static int slot_sign(uint32_t slot, mbedtls_ecp_group_id gid,
		     const uint8_t *digest, size_t dlen,
		     uint8_t *r_out, uint8_t *s_out)
{
	size_t n_size = slot_curve_size(gid);

	if (n_size == 0) {
		return LS_OTBN_ERR_INVAL;
	}

#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	mbedtls_ecp_group grp;
	mbedtls_mpi r, s;
	int err;

	mbedtls_ecp_group_init(&grp);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	err = mbedtls_ecp_group_load(&grp, gid);
	if (err == 0) {
		err = mbedtls_ls_otbn_ecdsa_sign_slot(&grp, &r, &s, slot,
						      digest, dlen, NULL, NULL);
	}
	if (err == 0) {
		err = mbedtls_mpi_write_binary_le(&r, r_out, n_size);
	}
	if (err == 0) {
		err = mbedtls_mpi_write_binary_le(&s, s_out, n_size);
	}

	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	mbedtls_ecp_group_free(&grp);
	return err;
#elif defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER)
	/* Mirror the client's truncation, then follow the bus cache contract. */
	size_t send_len = dlen > n_size ? n_size : dlen;
	__attribute__((aligned(32))) uint32_t slot_id = slot;
	__attribute__((aligned(32))) uint8_t digest_buf[MAX_SIG_LEN] = {0};
	struct otbn_delegate_params req = {0};
	struct otbn_delegate_params reply = {0};

	memcpy(digest_buf, digest, send_len);
	otbn_deleg_cache_flush(&slot_id, sizeof(slot_id));
	otbn_deleg_cache_flush(digest_buf, sizeof(digest_buf));

	req.op = OTBN_DELEG_OP_MBEDTLS_ECDSA_SIGN_SLOT;
	req.arg[0] = (uint32_t)gid;
	req.arg[1] = (uint32_t)send_len;
	req.data[0] = (uint8_t *)&slot_id;
	req.data[1] = digest_buf;
	req.data[2] = r_out;
	req.data[3] = s_out;
	return otbn_deleg_mbedtls_ecdsa_sign_slot(&req, &reply);
#else
	ARG_UNUSED(slot);
	ARG_UNUSED(digest);
	ARG_UNUSED(dlen);
	ARG_UNUSED(r_out);
	ARG_UNUSED(s_out);
	return LS_OTBN_ERR_NOSUPP;
#endif
}

/*
 * Independent SM2 verification (GB/T 32918.2): the OTBN SM2 firmware signs
 * with the SM2 equation, while mbedtls_ecdsa_verify() is plain ECDSA over the
 * curve (no SM2 special-case in ecdsa.c). Using the ECDSA verify as the SM2
 * oracle is therefore wrong -- the default matrix only "passes" SM2 because
 * its verify is delegated to the same OTBN firmware (common-mode).
 *
 *   t = (r + s) mod n
 *   (x1, y1) = s*G + t*Q
 *   v = (e + x1) mod n ; valid iff v == r
 *
 * This path uses only software ECP on both cores, so it is a real oracle.
 */
static void sm2_verify_equation(const mbedtls_ecp_group *grp,
				const mbedtls_ecp_point *Q,
				const uint8_t *digest, size_t dlen,
				const mbedtls_mpi *r, const mbedtls_mpi *s)
{
	mbedtls_mpi e, t, v;
	mbedtls_ecp_point R;
	int err;

	mbedtls_mpi_init(&e);
	mbedtls_mpi_init(&t);
	mbedtls_mpi_init(&v);
	mbedtls_ecp_point_init(&R);

	/* r and s must be in [1, n-1] (GB/T 32918.2). */
	zassert_true(mbedtls_mpi_cmp_int(r, 0) > 0 &&
		     mbedtls_mpi_cmp_mpi(r, &grp->N) < 0,
		     "SM2 r out of range");
	zassert_true(mbedtls_mpi_cmp_int(s, 0) > 0 &&
		     mbedtls_mpi_cmp_mpi(s, &grp->N) < 0,
		     "SM2 s out of range");

	err = mbedtls_mpi_read_binary(&e, digest, dlen);
	zassert_equal(err, 0, "SM2 read e failed: %d", err);
	err = mbedtls_mpi_add_mpi(&t, r, s);
	zassert_equal(err, 0, "SM2 add r+s failed: %d", err);
	err = mbedtls_mpi_mod_mpi(&t, &t, &grp->N);
	zassert_equal(err, 0, "SM2 mod n failed: %d", err);
	zassert_true(mbedtls_mpi_cmp_int(&t, 0) != 0, "SM2 t == 0");

	err = mbedtls_ecp_muladd((mbedtls_ecp_group *)grp, &R, s, &grp->G, &t, Q);
	zassert_equal(err, 0, "SM2 muladd failed: %d", err);
	zassert_false(mbedtls_ecp_is_zero(&R), "SM2 R is the point at infinity");

	err = mbedtls_mpi_add_mpi(&v, &e, &R.MBEDTLS_PRIVATE(X));
	zassert_equal(err, 0, "SM2 add e+x1 failed: %d", err);
	err = mbedtls_mpi_mod_mpi(&v, &v, &grp->N);
	zassert_equal(err, 0, "SM2 mod n (v) failed: %d", err);

	zassert_equal(mbedtls_mpi_cmp_mpi(&v, r), 0,
		      "SM2 verify equation failed (v != r)");

	mbedtls_mpi_free(&e);
	mbedtls_mpi_free(&t);
	mbedtls_mpi_free(&v);
	mbedtls_ecp_point_free(&R);
}

static void slot_verify(mbedtls_ecp_group_id gid, const char *qx, const char *qy,
			const uint8_t *digest, size_t dlen,
			const uint8_t *r_buf, const uint8_t *s_buf, size_t n_size,
			bool expect_ok)
{
	mbedtls_ecp_group grp;
	mbedtls_ecp_point Q;
	mbedtls_mpi r, s;
	int err;

	mbedtls_ecp_group_init(&grp);
	mbedtls_ecp_point_init(&Q);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	err = mbedtls_ecp_group_load(&grp, gid);
	zassert_equal(err, 0, "load group 0x%x failed: %d", (unsigned int)gid, err);
	err = mbedtls_ecp_point_read_string(&Q, 16, qx, qy);
	zassert_equal(err, 0, "read public key failed: %d", err);
	err = mbedtls_mpi_read_binary_le(&r, r_buf, n_size);
	zassert_equal(err, 0, "read r failed: %d", err);
	err = mbedtls_mpi_read_binary_le(&s, s_buf, n_size);
	zassert_equal(err, 0, "read s failed: %d", err);

	if (gid == MBEDTLS_ECP_DP_SM2 && expect_ok) {
		sm2_verify_equation(&grp, &Q, digest, dlen, &r, &s);
	} else {
		err = mbedtls_ecdsa_verify(&grp, digest, dlen, &Q, &r, &s);
		if (expect_ok) {
			zassert_equal(err, 0, "signature verification failed: %d", err);
		} else {
			zassert_equal(err, MBEDTLS_ERR_ECP_VERIFY_FAILED,
				      "wrong-key verification returned %d", err);
		}
	}

	mbedtls_ecp_group_free(&grp);
	mbedtls_ecp_point_free(&Q);
	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
}

static void assert_rs_in_range(mbedtls_ecp_group_id gid,
			       const uint8_t *r_buf, const uint8_t *s_buf,
			       size_t n_size)
{
	mbedtls_ecp_group grp;
	mbedtls_mpi r, s;
	int err;

	mbedtls_ecp_group_init(&grp);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	err = mbedtls_ecp_group_load(&grp, gid);
	zassert_equal(err, 0, "load group failed: %d", err);
	err = mbedtls_mpi_read_binary_le(&r, r_buf, n_size);
	zassert_equal(err, 0, "read r failed: %d", err);
	err = mbedtls_mpi_read_binary_le(&s, s_buf, n_size);
	zassert_equal(err, 0, "read s failed: %d", err);

	zassert_true(mbedtls_mpi_cmp_int(&r, 0) > 0 &&
		     mbedtls_mpi_cmp_mpi(&r, &grp.N) < 0, "r out of range");
	zassert_true(mbedtls_mpi_cmp_int(&s, 0) > 0 &&
		     mbedtls_mpi_cmp_mpi(&s, &grp.N) < 0, "s out of range");

	mbedtls_ecp_group_free(&grp);
	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
}

/* A client can reach this item before the server's test_main() has published
 * the slots, so retry the positive item on the sole "not registered yet" code. */
ZTEST(otbn_slot, s01_sign_verify)
{
	__attribute__((aligned(32))) uint8_t r[MAX_SIG_LEN] = {0};
	__attribute__((aligned(32))) uint8_t s[MAX_SIG_LEN] = {0};
	int err = -EIO;
	int i;

#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	for (i = 0; i < 20; i++) {
		err = slot_sign(SLOT_P256A, MBEDTLS_ECP_DP_SECP256R1,
				otbn_slot_hash, sizeof(otbn_slot_hash), r, s);
		if (err != LS_OTBN_ERR_NOENT) {
			break;
		}
		k_msleep(50);
	}
#else
	err = slot_sign(SLOT_P256A, MBEDTLS_ECP_DP_SECP256R1,
			otbn_slot_hash, sizeof(otbn_slot_hash), r, s);
#endif

	zassert_equal(err, 0, "slot sign failed: %d", err);
	slot_verify(MBEDTLS_ECP_DP_SECP256R1, OTBN_SLOT_QX, OTBN_SLOT_QY,
		    otbn_slot_hash, sizeof(otbn_slot_hash), r, s, 32, true);
	ZT_INFO("sign_verify", "slot=%u,rc=%d", SLOT_P256A, err);
}

ZTEST(otbn_slot, s02_unregistered)
{
	__attribute__((aligned(32))) uint8_t r[MAX_SIG_LEN] = {0};
	__attribute__((aligned(32))) uint8_t s[MAX_SIG_LEN] = {0};
	int err;

	err = slot_sign(SLOT_EMPTY, MBEDTLS_ECP_DP_SECP256R1,
			otbn_slot_hash, sizeof(otbn_slot_hash), r, s);
	zassert_equal(err, LS_OTBN_ERR_NOENT, "unregistered slot returned %d", err);
	ZT_INFO("unregistered", "rc=%d", err);
}

ZTEST(otbn_slot, s03_curve_mismatch)
{
	__attribute__((aligned(32))) uint8_t r[MAX_SIG_LEN] = {0};
	__attribute__((aligned(32))) uint8_t s[MAX_SIG_LEN] = {0};
	int err;

	/* Slot 0 holds a P256 key; requesting P384 must be refused before the
	 * scalar is used. Keep the digest 32 bytes so the only mismatch is the
	 * curve. */
	err = slot_sign(SLOT_P256A, MBEDTLS_ECP_DP_SECP384R1,
			otbn_slot_hash, sizeof(otbn_slot_hash), r, s);
	zassert_equal(err, LS_OTBN_ERR_CURVE, "curve mismatch returned %d", err);
	ZT_INFO("curve_mismatch", "rc=%d", err);
}

ZTEST(otbn_slot, s04_register_validation)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER)
	static const uint8_t zero_key[32] = {0};
	__attribute__((aligned(32))) uint8_t r[MAX_SIG_LEN] = {0};
	__attribute__((aligned(32))) uint8_t s[MAX_SIG_LEN] = {0};
	int status;
	int err;

	/* 0 and n are outside the valid scalar range [1, n-1]. The plain form
	 * reports the crypto-layer marker; the _ex form also carries mbedtls'
	 * exact value in status. */
	err = ls_otbn_ecdsa_key_slot_register_ex(SLOT_SCRATCH,
						 MBEDTLS_ECP_DP_SECP256R1,
						 zero_key, &status);
	zassert_equal(err, LS_OTBN_ERR_MBEDTLS, "zero private key returned %d", err);
	zassert_equal(status, MBEDTLS_ERR_ECP_INVALID_KEY,
		      "zero key detail %d", status);

	err = ls_otbn_ecdsa_key_slot_register(SLOT_SCRATCH, MBEDTLS_ECP_DP_SECP256R1,
					      p256_n_le);
	zassert_equal(err, LS_OTBN_ERR_MBEDTLS, "d=n returned %d", err);

	/* n-1 is the largest valid scalar and must be accepted. */
	err = ls_otbn_ecdsa_key_slot_register(SLOT_SCRATCH, MBEDTLS_ECP_DP_SECP256R1,
					      p256_n_minus1_le);
	zassert_equal(err, 0, "d=n-1 returned %d", err);
	err = ls_otbn_ecdsa_key_slot_unregister(SLOT_SCRATCH);
	zassert_equal(err, 0, "unregister n-1 slot returned %d", err);

	/* Out-of-whitelist curve and out-of-range slot id. */
	err = ls_otbn_ecdsa_key_slot_register(SLOT_SCRATCH, MBEDTLS_ECP_DP_SECP521R1,
					      otbn_slot_key_le);
	zassert_equal(err, LS_OTBN_ERR_NOSUPP, "unsupported curve returned %d", err);

	err = ls_otbn_ecdsa_key_slot_register(SLOT_OOR_SERVER, MBEDTLS_ECP_DP_SECP256R1,
					      otbn_slot_key_le);
	zassert_equal(err, LS_OTBN_ERR_RANGE, "out-of-range slot returned %d", err);

	err = ls_otbn_ecdsa_key_slot_register(SLOT_SCRATCH, MBEDTLS_ECP_DP_SECP256R1,
					      NULL);
	zassert_equal(err, LS_OTBN_ERR_INVAL, "NULL key returned %d", err);

	err = ls_otbn_ecdsa_key_slot_unregister(SLOT_SCRATCH);
	zassert_equal(err, LS_OTBN_ERR_NOENT, "unregister empty slot returned %d", err);

	/* Register, sign, unregister, sign again: the slot disappears. */
	err = ls_otbn_ecdsa_key_slot_register(SLOT_SCRATCH, MBEDTLS_ECP_DP_SECP256R1,
					      otbn_slot_key_le);
	zassert_equal(err, 0, "register scratch returned %d", err);
	err = slot_sign(SLOT_SCRATCH, MBEDTLS_ECP_DP_SECP256R1,
			otbn_slot_hash, sizeof(otbn_slot_hash), r, s);
	zassert_equal(err, 0, "sign registered scratch returned %d", err);

	err = ls_otbn_ecdsa_key_slot_unregister(SLOT_SCRATCH);
	zassert_equal(err, 0, "unregister scratch returned %d", err);
	err = slot_sign(SLOT_SCRATCH, MBEDTLS_ECP_DP_SECP256R1,
			otbn_slot_hash, sizeof(otbn_slot_hash), r, s);
	zassert_equal(err, LS_OTBN_ERR_NOENT, "sign after unregister returned %d", err);

	ZT_INFO("register_validation", "ok=1");
#else
	ztest_test_skip();
#endif
}

ZTEST(otbn_slot, s05_curves)
{
	__attribute__((aligned(32))) uint8_t r[MAX_SIG_LEN] = {0};
	__attribute__((aligned(32))) uint8_t s[MAX_SIG_LEN] = {0};
	int err;

	err = slot_sign(SLOT_P384, MBEDTLS_ECP_DP_SECP384R1,
			otbn_slot_hash, sizeof(otbn_slot_hash), r, s);
	zassert_equal(err, 0, "P384 32B sign failed: %d", err);
	slot_verify(MBEDTLS_ECP_DP_SECP384R1, P384_QX, P384_QY,
		    otbn_slot_hash, sizeof(otbn_slot_hash), r, s, 48, true);
	ZT_INFO("curves", "p384_32=ok");

	/* The natural P384 digest is 48 bytes. Keep it as a separate probe so a
	 * failure here is distinguishable from the 32-byte case above. */
	err = slot_sign(SLOT_P384, MBEDTLS_ECP_DP_SECP384R1,
			p384_hash, sizeof(p384_hash), r, s);
	zassert_equal(err, 0, "P384 48B sign failed: %d", err);
	slot_verify(MBEDTLS_ECP_DP_SECP384R1, P384_QX, P384_QY,
		    p384_hash, sizeof(p384_hash), r, s, 48, true);
	ZT_INFO("curves", "p384_48=ok");

	err = slot_sign(SLOT_SM2, MBEDTLS_ECP_DP_SM2,
			otbn_slot_hash, sizeof(otbn_slot_hash), r, s);
	zassert_equal(err, 0, "SM2 slot sign failed: %d", err);
	slot_verify(MBEDTLS_ECP_DP_SM2, SM2_QX, SM2_QY,
		    otbn_slot_hash, sizeof(otbn_slot_hash), r, s, 32, true);
	ZT_INFO("curves", "sm2=ok");
}

ZTEST(otbn_slot, s06_multi_slot)
{
	__attribute__((aligned(32))) uint8_t r_a[MAX_SIG_LEN] = {0};
	__attribute__((aligned(32))) uint8_t s_a[MAX_SIG_LEN] = {0};
	__attribute__((aligned(32))) uint8_t r_b[MAX_SIG_LEN] = {0};
	__attribute__((aligned(32))) uint8_t s_b[MAX_SIG_LEN] = {0};
	int err;

	/* Two P256 keys live in different slots: each signature must verify
	 * with its own public key, and not with the other one. */
	err = slot_sign(SLOT_P256A, MBEDTLS_ECP_DP_SECP256R1,
			otbn_slot_hash, sizeof(otbn_slot_hash), r_a, s_a);
	zassert_equal(err, 0, "slot A sign failed: %d", err);
	slot_verify(MBEDTLS_ECP_DP_SECP256R1, OTBN_SLOT_QX, OTBN_SLOT_QY,
		    otbn_slot_hash, sizeof(otbn_slot_hash), r_a, s_a, 32, true);

	err = slot_sign(SLOT_P256B, MBEDTLS_ECP_DP_SECP256R1,
			otbn_slot_hash, sizeof(otbn_slot_hash), r_b, s_b);
	zassert_equal(err, 0, "slot B sign failed: %d", err);
	slot_verify(MBEDTLS_ECP_DP_SECP256R1, P256B_QX, P256B_QY,
		    otbn_slot_hash, sizeof(otbn_slot_hash), r_b, s_b, 32, true);

	/* Slot A's signature must not verify under key B. */
	slot_verify(MBEDTLS_ECP_DP_SECP256R1, P256B_QX, P256B_QY,
		    otbn_slot_hash, sizeof(otbn_slot_hash), r_a, s_a, 32, false);

	ZT_INFO("multi_slot", "a=ok,b=ok,cross=rejected");
}

ZTEST(otbn_slot, s07_hash_lengths)
{
	__attribute__((aligned(32))) uint8_t r[MAX_SIG_LEN] = {0};
	__attribute__((aligned(32))) uint8_t s[MAX_SIG_LEN] = {0};
	uint8_t short_hash[32] = {0};
	uint8_t long_hash[64] = {0};
	int err;

	memcpy(short_hash, otbn_slot_hash, 31);
	memcpy(long_hash, otbn_slot_hash, sizeof(otbn_slot_hash));
	memcpy(long_hash + sizeof(otbn_slot_hash), otbn_slot_hash,
	       sizeof(otbn_slot_hash));

	/* blen < n: sign exactly what was given. */
	err = slot_sign(SLOT_P256A, MBEDTLS_ECP_DP_SECP256R1,
			short_hash, 31, r, s);
	zassert_equal(err, 0, "31-byte hash sign failed: %d", err);
	slot_verify(MBEDTLS_ECP_DP_SECP256R1, OTBN_SLOT_QX, OTBN_SLOT_QY,
		    short_hash, 31, r, s, 32, true);

	/* blen > n: the API truncates to the curve size (32). */
	err = slot_sign(SLOT_P256A, MBEDTLS_ECP_DP_SECP256R1,
			long_hash, sizeof(long_hash), r, s);
	zassert_equal(err, 0, "64-byte hash sign failed: %d", err);
	slot_verify(MBEDTLS_ECP_DP_SECP256R1, OTBN_SLOT_QX, OTBN_SLOT_QY,
		    long_hash, 32, r, s, 32, true);

	/* blen == 0 signs an empty message; only the range is assertable. */
	err = slot_sign(SLOT_P256A, MBEDTLS_ECP_DP_SECP256R1,
			otbn_slot_hash, 0, r, s);
	zassert_equal(err, 0, "zero-length hash sign failed: %d", err);
	assert_rs_in_range(MBEDTLS_ECP_DP_SECP256R1, r, s, 32);

	ZT_INFO("hash_lengths", "lt=ok,gt=ok,zero=ok");
}

ZTEST(otbn_slot, s08_client_args)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	mbedtls_ecp_group grp;
	mbedtls_mpi r, s;
	int status;
	int err;

	mbedtls_ecp_group_init(&grp);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	err = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
	zassert_equal(err, 0, "load P256 failed: %d", err);

	/* NULL arguments are rejected before any round trip. */
	err = mbedtls_ls_otbn_ecdsa_sign_slot(NULL, &r, &s, SLOT_P256A,
					      otbn_slot_hash, 32, NULL, NULL);
	zassert_equal(err, LS_OTBN_ERR_INVAL, "NULL grp returned %d", err);
	err = mbedtls_ls_otbn_ecdsa_sign_slot(&grp, NULL, &s, SLOT_P256A,
					      otbn_slot_hash, 32, NULL, NULL);
	zassert_equal(err, LS_OTBN_ERR_INVAL, "NULL r returned %d", err);
	err = mbedtls_ls_otbn_ecdsa_sign_slot(&grp, &r, NULL, SLOT_P256A,
					      otbn_slot_hash, 32, NULL, NULL);
	zassert_equal(err, LS_OTBN_ERR_INVAL, "NULL s returned %d", err);
	err = mbedtls_ls_otbn_ecdsa_sign_slot(&grp, &r, &s, SLOT_P256A,
					      NULL, 32, NULL, NULL);
	zassert_equal(err, LS_OTBN_ERR_INVAL, "NULL digest returned %d", err);

	/* The _ex form returns the same code and fills the backend detail. */
	err = mbedtls_ls_otbn_ecdsa_sign_slot_ex(NULL, &r, &s, SLOT_P256A,
						 otbn_slot_hash, 32, NULL, NULL,
						 &status);
	zassert_equal(err, LS_OTBN_ERR_INVAL, "NULL grp (ex) returned %d", err);
	zassert_equal(status, 0, "ex status %d", status);

	/* Out-of-whitelist curve fails locally with LS_OTBN_ERR_NOSUPP. */
	{
		mbedtls_ecp_group p521;
		mbedtls_ecp_group_init(&p521);
		err = mbedtls_ecp_group_load(&p521, MBEDTLS_ECP_DP_SECP521R1);
		zassert_equal(err, 0, "load P521 failed: %d", err);
		err = mbedtls_ls_otbn_ecdsa_sign_slot(&p521, &r, &s, SLOT_P256A,
						      otbn_slot_hash, 32, NULL, NULL);
		zassert_equal(err, LS_OTBN_ERR_NOSUPP, "unsupported curve returned %d", err);
		mbedtls_ecp_group_free(&p521);
	}

	/* A slot id past the table is rejected by the peer with LS_OTBN_ERR_RANGE; an id
	 * inside the table that was never registered gives LS_OTBN_ERR_NOENT instead. */
	err = mbedtls_ls_otbn_ecdsa_sign_slot(&grp, &r, &s, SLOT_OOR_CLIENT,
					      otbn_slot_hash, 32, NULL, NULL);
	zassert_equal(err, LS_OTBN_ERR_RANGE, "out-of-range slot returned %d", err);

	mbedtls_ecp_group_free(&grp);
	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	ZT_INFO("client_args", "ok=1");
#else
	ztest_test_skip();
#endif
}

ZTEST(otbn_slot, s09_server_args)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER)
	__attribute__((aligned(32))) uint32_t slot_id = SLOT_P256A;
	__attribute__((aligned(32))) uint8_t digest[MAX_SIG_LEN * 2] = {0};
	__attribute__((aligned(32))) uint8_t r[MAX_SIG_LEN] = {0};
	__attribute__((aligned(32))) uint8_t s[MAX_SIG_LEN] = {0};
	struct otbn_delegate_params req = {0};
	struct otbn_delegate_params reply = {0};
	int err;

	memcpy(digest, otbn_slot_hash, sizeof(otbn_slot_hash));
	req.op = OTBN_DELEG_OP_MBEDTLS_ECDSA_SIGN_SLOT;
	req.arg[0] = MBEDTLS_ECP_DP_SECP256R1;
	req.arg[1] = sizeof(otbn_slot_hash);
	req.data[0] = (uint8_t *)&slot_id;
	req.data[1] = digest;
	req.data[2] = r;
	req.data[3] = s;
	otbn_deleg_cache_flush(&slot_id, sizeof(slot_id));
	otbn_deleg_cache_flush(digest, sizeof(digest));

	/* Every shared buffer is mandatory; a missing one is a malformed bus
	 * request, not an application argument error (LS_OTBN_ERR_INVAL is the API's). */
	req.data[0] = NULL;
	zassert_equal(otbn_deleg_mbedtls_ecdsa_sign_slot(&req, &reply), LS_OTBN_ERR_PROTO,
		      "NULL data[0] accepted");
	req.data[0] = (uint8_t *)&slot_id;
	req.data[1] = NULL;
	zassert_equal(otbn_deleg_mbedtls_ecdsa_sign_slot(&req, &reply), LS_OTBN_ERR_PROTO,
		      "NULL data[1] accepted");
	req.data[1] = digest;
	req.data[2] = NULL;
	zassert_equal(otbn_deleg_mbedtls_ecdsa_sign_slot(&req, &reply), LS_OTBN_ERR_PROTO,
		      "NULL data[2] accepted");
	req.data[2] = r;
	req.data[3] = NULL;
	zassert_equal(otbn_deleg_mbedtls_ecdsa_sign_slot(&req, &reply), LS_OTBN_ERR_PROTO,
		      "NULL data[3] accepted");
	req.data[3] = s;

	/* The client truncates, so a longer digest length here is a protocol
	 * violation and must be rejected before any key access. */
	req.arg[1] = 64;
	zassert_equal(otbn_deleg_mbedtls_ecdsa_sign_slot(&req, &reply), LS_OTBN_ERR_MSGSIZE,
		      "oversized digest length accepted");
	req.arg[1] = sizeof(otbn_slot_hash);

	/* Curve must match the slot's registration. */
	req.arg[0] = MBEDTLS_ECP_DP_SECP384R1;
	zassert_equal(otbn_deleg_mbedtls_ecdsa_sign_slot(&req, &reply), LS_OTBN_ERR_CURVE,
		      "P384 request on a P256 slot accepted");
	req.arg[0] = MBEDTLS_ECP_DP_SECP521R1;
	zassert_equal(otbn_deleg_mbedtls_ecdsa_sign_slot(&req, &reply), LS_OTBN_ERR_CURVE,
		      "unsupported curve accepted");
	req.arg[0] = MBEDTLS_ECP_DP_SECP256R1;

	/* Sanity: the same request with valid scalars succeeds. */
	err = otbn_deleg_mbedtls_ecdsa_sign_slot(&req, &reply);
	zassert_equal(err, 0, "valid local request returned %d", err);

	ZT_INFO("server_args", "ok=1");
#else
	ztest_test_skip();
#endif
}

ZTEST(otbn_slot, s10_soak)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	__attribute__((aligned(32))) uint8_t r[MAX_SIG_LEN] = {0};
	__attribute__((aligned(32))) uint8_t s[MAX_SIG_LEN] = {0};
	int err = 0;
	int i;

	for (i = 0; i < 100; i++) {
		err = slot_sign(SLOT_P256A, MBEDTLS_ECP_DP_SECP256R1,
				otbn_slot_hash, sizeof(otbn_slot_hash), r, s);
		if (err != 0) {
			break;
		}
		/* Verify every signature: if one is bad, the INFO line just
		 * before the assertion identifies the failing iteration. */
		ZT_INFO("soak_iter", "i=%d", i);
		slot_verify(MBEDTLS_ECP_DP_SECP256R1, OTBN_SLOT_QX, OTBN_SLOT_QY,
			    otbn_slot_hash, sizeof(otbn_slot_hash), r, s, 32, true);
	}
	zassert_equal(err, 0, "soak sign failed at iteration %d: %d", i, err);
	ZT_INFO("soak", "iterations=%d,rc=0", i);
#else
	ztest_test_skip();
#endif
}

#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
static void concurrent_worker(void *p1, void *p2, void *p3)
{
	struct concurrent_ctx *ctx = p1;
	__attribute__((aligned(32))) uint8_t r[MAX_SIG_LEN];
	__attribute__((aligned(32))) uint8_t s[MAX_SIG_LEN];
	int i;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (i = 0; i < 10; i++) {
		int err = slot_sign(SLOT_P256A, MBEDTLS_ECP_DP_SECP256R1,
				    otbn_slot_hash, sizeof(otbn_slot_hash), r, s);
		if (err != 0) {
			ctx->rc = err;
			return;
		}
	}
	ctx->rc = 0;
}

K_THREAD_STACK_DEFINE(conc_stack_a, 8192);
K_THREAD_STACK_DEFINE(conc_stack_b, 8192);
static struct k_thread conc_thread_a;
static struct k_thread conc_thread_b;
#endif

ZTEST(otbn_slot, s11_concurrent)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	struct concurrent_ctx a = { .rc = -EIO };
	struct concurrent_ctx b = { .rc = -EIO };

	k_thread_create(&conc_thread_a, conc_stack_a,
			K_THREAD_STACK_SIZEOF(conc_stack_a),
			concurrent_worker, &a, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	k_thread_create(&conc_thread_b, conc_stack_b,
			K_THREAD_STACK_SIZEOF(conc_stack_b),
			concurrent_worker, &b, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

	zassert_equal(k_thread_join(&conc_thread_a, K_FOREVER), 0,
		      "join A failed");
	zassert_equal(k_thread_join(&conc_thread_b, K_FOREVER), 0,
		      "join B failed");

	zassert_equal(a.rc, 0, "concurrent worker A returned %d", a.rc);
	zassert_equal(b.rc, 0, "concurrent worker B returned %d", b.rc);
	ZT_INFO("concurrent", "threads=2,iterations=10,ok=1");
#else
	ztest_test_skip();
#endif
}

#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER)
K_THREAD_STACK_DEFINE(race_stack, 8192);
static struct k_thread race_thread;

static void race_worker(void *p1, void *p2, void *p3)
{
	struct concurrent_ctx *ctx = p1;
	int i;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (i = 0; i < 50; i++) {
		int err = ls_otbn_ecdsa_key_slot_register(SLOT_SCRATCH,
							  MBEDTLS_ECP_DP_SECP256R1,
							  otbn_slot_key_le);

		if (err != 0) {
			ctx->rc = err;
			return;
		}
		err = ls_otbn_ecdsa_key_slot_unregister(SLOT_SCRATCH);
		if (err != 0) {
			ctx->rc = err;
			return;
		}
	}
	ctx->rc = 0;
}
#endif

ZTEST(otbn_slot, s12_server_race)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER)
	struct concurrent_ctx ctx = { .rc = -EIO };
	__attribute__((aligned(32))) uint8_t r[MAX_SIG_LEN] = {0};
	__attribute__((aligned(32))) uint8_t s[MAX_SIG_LEN] = {0};
	int err;
	int i;

	k_thread_create(&race_thread, race_stack,
			K_THREAD_STACK_SIZEOF(race_stack),
			race_worker, &ctx, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

	/* Slot 0 is never touched by the racer; every sign must still work. */
	for (i = 0; i < 20; i++) {
		err = slot_sign(SLOT_P256A, MBEDTLS_ECP_DP_SECP256R1,
				otbn_slot_hash, sizeof(otbn_slot_hash), r, s);
		zassert_equal(err, 0, "sign during registry churn failed at %d: %d",
			      i, err);
	}

	zassert_equal(k_thread_join(&race_thread, K_FOREVER), 0,
		      "join race worker failed");
	zassert_equal(ctx.rc, 0, "registry churn returned %d", ctx.rc);

	/* The racer's last action is an unregister; make the state explicit. */
	(void)ls_otbn_ecdsa_key_slot_unregister(SLOT_SCRATCH);
	ZT_INFO("server_race", "iterations=50,signs=20,ok=1");
#else
	ztest_test_skip();
#endif
}

ZTEST(otbn_slot, s13_proxy_rng)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	mbedtls_ecdsa_context ctx;
	mbedtls_mpi r1, s1, r2, s2;
	struct fixed_rng_ctx seed = { otbn_slot_key_le, 32 };
	struct fixed_rng_ctx nonce = { p256b_key_le, 32 };
	int err;

	mbedtls_ecp_group_init(&ctx.private_grp);
	mbedtls_ecp_point_init(&ctx.private_Q);
	mbedtls_mpi_init(&ctx.private_d);
	mbedtls_mpi_init(&r1);
	mbedtls_mpi_init(&s1);
	mbedtls_mpi_init(&r2);
	mbedtls_mpi_init(&s2);

	/* Client RNG supplies the keygen seed (key A) and the sign nonce
	 * (key B). The seed must reproduce key A, and the same nonce must
	 * reproduce the same signature. */
	err = mbedtls_ls_otbn_ecdsa_genkey(&ctx, MBEDTLS_ECP_DP_SECP256R1,
					   fixed_rng, &seed);
	zassert_equal(err, 0, "proxy genkey with client RNG failed: %d", err);

	err = mbedtls_ls_otbn_ecdsa_sign(&ctx.private_grp, &r1, &s1,
					 &ctx.private_d, otbn_slot_hash,
					 sizeof(otbn_slot_hash), fixed_rng, &nonce);
	zassert_equal(err, 0, "proxy sign #1 failed: %d", err);
	err = mbedtls_ls_otbn_ecdsa_sign(&ctx.private_grp, &r2, &s2,
					 &ctx.private_d, otbn_slot_hash,
					 sizeof(otbn_slot_hash), fixed_rng, &nonce);
	zassert_equal(err, 0, "proxy sign #2 failed: %d", err);

	zassert_equal(mbedtls_mpi_cmp_mpi(&r1, &r2), 0,
		      "same client nonce produced different r");
	zassert_equal(mbedtls_mpi_cmp_mpi(&s1, &s2), 0,
		      "same client nonce produced different s");

	err = mbedtls_ecdsa_verify(&ctx.private_grp, otbn_slot_hash,
				   sizeof(otbn_slot_hash), &ctx.private_Q, &r1, &s1);
	zassert_equal(err, 0, "proxy signature verify failed: %d", err);

	mbedtls_mpi_free(&r1);
	mbedtls_mpi_free(&s1);
	mbedtls_mpi_free(&r2);
	mbedtls_mpi_free(&s2);
	mbedtls_ecp_group_free(&ctx.private_grp);
	mbedtls_ecp_point_free(&ctx.private_Q);
	mbedtls_mpi_free(&ctx.private_d);

	ZT_INFO("proxy_rng", "genkey=ok,sign=deterministic");
#else
	ztest_test_skip();
#endif
}

ZTEST(otbn_slot, s14_slot_rng)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	mbedtls_ecp_group grp;
	mbedtls_ecp_point Q;
	mbedtls_mpi r1, s1, r2, s2;
	struct fixed_rng_ctx nonce = { p256b_key_le, 32 };
	int err;

	mbedtls_ecp_group_init(&grp);
	mbedtls_ecp_point_init(&Q);
	mbedtls_mpi_init(&r1);
	mbedtls_mpi_init(&s1);
	mbedtls_mpi_init(&r2);
	mbedtls_mpi_init(&s2);

	err = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
	zassert_equal(err, 0, "load P256 failed: %d", err);
	err = mbedtls_ecp_point_read_string(&Q, 16, OTBN_SLOT_QX, OTBN_SLOT_QY);
	zassert_equal(err, 0, "read Q failed: %d", err);

	err = mbedtls_ls_otbn_ecdsa_sign_slot(&grp, &r1, &s1, SLOT_P256A,
					      otbn_slot_hash,
					      sizeof(otbn_slot_hash),
					      fixed_rng, &nonce);
	zassert_equal(err, 0, "slot sign #1 with client RNG failed: %d", err);
	err = mbedtls_ls_otbn_ecdsa_sign_slot(&grp, &r2, &s2, SLOT_P256A,
					      otbn_slot_hash,
					      sizeof(otbn_slot_hash),
					      fixed_rng, &nonce);
	zassert_equal(err, 0, "slot sign #2 with client RNG failed: %d", err);

	zassert_equal(mbedtls_mpi_cmp_mpi(&r1, &r2), 0,
		      "same client nonce produced different r");
	zassert_equal(mbedtls_mpi_cmp_mpi(&s1, &s2), 0,
		      "same client nonce produced different s");

	err = mbedtls_ecdsa_verify(&grp, otbn_slot_hash, sizeof(otbn_slot_hash),
				   &Q, &r1, &s1);
	zassert_equal(err, 0, "slot signature verify failed: %d", err);

	mbedtls_mpi_free(&r1);
	mbedtls_mpi_free(&s1);
	mbedtls_mpi_free(&r2);
	mbedtls_mpi_free(&s2);
	mbedtls_ecp_point_free(&Q);
	mbedtls_ecp_group_free(&grp);

	ZT_INFO("slot_rng", "sign=deterministic");
#else
	ztest_test_skip();
#endif
}

ZTEST(otbn_slot, s15_p384_fixed_rng)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	mbedtls_ecp_group grp_ref, grp_otbn;
	mbedtls_ecp_point Q;
	mbedtls_mpi d_ref, r_ref, s_ref, r_otbn, s_otbn;
	struct fixed_rng_ctx nonce = { p384_key_le, 48 };
	int err;

	mbedtls_ecp_group_init(&grp_ref);
	mbedtls_ecp_group_init(&grp_otbn);
	mbedtls_ecp_point_init(&Q);
	mbedtls_mpi_init(&d_ref);
	mbedtls_mpi_init(&r_ref);
	mbedtls_mpi_init(&s_ref);
	mbedtls_mpi_init(&r_otbn);
	mbedtls_mpi_init(&s_otbn);

	err = mbedtls_ecp_group_load(&grp_ref, MBEDTLS_ECP_DP_SECP384R1);
	zassert_equal(err, 0, "load P384 ref failed: %d", err);
	err = mbedtls_ecp_group_load(&grp_otbn, MBEDTLS_ECP_DP_SECP384R1);
	zassert_equal(err, 0, "load P384 otbn failed: %d", err);
	err = mbedtls_mpi_read_binary_le(&d_ref, p384_key_le, sizeof(p384_key_le));
	zassert_equal(err, 0, "read P384 d failed: %d", err);
	err = mbedtls_ecp_point_read_string(&Q, 16, P384_QX, P384_QY);
	zassert_equal(err, 0, "read P384 Q failed: %d", err);

	/* Software reference with the same key, hash and nonce. */
	err = mbedtls_ecdsa_sign(&grp_ref, &r_ref, &s_ref, &d_ref,
				 p384_hash, sizeof(p384_hash), fixed_rng, &nonce);
	zassert_equal(err, 0, "software P384 sign failed: %d", err);

	/* OTBN slot sign through the new client-RNG op. */
	err = mbedtls_ls_otbn_ecdsa_sign_slot(&grp_otbn, &r_otbn, &s_otbn,
					      SLOT_P384, p384_hash,
					      sizeof(p384_hash), fixed_rng, &nonce);
	zassert_equal(err, 0, "OTBN P384 slot sign failed: %d", err);

	zassert_equal(mbedtls_mpi_cmp_mpi(&r_ref, &r_otbn), 0,
		      "P384 r differs from software reference");
	zassert_equal(mbedtls_mpi_cmp_mpi(&s_ref, &s_otbn), 0,
		      "P384 s differs from software reference");

	err = mbedtls_ecdsa_verify(&grp_otbn, p384_hash, sizeof(p384_hash),
				   &Q, &r_otbn, &s_otbn);
	zassert_equal(err, 0, "P384 OTBN signature verify failed: %d", err);

	mbedtls_mpi_free(&d_ref);
	mbedtls_mpi_free(&r_ref);
	mbedtls_mpi_free(&s_ref);
	mbedtls_mpi_free(&r_otbn);
	mbedtls_mpi_free(&s_otbn);
	mbedtls_ecp_point_free(&Q);
	mbedtls_ecp_group_free(&grp_ref);
	mbedtls_ecp_group_free(&grp_otbn);

	ZT_INFO("p384_fixed_rng", "otbn=software");
#else
	ztest_test_skip();
#endif
}

ZTEST(otbn_slot, s16_p384_server_rng)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	mbedtls_ecp_group grp;
	mbedtls_ecp_point Q;
	mbedtls_mpi r, s;
	int err;
	int fails = 0;
	int i;

	mbedtls_ecp_group_init(&grp);
	mbedtls_ecp_point_init(&Q);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	err = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP384R1);
	zassert_equal(err, 0, "load P384 failed: %d", err);
	err = mbedtls_ecp_point_read_string(&Q, 16, P384_QX, P384_QY);
	zassert_equal(err, 0, "read P384 Q failed: %d", err);

	/* Server RNG, client-only: no concurrent local sign on the serving core. */
	for (i = 0; i < 20; i++) {
		err = mbedtls_ls_otbn_ecdsa_sign_slot(&grp, &r, &s, SLOT_P384,
						      p384_hash,
						      sizeof(p384_hash),
						      NULL, NULL);
		if (err != 0) {
			fails++;
			ZT_INFO("p384_srv_rng_sign", "i=%d,rc=%d", i, err);
			continue;
		}
		err = mbedtls_ecdsa_verify(&grp, p384_hash, sizeof(p384_hash),
					   &Q, &r, &s);
		if (err != 0) {
			fails++;
			ZT_INFO("p384_srv_rng_verify", "i=%d,rc=%d", i, err);
		}
	}

	zassert_equal(fails, 0, "P384 server-RNG failures %d/20", fails);

	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	mbedtls_ecp_point_free(&Q);
	mbedtls_ecp_group_free(&grp);

	ZT_INFO("p384_srv_rng", "iterations=20,fails=%d", fails);
#else
	ztest_test_skip();
#endif
}

ZTEST(otbn_slot, s17_p384_32b_server_rng)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	mbedtls_ecp_group grp;
	mbedtls_ecp_point Q;
	mbedtls_mpi r, s;
	int err;
	int fails = 0;
	int i;

	mbedtls_ecp_group_init(&grp);
	mbedtls_ecp_point_init(&Q);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);

	err = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP384R1);
	zassert_equal(err, 0, "load P384 failed: %d", err);
	err = mbedtls_ecp_point_read_string(&Q, 16, P384_QX, P384_QY);
	zassert_equal(err, 0, "read P384 Q failed: %d", err);

	/* Same as s16 but a 32-byte digest (e is 256 bits). The only variable
	 * versus s16 is the digest length, so this isolates the e-width path. */
	for (i = 0; i < 20; i++) {
		err = mbedtls_ls_otbn_ecdsa_sign_slot(&grp, &r, &s, SLOT_P384,
						      otbn_slot_hash,
						      sizeof(otbn_slot_hash),
						      NULL, NULL);
		if (err != 0) {
			fails++;
			ZT_INFO("p384_32b_srv_rng_sign", "i=%d,rc=%d", i, err);
			continue;
		}
		err = mbedtls_ecdsa_verify(&grp, otbn_slot_hash,
					   sizeof(otbn_slot_hash), &Q, &r, &s);
		if (err != 0) {
			fails++;
			ZT_INFO("p384_32b_srv_rng_verify", "i=%d,rc=%d", i, err);
		}
	}

	zassert_equal(fails, 0, "P384 32B server-RNG failures %d/20", fails);

	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	mbedtls_ecp_point_free(&Q);
	mbedtls_ecp_group_free(&grp);

	ZT_INFO("p384_32b_srv_rng", "iterations=20,fails=%d", fails);
#else
	ztest_test_skip();
#endif
}

/*
 * The _ex form must return the same driver code as the plain form and report
 * the backend detail only when the failure came from the crypto layer. A
 * driver cause leaves *status at 0; a NULL status pointer must be harmless.
 */
ZTEST(otbn_slot, s18_sign_slot_ex_status)
{
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	mbedtls_ecp_group grp;
	mbedtls_mpi r, s;
	int status;
	int err;

	mbedtls_ecp_group_init(&grp);
	mbedtls_mpi_init(&r);
	mbedtls_mpi_init(&s);
	err = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
	zassert_equal(err, 0, "load P256 failed: %d", err);

	/* NULL argument: driver code, no backend detail. */
	status = 0x5A5A;
	err = mbedtls_ls_otbn_ecdsa_sign_slot_ex(NULL, &r, &s, SLOT_P256A,
						 otbn_slot_hash, sizeof(otbn_slot_hash),
						 NULL, NULL, &status);
	zassert_equal(err, LS_OTBN_ERR_INVAL, "NULL grp returned %d", err);
	zassert_equal(status, 0, "NULL grp status %d", status);

	/* Unregistered slot: NOENT, no backend detail. */
	status = 0x5A5A;
	err = mbedtls_ls_otbn_ecdsa_sign_slot_ex(&grp, &r, &s, SLOT_EMPTY,
						 otbn_slot_hash, sizeof(otbn_slot_hash),
						 NULL, NULL, &status);
	zassert_equal(err, LS_OTBN_ERR_NOENT, "empty slot returned %d", err);
	zassert_equal(status, 0, "empty slot status %d", status);

	/* Curve mismatch: CURVE, no backend detail. */
	{
		mbedtls_ecp_group p384;

		mbedtls_ecp_group_init(&p384);
		err = mbedtls_ecp_group_load(&p384, MBEDTLS_ECP_DP_SECP384R1);
		zassert_equal(err, 0, "load P384 failed: %d", err);
		status = 0x5A5A;
		err = mbedtls_ls_otbn_ecdsa_sign_slot_ex(&p384, &r, &s, SLOT_P256A,
							 otbn_slot_hash,
							 sizeof(otbn_slot_hash),
							 NULL, NULL, &status);
		zassert_equal(err, LS_OTBN_ERR_CURVE, "curve mismatch returned %d", err);
		zassert_equal(status, 0, "curve mismatch status %d", status);
		mbedtls_ecp_group_free(&p384);
	}

	/* Success: status may be omitted, and is 0 when supplied. */
	err = mbedtls_ls_otbn_ecdsa_sign_slot_ex(&grp, &r, &s, SLOT_P256A,
						 otbn_slot_hash, sizeof(otbn_slot_hash),
						 NULL, NULL, NULL);
	zassert_equal(err, 0, "success with NULL status returned %d", err);

	status = 0x5A5A;
	err = mbedtls_ls_otbn_ecdsa_sign_slot_ex(&grp, &r, &s, SLOT_P256A,
						 otbn_slot_hash, sizeof(otbn_slot_hash),
						 NULL, NULL, &status);
	zassert_equal(err, 0, "success returned %d", err);
	zassert_equal(status, 0, "success status %d", status);

	mbedtls_mpi_free(&r);
	mbedtls_mpi_free(&s);
	mbedtls_ecp_group_free(&grp);
	ZT_INFO("s18_sign_slot_ex_status", "ok=1");
#else
	ztest_test_skip();
#endif
}

/* 6 explicit params: setup/teardown are suite-level, before/after per-test --
 * all NULL here (no shared fixture). Do NOT move a teardown into `after`. */
ZTEST_SUITE(otbn_slot, NULL, NULL, NULL, NULL, NULL);
