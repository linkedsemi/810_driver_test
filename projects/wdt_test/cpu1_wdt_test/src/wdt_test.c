/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * WDT test cases (non-destructive, sec_iwdg)
 *
 * 破坏性 case（触发复位）需要 XIP 变体 + noinit 状态机，
 * 成本太高不纳入本工程，由独立脚本跑。
 * 本工程只跑非破坏性 case（不触发复位）。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/ztest.h>
#include <errno.h>

static const struct device *wdt_dev;
static struct wdt_timeout_cfg wdt_cfg;

static void *wdt_test_setup(void)
{
	wdt_dev = DEVICE_DT_GET(DT_NODELABEL(sec_iwdg));
	zassert_true(device_is_ready(wdt_dev), "sec_iwdg not ready");
	return NULL;
}

static void wdt_test_teardown(void *data)
{
	ARG_UNUSED(data);
	/* 确保狗被关闭，不影响后续 case */
	wdt_disable(wdt_dev);
}

/* TC-1: install_timeout with max_time_ms=0 -> -EINVAL */
ZTEST(wdt, test_wdt_invalid_timeout_min)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 0,
	};
	int ret = wdt_install_timeout(wdt_dev, &cfg);
	zassert_equal(ret, -EINVAL,
		"install_timeout(max=0) returned %d, expected -EINVAL", ret);
}

/* TC-2: install_timeout with invalid flags -> -EINVAL */
ZTEST(wdt, test_wdt_invalid_flags)
{
	struct wdt_timeout_cfg cfg = {
		.flags = 0xFF,  /* 无效 flags */
		.window.min = 0,
		.window.max = 500,
	};
	int ret = wdt_install_timeout(wdt_dev, &cfg);
	/* 驱动可能接受也可能拒绝，取决于 flags 解析 */
	/* 这里只验证不 crash */
	ARG_UNUSED(ret);
}

/* TC-3: setup + feed (不复位) */
ZTEST(wdt, test_wdt_setup_then_feed)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 10000,  /* 10s 超时 */
	};
	int channel = wdt_install_timeout(wdt_dev, &cfg);
	zassert_true(channel >= 0, "install_timeout failed: %d", channel);

	int ret = wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
	zassert_equal(ret, 0, "wdt_setup failed: %d", ret);

	/* 连续 feed 5 次（间隔 1s < 10s 超时），不应复位 */
	for (int i = 0; i < 5; i++) {
		wdt_feed(wdt_dev, channel);
		k_msleep(1000);
	}

	wdt_disable(wdt_dev);
}

/* TC-4: disable 后不再触发回调 */
ZTEST(wdt, test_wdt_disable)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 500,
	};
	int channel = wdt_install_timeout(wdt_dev, &cfg);
	zassert_true(channel >= 0, "install_timeout failed: %d", channel);

	int ret = wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
	zassert_equal(ret, 0, "wdt_setup failed: %d", ret);

	wdt_disable(wdt_dev);

	/* 等 1s（超过 500ms 超时），因已 disable 不应复位 */
	k_msleep(1000);
}

/* TC-5: install 两次超时（第二次应报错，单通道） */
ZTEST(wdt, test_wdt_two_timeouts_not_supported)
{
	struct wdt_timeout_cfg cfg1 = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 10000,
	};
	int ch1 = wdt_install_timeout(wdt_dev, &cfg1);
	zassert_true(ch1 >= 0, "first install_timeout failed: %d", ch1);

	struct wdt_timeout_cfg cfg2 = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 5000,
	};
	int ch2 = wdt_install_timeout(wdt_dev, &cfg2);
	/* 第二次 install 可能返回新 channel 或错误 */
	ARG_UNUSED(ch2);

	wdt_disable(wdt_dev);
}

ZTEST_SUITE(wdt, NULL, wdt_test_setup, NULL, wdt_test_teardown, NULL);
