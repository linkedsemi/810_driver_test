/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * spi_spif_test 单核测试入口（cpu1 安全域）。
 * 纯 ztest：test_main 打印角色 banner 后 zt_init + zt_run_all
 * （common/ztest_glue），不返回（同 ztest 语义）。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "ztest_glue.h"

LOG_MODULE_REGISTER(spi_spif_test, LOG_LEVEL_INF);

void test_main(void)
{
	LOG_INF("[CPU1 SPI_SPIF] spi_ls + spi filter test started");   /* 角色 banner（harness 依赖） */

	zt_init("TGT");
	zt_run_all();
}
