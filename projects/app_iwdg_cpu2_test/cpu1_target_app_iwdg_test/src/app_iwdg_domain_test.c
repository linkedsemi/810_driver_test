/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * app_iwdg_cpu2_test - cpu1 侧结构守卫用例
 *
 * cpu1 无被测对象（app_iwdg 是 cpu2 外设），本套件锁定的是"为什么本案必须
 * 拆成独立 cpu2 工程"这一结构事实，防止后续有人把 app_iwdg 用例塞回单核
 * 工程（roadmap §4「通用规则」）：
 *   - app_iwdg 节点在 cpu1 板级 dts 上未使能；
 *   - cpu1 侧 reset_reason_init() 走 SEC_PMU/SEC_PER 分支，不产生
 *     APP_IWDT_HART_RESET（`soc_reset_reason.c` 的
 *     `DT_NODE_HAS_STATUS(DT_NODELABEL(cpu1), okay)` 分支）。
 *
 * 本套件全部为**结构守卫**（编译期事实的运行期断言），非行为验证；
 * 真正的行为验证在 cpu2 侧 app_iwdg 套件。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/ztest.h>

#include "ztest_glue.h"
#include "rlog.h"
#include "soc_reset.h"

ZTEST(app_iwdg_domain, test_cpu1_does_not_own_app_iwdg)
{
	zassert_false(DT_NODE_HAS_STATUS(DT_NODELABEL(app_iwdg), okay),
		      "cpu1 板级不应使能 app_iwdg（它是 cpu2 应用域外设）");
}

ZTEST(app_iwdg_domain, test_cpu1_reset_reason_not_app_iwdt)
{
	uint32_t reason = reset_reason_get();

	/* 单出口：INFO 是四类标记之一（§6.9.1）⇒ 不能留在本核串口 */
	RLOG_ZT_INFO("test_cpu1_reset_reason_not_app_iwdt", "reason=%u", reason);
	zassert_not_equal(reason, APP_IWDT_HART_RESET,
			  "cpu1 不应报告 APP_IWDT_HART_RESET（该枚举仅 cpu2 侧分支产生），实际 %u",
			  reason);
}

ZTEST_SUITE(app_iwdg_domain, NULL, NULL, NULL, NULL, NULL);
