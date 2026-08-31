/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RTC 单核测试入口（cpu1 安全域）。
 * 纯 ztest：test_main 打印角色 banner 后 zt_init + zt_run_all
 * （common/ztest_glue），不返回（同 ztest 语义）。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>

#include "ztest_glue.h"

LOG_MODULE_REGISTER(rtc_test, LOG_LEVEL_INF);

/* rtc@4006fc00（PMU 域硬件 RTC，cyc-1hz=32768，cpu1 安全域） */
const struct device *rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rtc));

void test_main(void)
{
	LOG_INF("[CPU1 RTC] RTC test started");   /* 角色 bannerp（harness 依赖） */

	zt_init("TGT");
	zt_run_all();
}
