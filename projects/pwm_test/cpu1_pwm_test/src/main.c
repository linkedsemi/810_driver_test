/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PWM interface test - entry point (single-core, cpu1)
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "ztest_glue.h"

LOG_MODULE_REGISTER(pwm_test, LOG_LEVEL_INF);

void test_main(void)
{
	LOG_INF("[CPU1 PWM] PWM interface test started");
	zt_init("TGT");
	zt_run_all();
}
