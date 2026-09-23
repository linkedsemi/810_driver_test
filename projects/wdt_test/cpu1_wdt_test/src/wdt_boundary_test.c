/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * WDT boundary / error-path tests (non-destructive, sec_iwdg + pmu_iwdg)
 *
 * 对照 zephyr/drivers/watchdog/wdt_iwdg_ls.c 逐分支覆盖：
 *   install_timeout: window.min != 0 拒 / window.max == 0 拒 /
 *                    HAL_IWDG_IS_MATCH 按实例匹配 flags / callback 注册与清除
 *   setup:           IWDT_LOAD == 0 -> -ENOTSUP /
 *                    IWDT_CTRL & (IWDT_EN|IWDT_RST_EN) != 0 -> -EBUSY /
 *                    WDT_OPT_PAUSE_IN_SLEEP -> -ENOTSUP / 正常使能路径
 *   disable/feed:    正常返回 0
 *   换算:            timeout_ms -> IWDT_LOAD ticks（含 uint32 饱和边界）
 *
 * 全部用例均不使能看门狗到超时（setup 成功的用例在断言后立即 disable），
 * 因此不触发任何复位。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/ztest.h>
#include <errno.h>

#include "ztest_glue.h"

/*
 * IWDT_LOAD / IWDT_CTRL 在实例寄存器块偏移 0x0 / 0x4。
 * 驱动无 API 把 IWDT_LOAD 复位为 0，故「未安装超时」前置态由测试直接写寄存器复现
 * （白盒前置态设置，不改变被测驱动行为）。
 */
#define IWDG_LOAD_OFFSET 0u
#define IWDG_CTRL_OFFSET 1u

#define SEC_IWDG_REGS ((volatile uint32_t *)DT_REG_ADDR(DT_NODELABEL(sec_iwdg)))

/*
 * sec_iwdg / pmu_iwdg 的 clock-frequency = 32768（lsqsh.dtsi）。
 * 驱动换算（wdt_iwdg_ls.c，iwdt_calculate_load）：
 *     uint64_t ticks = (uint64_t)timeout_ms * frequency / 1000;
 *     return (ticks > UINT32_MAX) ? UINT32_MAX : (uint32_t)ticks;
 *
 * 2026-09-18 驱动修复为 64 位乘法后，本节预期值同步改判（原 4294934 / 0 / 4294934
 * 是按 32 位回绕语义手算的，已作废）。改判后语义：
 *   - 未饱和区（timeout_ms < 131072000）：ticks = timeout_ms * 32768 / 1000；
 *   - 饱和区（timeout_ms >= 131072000）：ticks 超过 UINT32_MAX，饱和到 0xFFFFFFFF。
 * ⚠ 饱和是「钳到硬件寄存器最大值」的本意行为，不是缺陷：0xFFFFFFFF ticks 在 32768 Hz
 *    下约合 131072 s（≈36.4 h）——请求超过该上限即被钳到此值。
 */
#define TICKS_1MS 32u              /* 1 * 32768 / 1000（未饱和区）   */
#define TICKS_1000MS 32768u        /* 1000 * 32768 / 1000（未饱和区） */
/* 64 位饱和边界（timeout_ms >= 131072000 落入饱和区） */
#define TICKS_131071999MS 4294967263u  /* 131071999*32768/1000 = 4294967263（未饱和） */
#define TICKS_131072000MS 0xFFFFFFFFu  /* 131072000*32768/1000 = 2^32 > UINT32_MAX -> 饱和 */
#define TICKS_UINT32_MAX_MS 0xFFFFFFFFu /* 0xFFFFFFFF*32768/1000 远大于 UINT32_MAX -> 饱和 */

static const struct device *sec_dev;
static const struct device *pmu_dev;

static void wdt_boundary_dummy_cb(const struct device *dev, int channel)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
}

static void *wdt_boundary_setup(void)
{
	sec_dev = DEVICE_DT_GET(DT_NODELABEL(sec_iwdg));
	pmu_dev = DEVICE_DT_GET(DT_NODELABEL(pmu_iwdg));
	zassert_true(device_is_ready(sec_dev), "sec_iwdg not ready");
	zassert_true(device_is_ready(pmu_dev), "pmu_iwdg not ready");
	return NULL;
}

static void wdt_boundary_teardown(void *data)
{
	ARG_UNUSED(data);
	/* 确保两个实例都被关闭，不影响后续用例 */
	wdt_disable(sec_dev);
	wdt_disable(pmu_dev);
}

/* ---- install_timeout: 窗口参数 ---- */

