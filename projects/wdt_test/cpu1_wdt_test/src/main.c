/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * WDT test - entry point (single-core, cpu1)
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "ztest_glue.h"

LOG_MODULE_REGISTER(wdt_test, LOG_LEVEL_INF);

void test_main(void)
{
#ifdef WDT_DESTRUCTIVE_RESET
	extern void wdt_destructive_main(void);
	wdt_destructive_main();
#else
	LOG_INF("[CPU1 WDT] WDT test started");
	zt_init("TGT");
	zt_run_all();
#endif
}
