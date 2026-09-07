/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * MBOX test - cpu1 (target) entry point
 * mbox 测试不走 ztest（双核同步复杂），直接跑测试逻辑。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "ztest_glue.h"

extern void mbox_target_main(void);

LOG_MODULE_REGISTER(mbox_tgt_main, LOG_LEVEL_INF);

void test_main(void)
{
	LOG_INF("[CPU1 MBOX] MBOX test entry");
	zt_init("TGT");

	mbox_target_main();

	printk("PROJECT EXECUTION: SUCCESSFUL\n");

	/* 不返回（§8.1） */
	k_sleep(K_FOREVER);
}