/* window.min != 0：驱动不支持窗口模式，一律 -EINVAL */
ZTEST(wdt_boundary, test_window_min_nonzero_rejected)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 1,
		.window.max = 1000,
	};
	int ret = wdt_install_timeout(sec_dev, &cfg);
	zassert_equal(ret, -EINVAL,
		      "install(window.min=1) returned %d, expected -EINVAL", ret);
}

/* window.min == window.max != 0：仍属窗口模式，-EINVAL */
ZTEST(wdt_boundary, test_window_min_equals_max_rejected)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 500,
		.window.max = 500,
	};
	int ret = wdt_install_timeout(sec_dev, &cfg);
	zassert_equal(ret, -EINVAL,
		      "install(min==max==500) returned %d, expected -EINVAL", ret);
}

/* window.max == 0：上界为 0 无意义，-EINVAL */
ZTEST(wdt_boundary, test_window_max_zero_rejected)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 0,
	};
	int ret = wdt_install_timeout(sec_dev, &cfg);
	zassert_equal(ret, -EINVAL,
		      "install(window.max=0) returned %d, expected -EINVAL", ret);
}

/* ---- install_timeout: flags × 实例匹配（HAL_IWDG_IS_MATCH） ---- */

/* sec_iwdg 属 HART 复位域，只接受 WDT_FLAG_RESET_CPU_CORE */
ZTEST(wdt_boundary, test_sec_iwdg_rejects_reset_soc_flag)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_SOC,
		.window.min = 0,
		.window.max = 1000,
	};
	int ret = wdt_install_timeout(sec_dev, &cfg);
	zassert_equal(ret, -EINVAL,
		      "sec_iwdg install(RESET_SOC) returned %d, expected -EINVAL", ret);
}

/* pmu_iwdg 属 SOC 复位域，只接受 WDT_FLAG_RESET_SOC */
ZTEST(wdt_boundary, test_pmu_iwdg_rejects_reset_cpu_core_flag)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 1000,
	};
	int ret = wdt_install_timeout(pmu_dev, &cfg);
	zassert_equal(ret, -EINVAL,
		      "pmu_iwdg install(RESET_CPU_CORE) returned %d, expected -EINVAL", ret);
}

/* flags = 0（WDT_FLAG_RESET_NONE）与未知位 0xFF 均不匹配任何实例 -> -EINVAL */
ZTEST(wdt_boundary, test_install_rejects_unmatched_flags)
{
	struct wdt_timeout_cfg cfg_none = {
		.flags = WDT_FLAG_RESET_NONE,
		.window.min = 0,
		.window.max = 1000,
	};
	struct wdt_timeout_cfg cfg_ff = {
		.flags = 0xFF,
		.window.min = 0,
		.window.max = 1000,
	};

	int ret_none = wdt_install_timeout(sec_dev, &cfg_none);
	zassert_equal(ret_none, -EINVAL,
		      "install(flags=0) returned %d, expected -EINVAL", ret_none);

	int ret_ff = wdt_install_timeout(sec_dev, &cfg_ff);
	zassert_equal(ret_ff, -EINVAL,
		      "install(flags=0xFF) returned %d, expected -EINVAL", ret_ff);
}

/* 各实例在自身支持的 flags 下 install 成功（返回非负 channel） */
ZTEST(wdt_boundary, test_install_accepted_on_supported_flags)
{
	struct wdt_timeout_cfg cfg_sec = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 60000,
	};
	struct wdt_timeout_cfg cfg_pmu = {
		.flags = WDT_FLAG_RESET_SOC,
		.window.min = 0,
		.window.max = 60000,
	};

	int ch_sec = wdt_install_timeout(sec_dev, &cfg_sec);
	zassert_true(ch_sec >= 0,
		     "sec_iwdg install(RESET_CPU_CORE) returned %d", ch_sec);

	int ch_pmu = wdt_install_timeout(pmu_dev, &cfg_pmu);
	zassert_true(ch_pmu >= 0,
		     "pmu_iwdg install(RESET_SOC) returned %d", ch_pmu);
}

/* ---- install_timeout: callback 注册 / 清除分支 ---- */

/* callback != NULL：驱动走 irq_config_func + data->callback 赋值分支 */
ZTEST(wdt_boundary, test_install_with_callback_accepted)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 60000,
		.callback = wdt_boundary_dummy_cb,
	};
	int ret = wdt_install_timeout(sec_dev, &cfg);
	zassert_equal(ret, 0,
		      "install(callback!=NULL) returned %d, expected 0", ret);

	/* 清除已注册的回调：避免遗留 irq_enable + 非空 data->callback 影响后续用例 */
	struct wdt_timeout_cfg cfg_clear = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 60000,
		.callback = NULL,
	};
	(void)wdt_install_timeout(sec_dev, &cfg_clear);
}

