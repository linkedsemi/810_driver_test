/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * MBOX test - cpu2 (controller) entry point
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "ztest_glue.h"

extern void mbox_controller_main(void);

LOG_MODULE_REGISTER(mbox_ctrl_main, LOG_LEVEL_INF);

void test_main(void)
{
	LOG_INF("[CPU2 MBOX] MBOX controller entry");
	zt_init("CTRL");

	mbox_controller_main();

	printk("PROJECT EXECUTION: SUCCESSFUL\n");

	k_sleep(K_FOREVER);
}
