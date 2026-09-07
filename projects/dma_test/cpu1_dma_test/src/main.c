/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DMA M2M test - entry point (single-core, cpu1)
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "ztest_glue.h"

LOG_MODULE_REGISTER(dma_test, LOG_LEVEL_INF);

void test_main(void)
{
	LOG_INF("[CPU1 DMA] DMA M2M test started");
	zt_init("TGT");
	zt_run_all();
}
