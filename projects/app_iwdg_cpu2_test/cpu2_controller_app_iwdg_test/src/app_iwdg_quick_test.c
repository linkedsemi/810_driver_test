/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * app_iwdg quick-enable 用例（cpu2，case_quick_enable = CASE_ID 1）
 *
 * 覆盖驱动 iwdt_init 的 quick_enable 正常分支（wdt_iwdg_ls.c:172-184）：
 *   quick_enable == true && quick_timeout_ms > 0
 *     -> IWDT_LOAD = quick_timeout_ms 换算 ticks
 *     -> IWDT_CTRL  = IWDT_RST_EN | IWDT_EN（POST_KERNEL 阶段即启动）
 *
 * 本 case 通过 controller.overlay 给 app_iwdg 加 `quick-enable` 与
 * `quick-timeout-ms`——这是本案新开独立工程后**新增**的覆盖能力
 * （wdt_test 受"只增不改"约束无法引入该 DT 配置）。
 *
 * 顺序无关性说明：启动态只能在内核 init 之后、任何 install_timeout 之前
 * 观测（install_timeout 会写 CTRL=0）。为避免依赖 ztest 链接序
 * （SORT_BY_NAME，SPEC_REFERENCE §10.27），本 case 用**单个**测试项
 * 内部固定顺序 观测 -> feed -> disable。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/ztest.h>
#include <errno.h>

#include "ztest_glue.h"

#define IWDG_LOAD_WORD 0u
#define IWDG_CTRL_WORD 1u

#define APP_IWDG_REGS ((volatile uint32_t *)DT_REG_ADDR(DT_NODELABEL(app_iwdg)))

#define IWDG_CTRL_ENABLED 0x3u

/*
 * controller.overlay 设 quick-timeout-ms = <60000>；
 * ticks = 60000 * 32768 / 1000 = 1966080（不回绕）。
 */
#define QUICK_TIMEOUT_MS 60000u
#define QUICK_TICKS 1966080u

ZTEST(app_iwdg_quick, test_quick_enable_boot_state_feed_disable)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(app_iwdg));

	zassert_true(device_is_ready(dev), "app_iwdg not ready");

	/* 1. 启动态：quick_enable 应在 POST_KERNEL 阶段就把看门狗跑起来 */
	uint32_t load = APP_IWDG_REGS[IWDG_LOAD_WORD];
	uint32_t ctrl = APP_IWDG_REGS[IWDG_CTRL_WORD];

	ZT_INFO("test_quick_enable_boot_state_feed_disable",
		"ms=%u,load=%u,ctrl=0x%x", QUICK_TIMEOUT_MS, load, ctrl);
	zassert_equal(load, QUICK_TICKS,
		      "IWDT_LOAD=%u after quick-enable(%ums), expected %u",
		      load, QUICK_TIMEOUT_MS, QUICK_TICKS);
	zassert_equal(ctrl, IWDG_CTRL_ENABLED,
		      "IWDT_CTRL=0x%x after quick-enable, expected 0x%x (RST_EN|EN)",
		      ctrl, IWDG_CTRL_ENABLED);

	/* 2. 喂狗通路在 quick-enable 态下同样可用 */
	int feed = wdt_feed(dev, 0);

	zassert_equal(feed, 0, "feed after quick-enable returned %d, expected 0", feed);

	/* 3. 关闭，避免 60s 超时影响后续 */
	int dis = wdt_disable(dev);

	zassert_equal(dis, 0, "disable returned %d, expected 0", dis);
	zassert_equal(APP_IWDG_REGS[IWDG_CTRL_WORD], 0u,
		      "IWDT_CTRL should be 0 after disable");
}

ZTEST_SUITE(app_iwdg_quick, NULL, NULL, NULL, NULL, NULL);
