/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * app_iwdg_cpu2_test - cpu1 侧入口（2os TARGET 核，常驻监督）
 *
 * 职责边界：cpu1 是 2os 的引导核，负责释放 cpu2 并在 cpu2 被 app_iwdg
 * 触发 HART 复位期间保持在线。它不是本案被测对象（app_iwdg 的复位原因
 * 由 cpu2 侧 reset_reason_init() 的 SYSC_APP_PER 分支产生，cpu1 读不到）。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_iwdg_test_protocol.h"
#include "ztest_glue.h"
#include "rlog.h"
#include "test_results.h"

LOG_MODULE_REGISTER(app_iwdg_cpu1_main, LOG_LEVEL_INF);

void test_main(void)
{
	/* 单出口（SPEC_REFERENCE §6.9.3/§6.11.5）：本核是**从核**（ztest 型）⇒
	 * 不往自己的串口打 token。ztest 的逐项输出经 `tc_util_user_override.h`
	 * 把 PRINT_DATA 指向 RLOG 自动换出口；BOOT/INFO 走公共宏的 printk 路径
	 * （不经 PRINT_DATA）⇒ 此处显式换出口。本核 LOG / panic / assert 现场
	 * 仍走本核串口。 */
	rlog_secondary_init();

	LOG_INF("[CPU1 APP_IWDG] resident supervisor entry");
	RLOG_ZT_BOOT("TGT");
	zt_run_all();

	/* 本核结论（与主核同源：zt_run_all 按 ztest stats 记入 test_results[]）。
	 * DONE 帧是**本核发出的最后一条**：主核等到它才打自己的 PROJECT
	 * EXECUTION（§6.9.3 硬约束：完成标记必须是整条流最后一个）。 */
	bool any_fail = false;

	for (int i = 0; i < TR_MAX_COUNT; i++) {
		if (test_results[i] == TR_FAIL) {
			any_fail = true;
			break;
		}
	}
	rlog_emit_done(!any_fail, any_fail ? "PROJECT EXECUTION FAILED"
					   : "PROJECT EXECUTION SUCCESSFUL");

	/* 2os 引导核常驻：不得返回（SPEC_REFERENCE §6.1.1） */
	k_sleep(K_FOREVER);
}
