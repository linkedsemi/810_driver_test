/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * MBOX test - cpu1 (target) entry point
 * mbox 测试不走 ztest（双核同步复杂），直接跑测试逻辑。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "ztest_glue.h"
#include "mbox_test_report.h"

extern void mbox_target_main(void);
#if defined(CONFIG_MBOX_TEST_CASE_ID) && (CONFIG_MBOX_TEST_CASE_ID > 0)
/* 扩展 case 入口（P1c 追加）：实现见 src/mbox_cases.c，
 * 仅当本目录 Kconfig 的 MBOX_TEST_CASE_ID != 0 时编入。 */
extern void mbox_cases_target_main(void);
#endif

LOG_MODULE_REGISTER(mbox_tgt_main, LOG_LEVEL_INF);

void test_main(void)
{
	LOG_INF("[CPU1 MBOX] MBOX test entry");
	zt_init("TGT");
	mbox_report_init("TGT");

#if defined(CONFIG_MBOX_TEST_CASE_ID) && (CONFIG_MBOX_TEST_CASE_ID > 0)
	mbox_cases_target_main();
#else
	mbox_target_main();
#endif

	/* ★ 按实际判定结果打印 —— 修复前此处**无条件**打 SUCCESSFUL。
	 * 本核的判定状态由 tests/src/mbox_test_report.c 维护（target 侧
	 * 收齐帧数 / payload 校验失败都会置负）。 */
	mbox_print_verdict();

	/* 不返回（§8.1） */
	k_sleep(K_FOREVER);
}
