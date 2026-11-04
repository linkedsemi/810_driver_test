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
#elif defined(WDT_PMU_RESET_TEST)
	extern void wdt_pmu_reset_main(void);
	wdt_pmu_reset_main();
#else
	LOG_INF("[CPU1 WDT] WDT test started");
	LOG_INF("[CPU1 WDT] non-destructive suite: install/query only, "
		"the watchdog is never allowed to fire -> no reset here. "
		"Real reset: `itest hwreset sec_iwdg` (standalone: "
		"case_destructive_reset / case_pmu_iwdg_full_reset)");
	zt_init("TGT");
	zt_run_all();
#endif
}