/* callback == NULL：驱动走 data->callback 置空分支 */
ZTEST(wdt_boundary, test_install_without_callback_accepted)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 60000,
		.callback = NULL,
	};
	int ret = wdt_install_timeout(sec_dev, &cfg);
	zassert_equal(ret, 0,
		      "install(callback==NULL) returned %d, expected 0", ret);
}

/* ---- setup 错误分支 ---- */

/* IWDT_LOAD == 0（未安装超时）-> -ENOTSUP */
ZTEST(wdt_boundary, test_setup_without_timeout_enotsup)
{
	/* 复现「未安装超时」前置态：CTRL=0 且 LOAD=0。
	 * 直接写寄存器使本项与套件内其它用例的执行顺序解耦。 */
	wdt_disable(sec_dev);
	SEC_IWDG_REGS[IWDG_LOAD_OFFSET] = 0;
	SEC_IWDG_REGS[IWDG_CTRL_OFFSET] = 0;

	int ret = wdt_setup(sec_dev, 0);
	zassert_equal(ret, -ENOTSUP,
		      "setup without install returned %d, expected -ENOTSUP", ret);
}

/* WDT_OPT_PAUSE_IN_SLEEP 不支持 -> -ENOTSUP（且不使能看门狗） */
ZTEST(wdt_boundary, test_setup_pause_in_sleep_enotsup)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 60000,
	};
	int ch = wdt_install_timeout(sec_dev, &cfg);
	zassert_true(ch >= 0, "install_timeout failed: %d", ch);

	int ret = wdt_setup(sec_dev, WDT_OPT_PAUSE_IN_SLEEP);
	zassert_equal(ret, -ENOTSUP,
		      "setup(PAUSE_IN_SLEEP) returned %d, expected -ENOTSUP", ret);
}

/* IWDT_CTRL != 0（已 setup）-> -EBUSY */
ZTEST(wdt_boundary, test_setup_twice_ebusy)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 60000,
	};
	int ch = wdt_install_timeout(sec_dev, &cfg);
	zassert_true(ch >= 0, "install_timeout failed: %d", ch);

	int ret = wdt_setup(sec_dev, 0);
	zassert_equal(ret, 0, "first setup returned %d, expected 0", ret);

	int ret2 = wdt_setup(sec_dev, 0);
	zassert_equal(ret2, -EBUSY,
		      "second setup returned %d, expected -EBUSY", ret2);

	wdt_disable(sec_dev);
}

/* ---- setup / feed / disable 正常通路 ---- */

/* WDT_OPT_PAUSE_HALTED_BY_DBG 被接受；feed/disable 返回 0 */
ZTEST(wdt_boundary, test_setup_pause_halted_by_dbg_feed_disable)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 60000,
	};
	int ch = wdt_install_timeout(sec_dev, &cfg);
	zassert_true(ch >= 0, "install_timeout failed: %d", ch);

	int ret = wdt_setup(sec_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
	zassert_equal(ret, 0,
		      "setup(PAUSE_HALTED_BY_DBG) returned %d, expected 0", ret);

	int feed_ret = wdt_feed(sec_dev, ch);
	zassert_equal(feed_ret, 0, "feed returned %d, expected 0", feed_ret);

	int dis_ret = wdt_disable(sec_dev);
	zassert_equal(dis_ret, 0, "disable returned %d, expected 0", dis_ret);
}

/* pmu_iwdg 功能通路：install(RESET_SOC) -> setup -> feed -> disable */
ZTEST(wdt_boundary, test_pmu_iwdg_lifecycle)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_SOC,
		.window.min = 0,
		.window.max = 60000,
	};
	int ch = wdt_install_timeout(pmu_dev, &cfg);
	zassert_true(ch >= 0, "pmu_iwdg install_timeout returned %d", ch);

	int ret = wdt_setup(pmu_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
	zassert_equal(ret, 0, "pmu_iwdg setup returned %d, expected 0", ret);

	int feed_ret = wdt_feed(pmu_dev, ch);
	zassert_equal(feed_ret, 0, "pmu_iwdg feed returned %d, expected 0", feed_ret);

	int dis_ret = wdt_disable(pmu_dev);
	zassert_equal(dis_ret, 0, "pmu_iwdg disable returned %d, expected 0", dis_ret);
}

