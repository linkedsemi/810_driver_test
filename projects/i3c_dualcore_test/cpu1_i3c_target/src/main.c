/*
 * Copyright (c) 2025 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i3c_target_test.h"
#include "i3c_dualcore_test_protocol.h"
#include "ztest_glue.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i3c_slave_test, LOG_LEVEL_INF);

const struct device *i3c_tgt_dev = DEVICE_DT_GET(DT_NODELABEL(i3c6));
uint8_t tgt_tx_buf[256];

void test_main(void)
{
	/* BOOT 标记：harness 判定本固件已启动（§6.2 pass-2 判据）。
	 * 不调用 zt_run_all()——test_main 不得返回（§4.1 dispatcher
	 * k_thread_join），逐项 ztest 由 dispatcher 按 'E' 事件运行。 */
	zt_init("TGT");

	LOG_INF("[CPU1 SLAVE] I3C target test started");

	for (int i = 0; i < (int)sizeof(tgt_tx_buf); i++) {
		tgt_tx_buf[i] = (uint8_t)i;
	}

	target_test_init();
	target_dispatcher_run();
}
