/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * eMMC 块层测试入口（cpu1）。
 * 纯 ztest：test_main 打印角色 banner 后 zt_init + zt_run_all
 * （common/ztest_glue），不返回（同 ztest 语义，SPEC_REFERENCE §6.1.1）。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "ztest_glue.h"

LOG_MODULE_REGISTER(emmc_block_test, LOG_LEVEL_INF);

void test_main(void)
{
	LOG_INF("[CPU1 EMMC BLOCK] sdhci1 + on-board eMMC block-layer test started");

	zt_init("TGT");
	zt_run_all();
}
