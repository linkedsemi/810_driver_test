/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN ECDSA delegation -- cpu2 (controller, the delegation CLIENT).
 *
 * test_main only sets up the delegation channel and then hands over to ztest;
 * the test bodies live in tests/src/otbn_delegation_test.c (shared with cpu1).
 * The client's mbedtls_ecdsa_{genkey,sign,verify} are the delegation stubs, so
 * every test item below drives a real mbox round-trip to cpu1.
 */

#include <zephyr/kernel.h>

#include "ztest_glue.h"
#include "ls_otbn_delegation.h"

void test_main(void)
{
	/* Role banner (harness identifies the CTRL port by it). */
	printk("[CPU2 OTBN_DELEGATION CTRL] delegation client started\n");

	/* Bring up the otbn_crypto mbox channel BEFORE any test sends a request.
	 * cpu1 (server) is released first (CONFIG_BOOT_CPU2 on the cpu1 image) so
	 * its callback is already registered when the first request goes out.
	 * Guarded so a non-client build still links. */
#if defined(CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_CLIENT)
	ls_otbn_delegation_client_init();
#endif

	zt_init("CTRL");
	zt_run_all();
}
