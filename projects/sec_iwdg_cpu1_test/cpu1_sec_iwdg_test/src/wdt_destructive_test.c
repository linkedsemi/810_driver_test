/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * WDT destructive reset test - 触发 sec_iwdg 硬件复位
 *
 * 独立 main，不走 ztest（流程不是独立测试项而是状态机）。
 *
 * 流程：
 *   Phase 0 (第一次启动): 设置 2000ms 超时，不喂狗，等复位
 *   Phase 1 (复位后启动): 检查 reset_reason == SEC_IWDT_HART_RESET，
 *                         打印 PASS/FAIL + PROJECT EXECUTION 裁决行
 *   Phase 2 (上一轮遗留): 本次启动没装狗、没复位、没校验 ⇒ 不打裁决；
 *                         清 noinit 状态后按 Phase 0 重跑一轮完整流程
 *
 * noinit 跨复位保留既是本状态机的基础，也意味着重复 load/run 会读到上一轮
 * 的终态（GDB load 不写 NOLOAD 段）。故"测试通过"只能由本轮真跑出来的
 * Phase 1 宣布。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/reboot.h>
#include <errno.h>
#include <string.h>

#include "soc_reset.h"

/* noinit 段跨复位保留 */
__noinit static uint32_t test_phase;  /* 0=未跑, 1=已跑(等复位), 2=复位后验证 */
__noinit static uint32_t phase0_passed;  /* 第一阶段是否执行完 */

#define WDT_NODE DT_NODELABEL(sec_iwdg)
#define WDT_TIMEOUT_MS 2000

static bool wdt_is_watchdog_reset(uint32_t reason);
static const char *wdt_reset_reason_name(uint32_t reason);
static void wdt_phase1_verdict(uint32_t reason);

void wdt_destructive_main(void)
{
	printk("\n*** WDT destructive reset test ***\n");
	printk("Reset reason: %u\n", reset_reason_get());
	printk("test_phase: %u, phase0_passed: %u\n", test_phase, phase0_passed);

	/* Phase 2: noinit 里是上一轮留下的终态（或上电随机值）。本次启动尚未
	 * 装狗、未复位、未校验 ⇒ 此处不产生任何裁决行；清状态后落到下面的
	 * Phase 0 重新走完整流程，裁决由那一轮的 Phase 1 给出。 */
	if (test_phase != 0 && test_phase != 1) {
		printk("PHASE 2: stale noinit state (test_phase=%u), no test ran this "
		       "boot; clearing and restarting from PHASE 0\n", test_phase);
		test_phase = 0;
		phase0_passed = 0;
	}

phase0_restart:
	if (test_phase == 0) {
		/* Phase 0: 设置超时，不喂狗，等复位 */
		printk("PHASE 0: install timeout, will NOT feed, expecting reset in %d ms\n",
		       WDT_TIMEOUT_MS);

		const struct device *wdt_dev = DEVICE_DT_GET(WDT_NODE);
		if (!device_is_ready(wdt_dev)) {
			printk("ERROR: sec_iwdg not ready\n");
			printk("PROJECT EXECUTION: FAILED\n");
			k_sleep(K_FOREVER);
		}

		struct wdt_timeout_cfg cfg = {
			.flags = WDT_FLAG_RESET_CPU_CORE,
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

		int ret = wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
		if (ret != 0) {
			printk("ERROR: wdt_setup failed: %d\n", ret);
			printk("PROJECT EXECUTION: FAILED\n");
			k_sleep(K_FOREVER);
		}
		printk("wdt_setup OK\n");

		test_phase = 1;
		phase0_passed = 1;
		printk("PHASE 0 complete, waiting for watchdog reset...\n");

		/* 不喂狗，等 sec_iwdg 超时复位 */
		k_sleep(K_FOREVER);

		/* 正常情况下不会执行到这里 */
		printk("ERROR: wdt did not reset after timeout!\n");
		printk("PROJECT EXECUTION: FAILED\n");
		k_sleep(K_FOREVER);
	} else {
		/* test_phase == 1：上一轮 Phase 0 已装狗等复位。
		 *
		 * 但 noinit 段跨"重烧 + 复位"存活（GDB/烧录器不写 NOLOAD 段），
		 * 所以 phase==1 也可能是**上一次会话**留下的。只有"复位来源确实是看门狗"
		 * 才证明本轮真的走完了 Phase 0；其它来源（重烧、上电、调试器复位）
		 * 说明本轮没跑过测试 —— 清状态重来，绝不打裁决。
		 *
		 * 实测 2026-10-28：没有这个检查时，重烧后首次调用会拿烧录复位原因
		 * 去比对，打出假的 `FAIL: reset_reason == 3`，随后 k_sleep(K_FOREVER)
		 * 把 shell 线程永久挂死（后续 `itest reset` 采集 120 s 零字节）。 */
		uint32_t reason = reset_reason_get();

		if (!wdt_is_watchdog_reset(reason)) {
			printk("STALE: phase==1 but reset_reason=%u (%s) is not a "
			       "watchdog reset; no test ran this boot, restarting "
			       "from PHASE 0\n", reason, wdt_reset_reason_name(reason));
			test_phase = 0;
			phase0_passed = 0;
			goto phase0_restart;
		} else {
			/* Phase 1: 复位后检查 reset_reason */
			printk("PHASE 1: after reset, checking reset_reason\n");
			wdt_phase1_verdict(reason);
		}
	}
}

/* 复位来源是否为看门狗（本文件用到的三种）。 */
static bool wdt_is_watchdog_reset(uint32_t reason)
{
	return reason == SEC_IWDT_HART_RESET ||
	       reason == SEC_WWDT_HART_RESET ||
	       reason == SYS_IWDT_FULL_RESET;
}

static const char *wdt_reset_reason_name(uint32_t reason)
{
	switch (reason) {
	case NO_RESET_REASON:     return "NO_RESET_REASON";
	case PWR_FULL_RESET:      return "PWR_FULL_RESET";
	case SOFT_FULL_RESET:     return "SOFT_FULL_RESET";
	case CPU_FULL_RESET:      return "CPU_FULL_RESET";
	case SYS_IWDT_FULL_RESET: return "SYS_IWDT_FULL_RESET";
	case EXT_FULL_RESET:      return "EXT_FULL_RESET";
	case SEC_IWDT_HART_RESET: return "SEC_IWDT_HART_RESET";
	case SEC_WWDT_HART_RESET: return "SEC_WWDT_HART_RESET";
	case SOFT_HART_RESET:     return "SOFT_HART_RESET";
	default:                  return "UNKNOWN";
	}
}

static void wdt_phase1_verdict(uint32_t reason)
{
	/* sec_iwdg 用 WDT_FLAG_RESET_CPU_CORE → 触发 HART 复位
	 * （SEC_IWDT_HART_RESET=6），不是 FULL reset（SYS_IWDT=4）。 */
	bool ok = (reason == SEC_IWDT_HART_RESET);

	if (ok) {
		printk("PASS: reset_reason == SEC_IWDT_HART_RESET (expected)\n");
	} else {
		printk("FAIL: reset_reason == %u, expected SEC_IWDT_HART_RESET (%d)\n",
		       reason, SEC_IWDT_HART_RESET);
	}
	test_phase = 2;
	printk("PROJECT EXECUTION: %s\n", ok ? "SUCCESSFUL" : "FAILED");
	k_sleep(K_FOREVER);
}
