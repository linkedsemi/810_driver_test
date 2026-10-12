/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OTBN_DELEGATION_TEST_PROTOCOL_H
#define OTBN_DELEGATION_TEST_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OTBN ECDSA delegation test (otbn_delegation_test).
 *
 * No cross-core command protocol is defined here: the delegation wire format
 * is owned by the SoC support code and must NOT be duplicated in the test:
 *   zephyr/soc/linkedsemi/lsqsh/otbn/mbedtls/mbedtls_otbn_crypto_mbox.c
 *     - op 0x11 GENE_KEY / 0x22 SIGN / 0x33 VERIFY (struct otbn_delegate_params)
 *     - client side = the mbedtls_ecdsa_{genkey,sign,verify} stubs
 *     - server side = mbox callback -> k_msgq -> work -> real OTBN ECDSA
 * The tests only call the public mbedtls ECDSA API; which backend runs is
 * chosen purely by CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_{CLIENT,SERVER}.
 *
 * Topology (dual core, on-chip -- no external wiring):
 *   cpu2 (controller, uart5): delegation CLIENT -- mbedtls_ecdsa_* are stubs,
 *                             every op is a mbox round-trip to cpu1.
 *   cpu1 (target, uart1):     delegation SERVER -- answers cpu2's requests AND
 *                             runs the same matrix against local OTBN ECDSA.
 *
 * Curve whitelist (both the client stub and the server consumer enforce it):
 *   SECP256R1 / SECP384R1 / SM2 -- anything else must be rejected.
 */

#define OTBN_DELEGATION_TEST_PREFIX "OTBN_DELEGATION_TEST"

/* Curve ids covered (mirrors the client stub's whitelist). */
#define OTBN_DELEG_CURVE_IDS \
	{ MBEDTLS_ECP_DP_SECP256R1, MBEDTLS_ECP_DP_SECP384R1, MBEDTLS_ECP_DP_SM2 }

#ifdef __cplusplus
}
#endif

#endif /* OTBN_DELEGATION_TEST_PROTOCOL_H */
