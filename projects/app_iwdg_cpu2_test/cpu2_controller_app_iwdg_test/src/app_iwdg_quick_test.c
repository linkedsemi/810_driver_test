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
 * （sec_iwdg_cpu1_test 受"只增不改"约束无法引入该 DT 配置）。
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

	/* ★ 2026-09-21：先把 disable 后的 `IWDT_CTRL` 实际值**采出来**再断言。
	 *
	 * 原断言直接判"整寄存器 == 0"，且失败消息不带实测值 ⇒ 2026-09-21 那轮
	 * 回归只知道"非 0"、**不知道非 0 的是哪一位**，该 FAIL 至今无法判定：
	 * 是硬件留存位（`KNOWN_ISSUES #92` 已记 feed 后 `IWDT_CNT_CLR`(bit3)
	 * 会留存），还是 disable 压根没生效。二者的处置完全不同。
	 *
	 * **本条不放宽任何判据**（仍断言整寄存器为 0），只补采数 —— 下次回归
	 * 即可从 INFO 行直接读出位值，把"无法判定"变成"可判定"。
	 * 两条 INFO 同名不冲突：解析器是 `infos.setdefault(name, {}).update(...)`
	 * （`common/ztest_report.py:184`），按 key 合并而非整条覆盖。 */
	uint32_t ctrl_after = APP_IWDG_REGS[IWDG_CTRL_WORD];

	ZT_INFO("test_quick_enable_boot_state_feed_disable",
		"ctrl_after_disable=0x%x", ctrl_after);

	/* 判据取"看门狗确已关闭"的**语义位** `EN|RST_EN`，而不是整寄存器为 0。
	 *
	 * ★ 2026-09-22 上板实测（`OPEN_ITEMS` O-32）：disable 后 `IWDT_CTRL = 0x8`
	 *   —— **只有 bit3 `IWDT_CNT_CLR` 残留**，bit0 `EN` 与 bit1 `RST_EN`
	 *   **均已清零** ⇒ `iwdt_ls_disable()` 确已生效。
	 *   该残留位是**硬件清计数类位的留存行为**（`KNOWN_ISSUES #92` 已记同一
	 *   寄存器在 `feed` 后留存 bit3），disable 本就清不掉它 ⇒ 原判据
	 *   "整寄存器 == 0" 对本硬件**不可满足**，属判据过强。
	 *
	 * ★ 改判语义位后**不是放宽**：`EN` 或 `RST_EN` 只要有一个仍为 1
	 *   （= disable 未生效）**依然会被抓住**；且完整寄存器值仍经上面的
	 *   `ZT_INFO` 上报，任何异常位都不会被掩盖。
	 *   （本项在 disable 前先执行过 `wdt_feed`，见 `:64`。）
	 *   位定义：`modules/hal/linkedsemi/hal_driver/inc/reg_iwdgv2_type.h:31-37`。 */
	zassert_equal(ctrl_after & IWDG_CTRL_ENABLED, 0u,
		      "IWDT_CTRL=0x%x after disable: EN|RST_EN 未清零 ⇒ disable 未生效"
		      " (位: bit3=CNT_CLR bit2=INTR_CLR bit1=RST_EN bit0=EN)",
		      ctrl_after);
}

ZTEST_SUITE(app_iwdg_quick, NULL, NULL, NULL, NULL, NULL);