/* ---- timeout_ms -> IWDT_LOAD 换算（边界） ---- */

/* 最小有效超时 1 ms */
ZTEST(wdt_boundary, test_load_ticks_min_timeout)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 1,
	};
	int ch = wdt_install_timeout(sec_dev, &cfg);
	zassert_true(ch >= 0, "install_timeout failed: %d", ch);

	uint32_t load = SEC_IWDG_REGS[IWDG_LOAD_OFFSET];
	ZT_INFO("test_load_ticks_min_timeout", "ms=1,ticks=%u", load);
	zassert_equal(load, TICKS_1MS,
		      "IWDT_LOAD=%u for max=1ms, expected %u", load, TICKS_1MS);
}

/* 常用值 1000 ms */
ZTEST(wdt_boundary, test_load_ticks_1000ms)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 1000,
	};
	int ch = wdt_install_timeout(sec_dev, &cfg);
	zassert_true(ch >= 0, "install_timeout failed: %d", ch);

	uint32_t load = SEC_IWDG_REGS[IWDG_LOAD_OFFSET];
	ZT_INFO("test_load_ticks_1000ms", "ms=1000,ticks=%u", load);
	zassert_equal(load, TICKS_1000MS,
		      "IWDT_LOAD=%u for max=1000ms, expected %u", load, TICKS_1000MS);
}

/*
 * 饱和边界下侧：131071999 ms —— 64 位算式 131071999*32768/1000 = 4294967263，
 * 未超过 UINT32_MAX，故 IWDT_LOAD = 4294967263。
 * 2026-09-18 驱动改为 64 位乘法后改判（原值 4294934 是按 32 位回绕语义手算的）。
 */
ZTEST(wdt_boundary, test_load_ticks_just_below_saturation)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 131071999u,
	};
	int ch = wdt_install_timeout(sec_dev, &cfg);
	zassert_true(ch >= 0, "install_timeout failed: %d", ch);

	uint32_t load = SEC_IWDG_REGS[IWDG_LOAD_OFFSET];
	ZT_INFO("test_load_ticks_just_below_saturation", "ms=131071999,ticks=%u", load);
	zassert_equal(load, TICKS_131071999MS,
		      "IWDT_LOAD=%u for max=131071999ms, expected %u",
		      load, TICKS_131071999MS);
}

/*
 * 饱和边界：131072000 ms —— 64 位算式 131072000*32768/1000 = 2^32 > UINT32_MAX，
 * 触发饱和，故 IWDT_LOAD = 0xFFFFFFFF（本意 = 钳到硬件寄存器最大值）。
 * 2026-09-18 驱动改为 64 位乘法后改判（原值 0 是按 32 位回绕语义手算的）。
 */
ZTEST(wdt_boundary, test_load_ticks_saturation_boundary)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 131072000u,
	};
	int ch = wdt_install_timeout(sec_dev, &cfg);
	zassert_true(ch >= 0, "install_timeout failed: %d", ch);

	uint32_t load = SEC_IWDG_REGS[IWDG_LOAD_OFFSET];
	ZT_INFO("test_load_ticks_saturation_boundary", "ms=131072000,ticks=%u", load);
	zassert_equal(load, TICKS_131072000MS,
		      "IWDT_LOAD=%u for max=131072000ms, expected %u",
		      load, TICKS_131072000MS);
}

/*
 * 饱和区上界：window.max 取 uint32 上限 0xFFFFFFFF ms —— 64 位算式远大于 UINT32_MAX，
 * 触发饱和，故 IWDT_LOAD = 0xFFFFFFFF（与 131072000 ms 同值）。
 * 2026-09-18 驱动改为 64 位乘法后改判（原值 4294934 是按 32 位回绕语义手算的）。
 */
ZTEST(wdt_boundary, test_load_ticks_uint32_max_timeout)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = 0xFFFFFFFFu,
	};
	int ch = wdt_install_timeout(sec_dev, &cfg);
	zassert_true(ch >= 0, "install_timeout failed: %d", ch);

	uint32_t load = SEC_IWDG_REGS[IWDG_LOAD_OFFSET];
	ZT_INFO("test_load_ticks_uint32_max_timeout", "ms=4294967295,ticks=%u", load);
	zassert_equal(load, TICKS_UINT32_MAX_MS,
		      "IWDT_LOAD=%u for max=UINT32_MAX ms, expected %u",
		      load, TICKS_UINT32_MAX_MS);
}

ZTEST_SUITE(wdt_boundary, NULL, wdt_boundary_setup, NULL, NULL, wdt_boundary_teardown);
