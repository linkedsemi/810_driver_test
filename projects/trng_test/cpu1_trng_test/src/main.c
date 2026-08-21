/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * TRNG 单核测试入口（cpu1 安全域）。
 *
 * 主动自驱动模式（同 i3c_dualcore_test controller 侧）：test_main() 里
 * 顺序跑完所有 ZTEST（ztest_run_all），结尾打 SUMMARY + DONE token。
 * ZTEST 框架线程即主线程，全程不返回，无 LOG_PANIC 死窗风险。
 */

#include "trng_test.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(trng_test, LOG_LEVEL_INF);

/* dwtrng@40004000（cpu1 安全域，DesignWare TRNG） */
const struct device *trng_dev = DEVICE_DT_GET(DT_NODELABEL(trng1));

void test_main(void)
{
	LOG_INF("[CPU1 TRNG] TRNG test started");

	trng_test_run();
}
