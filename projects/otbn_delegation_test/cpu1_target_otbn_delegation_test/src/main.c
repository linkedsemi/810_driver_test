/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN ECDSA delegation -- cpu1 (target, the delegation SERVER).
 *
 * test_main brings up the delegation server (mbox callback + dedicated thread
 * that performs the real OTBN ECDSA on behalf of cpu2) and then runs the SAME ztest
 * matrix against the LOCAL OTBN backend. The double-core PASS therefore means
 * "cpu2's proxied ops round-tripped correctly" AND "cpu1's local crypto is
 * correct" -- cpu1 doubles as the oracle for cpu2.
 */

#include <zephyr/kernel.h>

#include "ztest_glue.h"
#include "ls_otbn_delegation.h"

#if defined(OTBN_DELEG_SLOT)
#include "mbedtls/ecp.h"
#include "mbedtls_otbn_ecdsa_slot_mbox.h"
#include "otbn_delegation_slot_vectors.h"
#endif

void test_main(void)
{
	/* Role banner (harness identifies the TGT port by it). */
	printk("[CPU1 OTBN_DELEGATION TGT] delegation server started\n");

	/* Register the otbn_crypto mbox callback + start the delegation thread
	 * before cpu2 issues any request. Guarded: case_no_server builds this core
	 * with CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER=n (peer absent). */
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER)
	ls_otbn_delegation_server_init();
#endif

	/* case_slot: publish slot 0 before ztest starts, so the client's first
	 * request (which may race this boot) finds it. The key buffer is static in
	 * the vectors header and therefore outlives the test run. */
#if defined(OTBN_DELEG_SLOT) && defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER)
	{
		int rc0 = ls_otbn_ecdsa_key_slot_register(0, MBEDTLS_ECP_DP_SECP256R1,
							  otbn_slot_key_le);
		int rc1 = ls_otbn_ecdsa_key_slot_register(1, MBEDTLS_ECP_DP_SECP384R1,
							  p384_key_le);
		int rc2 = ls_otbn_ecdsa_key_slot_register(2, MBEDTLS_ECP_DP_SM2,
							  sm2_key_le);
		int rc3 = ls_otbn_ecdsa_key_slot_register(3, MBEDTLS_ECP_DP_SECP256R1,
							  p256b_key_le);

		printk("[CPU1 OTBN_SLOT TGT] slots 0..3 register rc=%d,%d,%d,%d\n",
		       rc0, rc1, rc2, rc3);
	}
#endif

	zt_init("TGT");
	zt_run_all();
}
