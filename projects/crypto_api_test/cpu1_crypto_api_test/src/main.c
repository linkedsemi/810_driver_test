/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * crypto_api_test 单核测试入口（cpu1 安全域）。
 * 纯 ztest：test_main 打印角色 banner 后 zt_init + zt_run_all
 * （common/ztest_glue），不返回（同 ztest 语义）。
 */

#include <zephyr/kernel.h>

#include "ztest_glue.h"

void test_main(void)
{
	printk("[CPU1 CRYPTO API] crypto api test started\n");   /* 角色 banner（harness 依赖） */

	zt_init("TGT");
	zt_run_all();
}
