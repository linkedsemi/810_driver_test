/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * app_iwdg_cpu2_test - cpu2 (controller) side entry point
 *
 * case 由本目录 Kconfig 的 APP_IWDG_TEST_CASE_ID 选择（见 CMakeLists.txt）：
 *   0..2 = ztest 用例（非破坏性）
 *   3    = app_iwdg HART 复位状态机（破坏性，永不返回）
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_iwdg_test_protocol.h"
#include "ztest_glue.h"
#include "rlog.h"
#include "test_results.h"

LOG_MODULE_REGISTER(app_iwdg_cpu2_main, LOG_LEVEL_INF);

#if defined(CONFIG_APP_IWDG_TEST_CASE_ID) && (CONFIG_APP_IWDG_TEST_CASE_ID == 3)
extern void app_iwdg_reset_main(void);
#endif

void test_main(void)
{
#if defined(CONFIG_APP_IWDG_TEST_CASE_ID) && (CONFIG_APP_IWDG_TEST_CASE_ID == 3)
	/* 破坏性复位状态机：内部 k_sleep(K_FOREVER)，不返回 */
	app_iwdg_reset_main();
#else
	LOG_INF("[CPU2 APP_IWDG] app_iwdg case %d entry",
		(int)CONFIG_APP_IWDG_TEST_CASE_ID);
	zt_init("CTRL");

	/* 单出口（SPEC_REFERENCE §6.9.3）：本核是主核（唯一串口出口），收下从核
	 * 经 RLOG 送来的 token 行，加**核信封**原样转发。信封是分拣凭据
	 * （§6.9.4）：采集侧按 `<前缀>:TGT:` 把它们搬回 serial_TGT.log。 */
	rlog_primary_init();
	rlog_set_relay_envelope(APP_IWDG_TEST_PREFIX, "TGT");

	zt_run_all();

	/* 等从核 DONE（期间反复 drain，给从核 FIFO/环腾空间）。fail-closed
	 * （§6.9.3）：没等到 DONE、或接收侧跳号/坏帧/缓冲满 ⇒ 判失败 —— 绝不把
	 * "没收到从核结果"当成"通过"。 */
	bool sec_ok = rlog_wait_secondary_done(RLOG_TX_TIMEOUT_MS);
	bool any_fail = !sec_ok || (rlog_lost() != 0U);

	if (!sec_ok) {
		printk("[CTRL] secondary check FAILED: %s lost=%u\n",
		       rlog_secondary_silent() ? "SECONDARY_SILENT"
					       : "SECONDARY_INCOMPLETE",
		       (unsigned)rlog_lost());
	}
	for (int i = 0; i < TR_MAX_COUNT; i++) {
		if (test_results[i] == TR_FAIL) {
			any_fail = true;
			break;
		}
	}
	printk("PROJECT EXECUTION %s\n", any_fail ? "FAILED" : "SUCCESSFUL");

	/* cpu2 是被测量核：套件结束后保持在线（不返回，§6.1.1）。
	 * 不返回也保证 ztest 强 main() 的 TC_END_REPORT 不会在本核**再补**一条
	 * 晚于从核 DONE 的收尾标记（§6.9.3 硬约束）。 */
	k_sleep(K_FOREVER);
#endif
}
