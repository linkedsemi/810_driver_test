/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * perf_test cpu1 entry: pure ztest, one banner line then zt_init/zt_run_all.
 */
#include <zephyr/kernel.h>

#include "ztest_glue.h"

extern int perf_proxy_server_init(void);
extern int perf_telemetry_init(void);

void test_main(void)
{
	printk("[CPU1 PERF] performance test started\n");

	/* Start the flash proxy server before the cpu2 client can PING it, and
	 * the telemetry relay so cpu2's result lines appear on this console. */
	(void)perf_proxy_server_init();
	(void)perf_telemetry_init();

	zt_init("TGT");
	zt_run_all();
}
