/*
 * Copyright (c) 2025 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i3c_target_test.h"
#include "i3c_dualcore_test_protocol.h"
#include "ztest_glue.h"
#include "rlog.h"

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
	/* 不调用 zt_run_all()——test_main 不得返回（§4.1 dispatcher
	 * k_thread_join），逐项 ztest 由 dispatcher 按 'E' 事件运行。
	 *
	 * 单出口（§6.9.3/§6.11.5）：本核是**从核**⇒ 不往自己串口打 token。
	 * 逐项输出经 `tc_util_user_override.h` 把 PRINT_DATA 指向 RLOG 自动换
	 * 出口；BOOT 走公共宏的 printk 路径 ⇒ 此处显式换出口。本核 LOG / panic
	 * 现场仍走本核串口。本核不设 DONE：test_main 不返回（dispatcher join，
	 * §4.1），收尾标记由主核侧给出（本工程 TGT 无 PROJECT EXECUTION 属设计）。 */
	rlog_secondary_init();
	RLOG_ZT_BOOT("TGT");

	LOG_INF("[CPU1 SLAVE] I3C target test started");

	for (int i = 0; i < (int)sizeof(tgt_tx_buf); i++) {
		tgt_tx_buf[i] = (uint8_t)i;
	}

	target_test_init();
	target_dispatcher_run();
}
