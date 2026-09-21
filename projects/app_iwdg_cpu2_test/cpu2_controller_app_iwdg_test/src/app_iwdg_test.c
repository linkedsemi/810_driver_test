/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * app_iwdg 非破坏性用例（cpu2，case_non_destructive = CASE_ID 0）
 *
 * 对照 zephyr/drivers/watchdog/wdt_iwdg_ls.c 逐分支覆盖（SPEC_REFERENCE
 * §6.5.1 七维扫描）：
 *   install_timeout: window.min != 0 拒 / window.max == 0 拒 /
 *                    HAL_IWDG_IS_MATCH 按实例匹配 flags / callback 注册与清除
 *   setup:           IWDT_LOAD == 0 -> -ENOTSUP / IWDT_CTRL != 0 -> -EBUSY /
 *                    WDT_OPT_PAUSE_IN_SLEEP -> -ENOTSUP / 正常使能路径
 *   feed / disable:  正常返回 0，喂狗窗口内不复位
 *   换算:            timeout_ms -> IWDT_LOAD ticks
 *
 * 全部用例不使看门狗跑到超时：setup 成功的用例在断言后立即 disable；
 * test_feed_within_window_no_reset 有意跨越一个完整超时窗口，用"后续断言
 * 仍能执行"证明喂狗确实生效（若喂狗失效，本 hart 会被 app_iwdg 复位，
 * 整个 case 表现为重启）。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/ztest.h>
#include <errno.h>

#include "ztest_glue.h"

/*
 * IWDT_LOAD / IWDT_CTRL 在实例寄存器块的字偏移 0 / 1（字节 0x0 / 0x4，
 * 见 reg_iwdgv2_type.h）。驱动无 API 把 IWDT_LOAD 复位为 0，故
 * 「未安装超时」前置态由测试直接写寄存器复现（白盒前置态设置，
 * 不改变被测驱动行为）。
 */
#define IWDG_LOAD_WORD 0u
#define IWDG_CTRL_WORD 1u

#define APP_IWDG_REGS ((volatile uint32_t *)DT_REG_ADDR(DT_NODELABEL(app_iwdg)))

/* 驱动 iwdt_ls_setup 正常路径写入 CTRL = IWDT_RST_EN | IWDT_EN = 0x2 | 0x1 */
#define IWDG_CTRL_ENABLED 0x3u

/*
 * app_iwdg 的 clock-frequency = 32768（lsqsh.dtsi）。
 * 驱动换算 iwdt_calculate_load：ticks = timeout_ms * 32768 / 1000，
 * 其中 `timeout_ms * frequency` 是 **uint32 乘法**（两个操作数都是
 * uint32_t，C 整型提升后仍为 32 位）——故下界取值均选在
 * timeout_ms <= 131071（131072 * 32768 == 2^32）的**不回绕区间**内，
 * 预期值即按该式手算。回绕区间无确定性预期，见 COVERAGE.md 未覆盖项。
 */
#define TICKS_1MS 32u             /* 1 * 32768 / 1000                    */
#define TICKS_1000MS 32768u       /* 1000 * 32768 / 1000                 */
#define TICKS_65535MS 2147450u    /* 65535 * 32768 / 1000（商截断）      */
#define TICKS_MAX_NO_WRAP 4294934u /* 131071 * 32768 / 1000（不回绕上界） */

static const struct device *app_dev;

static void app_iwdg_dummy_cb(const struct device *dev, int channel)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
}

static void *app_iwdg_setup(void)
{
	app_dev = DEVICE_DT_GET(DT_NODELABEL(app_iwdg));
	zassert_true(device_is_ready(app_dev), "app_iwdg not ready");
	/* 清掉可能的遗留使能态，使首项与执行顺序解耦 */
	(void)wdt_disable(app_dev);
	return NULL;
}

static void app_iwdg_teardown(void *data)
{
	ARG_UNUSED(data);
	/* setup 无共享句柄，teardown 只做幂等的关闭，保证不复位 */
	(void)wdt_disable(app_dev);
}

static int install_with(uint32_t window_min, uint32_t window_max, uint8_t flags,
			wdt_callback_t cb)
{
	struct wdt_timeout_cfg cfg = {
		.flags = flags,
		.window.min = window_min,
		.window.max = window_max,
		.callback = cb,
	};

	return wdt_install_timeout(app_dev, &cfg);
}

/* ---- install_timeout: flags × 实例匹配（HAL_IWDG_IS_MATCH，ls_msp_iwdg.c:72） ---- */

