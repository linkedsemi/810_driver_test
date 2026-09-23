/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * rtc_timer_ls 单核测试入口（cpu1）。
 * 纯 ztest：zt_init + zt_run_all。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>

#include "ztest_glue.h"

LOG_MODULE_REGISTER(rtc_timer_test, LOG_LEVEL_INF);

/* dts 节点 rtc_timer（linkedsemi,ls-rtc-timer） */
const struct device *rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rtc_timer));

void test_main(void)
{
	LOG_INF("[CPU1 RTC_TIMER] rtc_timer_ls test started");

	zt_init("TGT");
	zt_run_all();
}
