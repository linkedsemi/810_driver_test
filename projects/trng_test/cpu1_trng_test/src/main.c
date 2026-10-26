/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * TRNG 单核测试入口（cpu1 安全域）。
 * 纯 ztest：test_main 打印角色 banner 后 zt_init + zt_run_all
 * （common/ztest_glue），不返回（同 ztest 语义）。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/logging/log.h>

#include "ztest_glue.h"

LOG_MODULE_REGISTER(trng_test, LOG_LEVEL_INF);

/* dwtrng@40004000（cpu1 安全域，DesignWare TRNG） */
const struct device *trng_dev = DEVICE_DT_GET(DT_NODELABEL(trng1));

/* trng@400a2000（cpu1 安全域，APB ring-oscillator TRNG）——独立模块，
 * 与 trng1 不是同一个驱动，单独成套测试。 */
const struct device *trng0_dev = DEVICE_DT_GET(DT_NODELABEL(trng0));

void test_main(void)
{
	LOG_INF("[CPU1 TRNG] TRNG test started");   /* 角色 banner（harness 依赖） */

	zt_init("TGT");
	zt_run_all();
}