/* app_iwdg 属 HART 复位域，只接受 WDT_FLAG_RESET_CPU_CORE */
ZTEST(app_iwdg, test_install_accepts_reset_cpu_core_flag)
{
	int ret = install_with(0, 60000, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_true(ret >= 0,
		     "app_iwdg install(RESET_CPU_CORE) returned %d, expected >= 0", ret);
}

/* app_iwdg 不是 SOC 复位域实例（HAL 只对 SEC_PMU_IWDG 返回匹配）-> -EINVAL */
ZTEST(app_iwdg, test_install_rejects_reset_soc_flag)
{
	int ret = install_with(0, 1000, WDT_FLAG_RESET_SOC, NULL);

	zassert_equal(ret, -EINVAL,
		      "app_iwdg install(RESET_SOC) returned %d, expected -EINVAL", ret);
}

/* flags = 0（NONE）与未知位 0xFF 均不匹配任何实例 -> -EINVAL */
ZTEST(app_iwdg, test_install_rejects_unmatched_flags)
{
	int ret_none = install_with(0, 1000, WDT_FLAG_RESET_NONE, NULL);

	zassert_equal(ret_none, -EINVAL,
		      "install(flags=NONE) returned %d, expected -EINVAL", ret_none);

	int ret_ff = install_with(0, 1000, 0xFF, NULL);

	zassert_equal(ret_ff, -EINVAL,
		      "install(flags=0xFF) returned %d, expected -EINVAL", ret_ff);
}

/* ---- install_timeout: 窗口参数边界 ---- */

/* window.max == 0：上界为 0 无意义 -> -EINVAL */
ZTEST(app_iwdg, test_install_invalid_window_max_zero)
{
	int ret = install_with(0, 0, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_equal(ret, -EINVAL,
		      "install(window.max=0) returned %d, expected -EINVAL", ret);
}

/* window.min != 0：驱动不支持窗口模式，一律 -EINVAL */
ZTEST(app_iwdg, test_install_invalid_window_min_nonzero)
{
	int ret = install_with(1, 1000, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_equal(ret, -EINVAL,
		      "install(window.min=1) returned %d, expected -EINVAL", ret);
}

/* ---- install_timeout: callback 注册 / 清除分支 ---- */

/* callback != NULL：驱动走 irq_config_func + data->callback 赋值分支 */
ZTEST(app_iwdg, test_install_with_callback_accepted)
{
	int ret = install_with(0, 60000, WDT_FLAG_RESET_CPU_CORE, app_iwdg_dummy_cb);

	zassert_equal(ret, 0, "install(callback!=NULL) returned %d, expected 0", ret);

	/* 清除已注册回调，避免遗留 irq_enable + 非空 data->callback 影响后续用例 */
	int ret_clear = install_with(0, 60000, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_equal(ret_clear, 0, "install(callback=NULL) returned %d, expected 0",
		      ret_clear);
}

/* callback == NULL：驱动走 data->callback 置空分支 */
ZTEST(app_iwdg, test_install_without_callback_accepted)
{
	int ret = install_with(0, 60000, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_equal(ret, 0, "install(callback==NULL) returned %d, expected 0", ret);
}

/* ---- setup: 正常使能路径（含 CTRL 位域白盒断言） ---- */

ZTEST(app_iwdg, test_setup_enables_ctrl_rst_en)
{
	int ch = install_with(0, 60000, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_true(ch >= 0, "install_timeout returned %d", ch);

	int ret = wdt_setup(app_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);

	zassert_equal(ret, 0, "setup returned %d, expected 0", ret);

	uint32_t ctrl = APP_IWDG_REGS[IWDG_CTRL_WORD];

	ZT_INFO("test_setup_enables_ctrl_rst_en", "ctrl=0x%x", ctrl);
	zassert_equal(ctrl, IWDG_CTRL_ENABLED,
		      "IWDT_CTRL=0x%x after setup, expected 0x%x (RST_EN|EN)",
		      ctrl, IWDG_CTRL_ENABLED);

	int dis = wdt_disable(app_dev);

	zassert_equal(dis, 0, "disable returned %d", dis);
	zassert_equal(APP_IWDG_REGS[IWDG_CTRL_WORD], 0u,
		      "IWDT_CTRL should be 0 after disable");
}

/* ---- setup: 错误分支 ---- */

/* IWDT_LOAD == 0（未安装超时）-> -ENOTSUP */
ZTEST(app_iwdg, test_setup_without_timeout_enotsup)
{
	/* 复现「未安装超时」前置态：CTRL=0 且 LOAD=0（与套件内执行顺序解耦） */
	(void)wdt_disable(app_dev);
	APP_IWDG_REGS[IWDG_LOAD_WORD] = 0;
	APP_IWDG_REGS[IWDG_CTRL_WORD] = 0;

	int ret = wdt_setup(app_dev, 0);

	zassert_equal(ret, -ENOTSUP,
		      "setup without install returned %d, expected -ENOTSUP", ret);
}

/* WDT_OPT_PAUSE_IN_SLEEP 不支持 -> -ENOTSUP（且不使能看门狗） */
ZTEST(app_iwdg, test_setup_pause_in_sleep_enotsup)
{
	int ch = install_with(0, 60000, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_true(ch >= 0, "install_timeout returned %d", ch);

	int ret = wdt_setup(app_dev, WDT_OPT_PAUSE_IN_SLEEP);

	zassert_equal(ret, -ENOTSUP,
		      "setup(PAUSE_IN_SLEEP) returned %d, expected -ENOTSUP", ret);
	zassert_equal(APP_IWDG_REGS[IWDG_CTRL_WORD], 0u,
		      "IWDT_CTRL should stay 0 after -ENOTSUP");

	(void)wdt_disable(app_dev);
}

/* IWDT_CTRL != 0（已 setup）-> -EBUSY */
ZTEST(app_iwdg, test_setup_twice_ebusy)
{
	int ch = install_with(0, 60000, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_true(ch >= 0, "install_timeout returned %d", ch);

	int ret = wdt_setup(app_dev, 0);

	zassert_equal(ret, 0, "first setup returned %d, expected 0", ret);

	int ret2 = wdt_setup(app_dev, 0);

	zassert_equal(ret2, -EBUSY,
		      "second setup returned %d, expected -EBUSY", ret2);

	(void)wdt_disable(app_dev);
}

/* ---- feed：窗口内喂狗，系统不复位 ---- */

ZTEST(app_iwdg, test_feed_within_window_no_reset)
{
	const uint32_t timeout_ms = 1000;
	int ch = install_with(0, timeout_ms, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_true(ch >= 0, "install_timeout returned %d", ch);

	int ret = wdt_setup(app_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);

	zassert_equal(ret, 0, "setup returned %d, expected 0", ret);

	/* 总时长 6 * 200ms = 1200ms > 1000ms 超时窗口：喂狗若失效，
	 * 本 hart 会在窗口处被 app_iwdg 复位，本项及后续项都不可能执行完。 */
	for (int i = 0; i < 6; i++) {
		k_sleep(K_MSEC(200));
		int feed = wdt_feed(app_dev, ch);

		zassert_equal(feed, 0, "feed #%d returned %d, expected 0", i, feed);
	}

	uint32_t ctrl = APP_IWDG_REGS[IWDG_CTRL_WORD];

	ZT_INFO("test_feed_within_window_no_reset", "timeout_ms=%u,elapsed_ms=%u",
		timeout_ms, 6u * 200u);
	zassert_equal(ctrl, IWDG_CTRL_ENABLED,
		      "IWDT_CTRL=0x%x after 1200ms of feeding, expected still enabled",
		      ctrl);

	int dis = wdt_disable(app_dev);

	zassert_equal(dis, 0, "disable returned %d", dis);
}

/* ---- timeout_ms -> IWDT_LOAD 换算（不回绕区间内） ---- */

ZTEST(app_iwdg, test_load_ticks_min_timeout)
{
	int ch = install_with(0, 1, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_true(ch >= 0, "install_timeout returned %d", ch);

	uint32_t load = APP_IWDG_REGS[IWDG_LOAD_WORD];

	ZT_INFO("test_load_ticks_min_timeout", "ms=1,ticks=%u", load);
	zassert_equal(load, TICKS_1MS,
		      "IWDT_LOAD=%u for max=1ms, expected %u", load, TICKS_1MS);
}

ZTEST(app_iwdg, test_load_ticks_1000ms)
{
	int ch = install_with(0, 1000, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_true(ch >= 0, "install_timeout returned %d", ch);

	uint32_t load = APP_IWDG_REGS[IWDG_LOAD_WORD];

	ZT_INFO("test_load_ticks_1000ms", "ms=1000,ticks=%u", load);
	zassert_equal(load, TICKS_1000MS,
		      "IWDT_LOAD=%u for max=1000ms, expected %u", load, TICKS_1000MS);
}

/* 商截断：65535 * 32768 / 1000 = 2147450.88 -> 2147450 */
ZTEST(app_iwdg, test_load_ticks_65535ms)
{
	int ch = install_with(0, 65535, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_true(ch >= 0, "install_timeout returned %d", ch);

	uint32_t load = APP_IWDG_REGS[IWDG_LOAD_WORD];

	ZT_INFO("test_load_ticks_65535ms", "ms=65535,ticks=%u", load);
	zassert_equal(load, TICKS_65535MS,
		      "IWDT_LOAD=%u for max=65535ms, expected %u", load, TICKS_65535MS);
}

/* 不回绕上界：131072 * 32768 == 2^32（回绕起点），131071 为最大安全值 */
ZTEST(app_iwdg, test_load_ticks_max_no_wrap)
{
	int ch = install_with(0, 131071u, WDT_FLAG_RESET_CPU_CORE, NULL);

	zassert_true(ch >= 0, "install_timeout returned %d", ch);

	uint32_t load = APP_IWDG_REGS[IWDG_LOAD_WORD];

	ZT_INFO("test_load_ticks_max_no_wrap", "ms=131071,ticks=%u", load);
	zassert_equal(load, TICKS_MAX_NO_WRAP,
		      "IWDT_LOAD=%u for max=131071ms, expected %u", load,
		      TICKS_MAX_NO_WRAP);
}

ZTEST_SUITE(app_iwdg, NULL, app_iwdg_setup, NULL, NULL, app_iwdg_teardown);
