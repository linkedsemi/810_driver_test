/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN ECDSA delegation -- cpu1 (target, the delegation SERVER).
 *
 * test_main brings up the delegation server (mbox callback + work queue that
 * performs the real OTBN ECDSA on behalf of cpu2) and then runs the SAME ztest
 * matrix against the LOCAL OTBN backend. The double-core PASS therefore means
 * "cpu2's proxied ops round-tripped correctly" AND "cpu1's local crypto is
 * correct" -- cpu1 doubles as the oracle for cpu2.
 */

#include <zephyr/kernel.h>

#include "ztest_glue.h"

#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER)
void mbedtls_ls_otbn_delegation_server_chanels_init(void);
#endif

void test_main(void)
{
	/* Role banner (harness identifies the TGT port by it). */
	printk("[CPU1 OTBN_DELEGATION TGT] delegation server started\n");

	/* Register the otbn_crypto mbox callback + start the consumer work queue
	 * before cpu2 issues any request. Guarded: case_no_server builds this core
	 * with CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER=n (peer absent). */
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER)
	mbedtls_ls_otbn_delegation_server_chanels_init();
#endif

	zt_init("TGT");
	zt_run_all();
}
