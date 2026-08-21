/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RTC 单核测试入口（cpu1 安全域）。
 *
 * 主动自驱动模式（同 trng_test controller 侧）：test_main() 里
 * 顺序跑完所有 ZTEST（ztest_run_all），结尾打 SUMMARY + DONE token。
 * ZTEST 框架线程即主线程，全程不返回，无 LOG_PANIC 死窗风险。
 */

#include "rtc_test.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rtc_test, LOG_LEVEL_INF);

/* rtc@4006fc00（PMU 域硬件 RTC，cyc-1hz=32768，cpu1 安全域） */
const struct device *rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rtc));

void test_main(void)
{
	LOG_INF("[CPU1 RTC] RTC test started");

	rtc_test_run();
}
