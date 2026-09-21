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

/*
 * 白盒回读 sec_iwdg 寄存器块（与同工程 case_boundary 的
 * wdt_boundary_test.c:35 同款做法）：
 *   IWDT_LOAD / IWDT_CTRL 在偏移 0x0 / 0x4
 *   （modules/hal/linkedsemi/hal_driver/inc/reg_iwdgv2_type.h:5-8）。
 * 驱动 install_timeout 只写不读、disable 只写 IWDT_CTRL=0，API 层拿不到
 * 这两项的落地结果，故这里直接回读寄存器做判别性断言。
 * sec_iwdg clock-frequency = 32768（zephyr/dts/riscv/linkedsemi/lsqsh.dtsi:590）。
 */
#define SEC_IWDG_REGS ((volatile uint32_t *)DT_REG_ADDR(DT_NODELABEL(sec_iwdg)))
#define IWDG_LOAD_OFFSET 0u
#define IWDG_CTRL_OFFSET 1u

/* iwdt_calculate_load 的 64 位算式（wdt_iwdg_ls.c:43-51）：ms * 32768 / 1000 */
#define WDT_TICKS_5000MS  163840u   /*  5000 * 32768 / 1000 */
#define WDT_TICKS_10000MS 327680u   /* 10000 * 32768 / 1000 */

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

	/* 驱动在 HAL_IWDG_IS_MATCH(reg, flags) == 0 时返回 -EINVAL
	 * （wdt_iwdg_ls.c:108-112）。HAL_IWDG_IS_MATCH 只认
	 * WDT_FLAG_RESET_CPU_CORE(1) / WDT_FLAG_RESET_SOC(2)，其余一律返回 0
	 * （modules/hal/linkedsemi/soc/rv32/qsh/ls_msp_iwdg.c:72-81）
	 * ⇒ 0xFF 两者皆非 ⇒ 必返回 -EINVAL。
	 * 驱动正确时必过；驱动若把未识别 flags 静默接受（返回 >=0）则必不过。 */
	zassert_equal(ret, -EINVAL,
		"install(flags=0xFF) returned %d, expected -EINVAL", ret);
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

	/* disable 生效判据：驱动 iwdt_ls_disable 把 IWDT_CTRL 写 0
	 * （wdt_iwdg_ls.c:131-136），回读必为 0。
	 * 驱动正确时必过；disable 未真正清 CTRL（例如漏写或写错实例）时必不过。
	 * （原用例只靠"1s 内没复位"隐式判定：若 disable 没生效但超时窗口早已错过，
	 *  同样不会复位 ⇒ 判别力弱，补此直接判据。） */
	zassert_equal(SEC_IWDG_REGS[IWDG_CTRL_OFFSET], 0u,
		      "IWDT_CTRL must read 0 right after wdt_disable()");

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
	zassert_true(ch2 >= 0, "second install_timeout failed: %d", ch2);

	/* 驱动没有"通道数"概念：iwdt_ls_install_timeout 每次都对同一个
	 * IWDT_LOAD 覆盖写新 cfg 的 ticks（wdt_iwdg_ls.c:122-126），第二次
	 * 安装必返回 0（channel 0）。可测的单通道不变式：
	 *   第二次安装后 IWDT_LOAD == ticks(5000ms) = 5000*32768/1000 = 163840，
	 *   且不再是第一次的 ticks(10000ms) = 327680。
	 * 驱动正确时必过；驱动若把第二次安装拒绝（返回负）或未覆盖装载值
	 * （仍留 10000ms 的 327680）则必不过。 */
	uint32_t load = SEC_IWDG_REGS[IWDG_LOAD_OFFSET];
	zassert_equal(load, WDT_TICKS_5000MS,
		      "IWDT_LOAD=%u after second install(max=5000ms), expected %u "
		      "(单通道覆盖；first install max=10000ms -> %u)",
		      load, WDT_TICKS_5000MS, WDT_TICKS_10000MS);

	wdt_disable(wdt_dev);
}

ZTEST_SUITE(wdt, NULL, wdt_test_setup, NULL, wdt_test_teardown, NULL);
