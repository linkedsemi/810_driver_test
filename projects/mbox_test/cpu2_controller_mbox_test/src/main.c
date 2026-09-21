/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * MBOX test - cpu2 (controller) entry point
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "ztest_glue.h"
#include "mbox_test_report.h"

extern void mbox_controller_main(void);
#if defined(CONFIG_MBOX_TEST_CASE_ID) && (CONFIG_MBOX_TEST_CASE_ID > 0)
/* 扩展 case 入口（P1c 追加）：实现见 src/mbox_cases.c，
 * 仅当本目录 Kconfig 的 MBOX_TEST_CASE_ID != 0 时编入。 */
extern void mbox_cases_controller_main(void);
#endif

LOG_MODULE_REGISTER(mbox_ctrl_main, LOG_LEVEL_INF);

void test_main(void)
{
	LOG_INF("[CPU2 MBOX] MBOX controller entry");
	zt_init("CTRL");
	mbox_report_init("CTRL");

#if defined(CONFIG_MBOX_TEST_CASE_ID) && (CONFIG_MBOX_TEST_CASE_ID > 0)
	mbox_cases_controller_main();
#else
	mbox_controller_main();
#endif

	/* ★ 按实际判定结果打印 —— 修复前此处**无条件**打 SUCCESSFUL，
	 * 驱动整条失效（send 全失败 / 对端一条收不到）时两核仍打 SUCCESS，
	 * case 被判假 PASS。判定状态由 tests/src/mbox_test_report.c 维护：
	 * 任一项 mbox_item_fail、或 mbox_mark_fail ⇒ 本核判负 ⇒ 打 FAILED。
	 * 调用点在 k_sleep(K_FOREVER) 之前，test_main 依旧不返回（§8.1）。 */
	mbox_print_verdict();

	k_sleep(K_FOREVER);
}
