/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * app_iwdg 复位用例（cpu2，case_reset = CASE_ID 3）——破坏性
 *
 * 独立 main，不走 ztest（流程不是独立测试项而是状态机）。
 * 状态用 __noinit 跨复位保留；复位把控制权交回本固件入口后按 test_phase 推进。
 *
 * 流程：
 *   Phase 0（首次启动）：install(2000ms, RESET_CPU_CORE) + setup，不喂狗，
 *                        等待 app_iwdg 超时触发 **cpu2 HART 复位**
 *   Phase 1（复位后启动）：验证 reset_reason_get() == APP_IWDT_HART_RESET
 *                        （由 soc_reset_reason.c 的 SYSC_APP_PER 分支产生——
 *                          该分支只在 cpu1 节点非 okay 的 cpu2 构建里编译）
 *   Phase 2（上一轮遗留）：本次启动未装狗、未复位、未校验 ⇒ 不打任何判定标记；
 *                        清 noinit 状态后按 Phase 0 重跑一轮完整流程
 *
 * noinit 跨复位保留既是本状态机的基础，也意味着重复 load/run 会读到上一轮
 * 的终态（GDB load 不写 NOLOAD 段）。故 PASS 标记只能由本轮真跑出来的
 * Phase 1 打出。
 *
 * 注意：本 case 会真实复位 cpu2。cpu1 不在 APP_IWDT 的复位域内，应由 cpu1
 * 侧常驻固件保持在线；本 case **不可进批量回归**（见 tests/COVERAGE.md）。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <errno.h>

#include "soc_reset.h"
#include "app_iwdg_test_protocol.h"
#if defined(ITEST_PEER_SECONDARY)
#include "rlog.h"
#endif

/* noinit 段跨 HART 复位保留（RAM 不被 hart 复位清空） */
__noinit static uint32_t test_phase;     /* 0=未跑, 1=已跑(等复位), 2=复位后验证 */
__noinit static uint32_t phase0_passed;  /* 第一阶段是否执行完 */

#define WDT_NODE DT_NODELABEL(app_iwdg)
#define WDT_TIMEOUT_MS 2000

void app_iwdg_reset_main(void)
{
	printk("\n*** app_iwdg (cpu2) HART reset test ***\n");
	printk("Reset reason: %u\n", reset_reason_get());
	printk("test_phase: %u, phase0_passed: %u\n", test_phase, phase0_passed);

	/* Phase 2：noinit 里是上一轮留下的终态（或上电随机值）。本次启动尚未
	 * 装狗、未复位、未校验 ⇒ 此处不打任何判定标记；清状态后落到下面的
	 * Phase 0 重新走完整流程，判定标记由那一轮的 Phase 1 打出。 */
	if (test_phase != 0 && test_phase != 1) {
		printk("PHASE 2: stale noinit state (test_phase=%u), no test ran this "
		       "boot; clearing and restarting from PHASE 0\n", test_phase);
		test_phase = 0;
		phase0_passed = 0;
	}

phase0_restart:
	if (test_phase == 0) {
		printk("PHASE 0: install %d ms timeout, will NOT feed, expecting HART reset\n",
		       WDT_TIMEOUT_MS);
#if defined(ITEST_PEER_SECONDARY)
		/* The integrated image keeps cpu2's console off the operator's view;
		 * relay the arming line so "the reset really happened" is visible on
		 * cpu1 too, not just as a verdict after the fact. */
		rlog_emitf("[itest-cpu2] app_iwdg: arming %d ms, expecting HART reset",
			   WDT_TIMEOUT_MS);
#endif

		const struct device *wdt_dev = DEVICE_DT_GET(WDT_NODE);

		if (!device_is_ready(wdt_dev)) {
			printk("ERROR: app_iwdg not ready\n");
			printk("PROJECT EXECUTION FAILED\n");
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
			printk("PROJECT EXECUTION FAILED\n");
			k_sleep(K_FOREVER);
		}

		int ret = wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);

		if (ret != 0) {
			printk("ERROR: wdt_setup failed: %d\n", ret);
			printk("PROJECT EXECUTION FAILED\n");
			k_sleep(K_FOREVER);
		}

		test_phase = 1;
		phase0_passed = 1;
		printk("PHASE 0 complete, waiting for app_iwdg reset...\n");

		/* 不喂狗，等 app_iwdg 超时复位本 hart */
		k_sleep(K_FOREVER);

		/* 正常情况下不会执行到这里 */
		printk("ERROR: app_iwdg did not reset after timeout!\n");
		printk("PROJECT EXECUTION FAILED\n");
		k_sleep(K_FOREVER);
	} else {
		/* test_phase == 1：上一轮 Phase 0 已装狗等复位。
		 *
		 * noinit 段跨"重烧 + 复位"存活（cpu2 是 RAM 运行，重烧后 RAM 可能
		 * 保留上一轮的值），所以 phase==1 也可能是上一次会话留下的。
		 * 只有"复位来源是看门狗"才证明本轮真的走完了 Phase 0；否则清状态
		 * 重来，绝不打裁决（否则重烧后会拿烧录复位原因比对，打出假 FAIL
		 * 并 k_sleep 挂死调用线程）。 */
		uint32_t reason = reset_reason_get();

		if (reason != APP_IWDT_HART_RESET &&
		    reason != APP_WWDT_HART_RESET &&
		    reason != SEC_IWDT_HART_RESET &&
		    reason != SYS_IWDT_FULL_RESET) {
			printk("STALE: phase==1 but reset_reason=%u is not a "
			       "watchdog reset; no test ran this boot, restarting "
			       "from PHASE 0\n", reason);
			test_phase = 0;
			phase0_passed = 0;
			goto phase0_restart;
		}

		bool ok = (reason == APP_IWDT_HART_RESET);

		printk("PHASE 1: after reset, reason=%u, expected APP_IWDT_HART_RESET(%d)\n",
		       reason, APP_IWDT_HART_RESET);
		test_phase = 2;

		if (ok) {
			printk(APP_IWDG_RESET_PASS_MARK
			       ": reset_reason == APP_IWDT_HART_RESET\n");
			printk("PROJECT EXECUTION SUCCESSFUL\n");
		} else {
			printk(APP_IWDG_RESET_FAIL_MARK
			       ": reset_reason=%u, expected %d\n",
			       reason, APP_IWDT_HART_RESET);
			printk("PROJECT EXECUTION FAILED\n");
		}
		k_sleep(K_FOREVER);
	}
}
