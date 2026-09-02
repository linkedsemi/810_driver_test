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
	zt_init("TGT");

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
	printk("PROJECT EXECUTION %s\n", any_fail ? "FAILED" : "SUCCESSFUL");
}
