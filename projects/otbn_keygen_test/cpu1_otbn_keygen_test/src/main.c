/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * OTBN RSA 密钥生成单核测试入口（cpu1 安全域）。
 * 纯 ztest：test_main 打印角色 banner 后 zt_init + zt_run_all
 * （common/ztest_glue），跑完不返回（同 ztest 语义）。
 */

#include <zephyr/kernel.h>

#include "ztest_glue.h"

void test_main(void)
{
	printk("[CPU1 OTBN_KEYGEN] OTBN RSA keygen test started\n");   /* 角色 banner（harness 依赖） */

	zt_init("TGT");
	zt_run_all();
}
