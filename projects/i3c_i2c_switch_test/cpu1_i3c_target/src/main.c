/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i3c_i2c_target_test.h"
#include "i3c_i2c_switch_test_protocol.h"
#include "test_results.h"
#include "ztest_glue.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i3c_i2c_target_test, LOG_LEVEL_INF);

#ifndef CONFIG_I3C_I2C_PURE_I2C_TEST
const struct device *tgt_i3c_dev = DEVICE_DT_GET(DT_NODELABEL(i3c6));
#endif
const struct device *tgt_i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c6));
uint8_t tgt_tx_buf[256];

void test_main(void)
{
	/* 单出口（SPEC_REFERENCE §6.9.3）：本核是**从核** ⇒ 不往自己的串口打
	 * token。先开 RLOG 发送通道，引导标记与逐项 token 全部经 mbox 交主核
	 * 原样打印。本核的 LOG / panic / assert 文本不走 RLOG ⇒ 崩溃现场仍留在
	 * 本地串口上。 */
	rlog_secondary_init();
	I3C_I2C_TGT_BOOT();

	LOG_INF("[CPU1 I3C_I2C_TGT] target test started");

	for (int i = 0; i < (int)sizeof(tgt_tx_buf); i++) {
		tgt_tx_buf[i] = (uint8_t)(TGT_READ_PATTERN_BASE + i);
	}

	target_test_init();
	target_dispatcher_run();

	/* dispatcher 在收到 CMD_PRINT_SUMMARY 后退出（tgt_done 标志）；
	 * 检查 test_results[] 判定本核全部项是否 PASS。 */
	bool any_fail = false;
	for (int i = 0; i < I3C_I2C_TEST_COUNT; i++) {
		if (test_results[i] == TR_FAIL) {
			any_fail = true;
			break;
		}
	}
	/* 收尾 DONE 帧必须是整条流的**最后一条**：主核等到它才打印自己的
	 * PROJECT EXECUTION（否则 harness 会因为"已见完成标记"提前杀 gdb、
	 * 截断从核结果）。行首带 `I3C_I2C_TEST:TGT:` ⇒ 采集侧按前缀分拣回
	 * serial_TGT.log，重放仍是两核各自成表（§6.9.4）。 */
	rlog_emit_done(!any_fail, any_fail
		? "I3C_I2C_TEST:TGT:PROJECT EXECUTION FAILED"
		: "I3C_I2C_TEST:TGT:PROJECT EXECUTION SUCCESSFUL");

	/* test_main 不得返回（CLAUDE.md §4.1）：返回会进 ztest 强 main() 的
	 * `TC_END_REPORT`，在本核串口补打 `====` + 一条**本地**
	 * `PROJECT EXECUTION` —— 那既是本核单出口的漏点（从核又自己出了收尾
	 * token），也让主核流里多出一条早于从核 DONE 的完成标记。 */
	k_sleep(K_FOREVER);
}
