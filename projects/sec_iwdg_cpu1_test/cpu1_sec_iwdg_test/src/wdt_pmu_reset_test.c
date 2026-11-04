/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * WDT pmu_iwdg full-reset test - 触发 pmu_iwdg 整芯片复位
 *
 * 独立 main，不走 ztest（流程是跨复位状态机，不是独立测试项）。
 *
 * sec_iwdg 与 pmu_iwdg 是两条独立复位路径：
 *   sec_iwdg + WDT_FLAG_RESET_CPU_CORE -> SEC_IWDT_HART_RESET（只复位本 hart）
 *   pmu_iwdg + WDT_FLAG_RESET_SOC      -> SYS_IWDT_FULL_RESET（整芯片复位）
 *
 * 流程：
 *   Phase 0（第一次启动）: 装 2s 超时，不喂狗，等整芯片复位
 *   Phase 1（复位后启动）: 校验 reset_reason == SYS_IWDT_FULL_RESET
 *   Phase 2（上一轮遗留）: 本次启动没装狗、没复位、没校验 ⇒ 不打裁决；
 *                         清 noinit 状态后按 Phase 0 重跑一轮完整流程
 *
 * noinit 跨复位保留既是本状态机的基础，也意味着重复 load/run 会读到上一轮
 * 的终态（GDB load 不写 NOLOAD 段）。故"测试通过"只能由本轮真跑出来的
 * Phase 1 宣布。
 *
 * ⚠ 本 case 会整芯片复位（连带另一核与全部外设），必须单独运行，
 *   不进批量回归（与 case_destructive_reset 同理）。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <errno.h>

#include "soc_reset.h"

/* noinit 段跨复位保留 */
__noinit static uint32_t pmu_test_phase;   /* 0=未跑, 1=已装超时等复位, 2=复位后已校验 */
__noinit static uint32_t pmu_phase0_passed;

#define WDT_NODE DT_NODELABEL(pmu_iwdg)
#define WDT_TIMEOUT_MS 2000

void wdt_pmu_reset_main(void)
{
	printk("\n*** WDT pmu_iwdg full-reset test ***\n");
	printk("Reset reason: %u\n", reset_reason_get());
	printk("pmu_test_phase: %u, pmu_phase0_passed: %u\n",
	       pmu_test_phase, pmu_phase0_passed);

	/* Phase 2: noinit 里是上一轮留下的终态（或上电随机值）。本次启动尚未
	 * 装狗、未复位、未校验 ⇒ 此处不产生任何裁决行；清状态后落到下面的
	 * Phase 0 重新走完整流程，裁决由那一轮的 Phase 1 给出。 */
	if (pmu_test_phase != 0 && pmu_test_phase != 1) {
		printk("PHASE 2: stale noinit state (pmu_test_phase=%u), no test ran this "
		       "boot; clearing and restarting from PHASE 0\n", pmu_test_phase);
		pmu_test_phase = 0;
		pmu_phase0_passed = 0;
	}

phase0_restart:
	if (pmu_test_phase == 0) {
		/* Phase 0: 装超时，不喂狗，等整芯片复位 */
		printk("PHASE 0: install pmu_iwdg timeout (SOC reset), will NOT feed, "
		       "expecting full reset in %d ms\n", WDT_TIMEOUT_MS);

		const struct device *wdt_dev = DEVICE_DT_GET(WDT_NODE);
		if (!device_is_ready(wdt_dev)) {
			printk("ERROR: pmu_iwdg not ready\n");
			printk("PROJECT EXECUTION: FAILED\n");
			k_sleep(K_FOREVER);
		}

		struct wdt_timeout_cfg cfg = {
			.flags = WDT_FLAG_RESET_SOC,
			.window.min = 0,
			.window.max = WDT_TIMEOUT_MS,
		};
		int channel = wdt_install_timeout(wdt_dev, &cfg);
		if (channel < 0) {
			printk("ERROR: install_timeout failed: %d\n", channel);
			printk("PROJECT EXECUTION: FAILED\n");
			k_sleep(K_FOREVER);
		}
		printk("channel: %d\n", channel);

		int ret = wdt_setup(wdt_dev, 0);
		if (ret != 0) {
			printk("ERROR: wdt_setup failed: %d\n", ret);
			printk("PROJECT EXECUTION: FAILED\n");
			k_sleep(K_FOREVER);
		}
		printk("wdt_setup OK\n");

		pmu_phase0_passed = 1;
		pmu_test_phase = 1;
		printk("PHASE 0 complete, waiting for pmu_iwdg full reset...\n");

		/* 不喂狗，等 pmu_iwdg 超时整芯片复位 */
		k_sleep(K_FOREVER);

		/* 正常情况下不会执行到这里 */
		printk("ERROR: pmu_iwdg did not reset after timeout!\n");
		printk("PROJECT EXECUTION: FAILED\n");
		k_sleep(K_FOREVER);
	} else {
		/* pmu_test_phase == 1：上一轮 Phase 0 已装狗等复位。
		 *
		 * noinit 段跨"重烧 + 复位"存活，phase==1 也可能是上一次会话留下的。
		 * 只有"复位来源是看门狗"才证明本轮真的走完了 Phase 0；否则清状态重来，
		 * 不打裁决（否则重烧后会拿烧录复位原因比对，打出假 FAIL 并挂死 shell）。 */
		uint32_t reason = reset_reason_get();

		if (reason != SYS_IWDT_FULL_RESET &&
		    reason != SEC_IWDT_HART_RESET &&
		    reason != SEC_WWDT_HART_RESET) {
			printk("STALE: pmu_test_phase==1 but reset_reason=%u is not a "
			       "watchdog reset; no test ran this boot, restarting "
			       "from PHASE 0\n", reason);
			pmu_test_phase = 0;
			pmu_phase0_passed = 0;
			goto phase0_restart;
		}

		/* Phase 1: 复位后校验复位原因 */
		printk("PHASE 1: after reset, checking reset_reason\n");
		bool ok = (reason == SYS_IWDT_FULL_RESET);

		if (ok) {
			printk("PASS: reset_reason == SYS_IWDT_FULL_RESET (expected)\n");
			printk("PROJECT EXECUTION: SUCCESSFUL\n");
		} else {
			printk("FAIL: reset_reason == %u, expected SYS_IWDT_FULL_RESET (%d)\n",
			       reason, SYS_IWDT_FULL_RESET);
			printk("PROJECT EXECUTION: FAILED\n");
		}
		pmu_test_phase = 2;
		k_sleep(K_FOREVER);
	}
}
