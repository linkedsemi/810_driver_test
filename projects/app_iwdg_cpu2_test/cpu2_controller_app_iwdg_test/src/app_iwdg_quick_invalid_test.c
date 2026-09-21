/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * app_iwdg quick-enable 非法参数用例（cpu2，case_quick_invalid = CASE_ID 2）
 *
 * 覆盖驱动 iwdt_init 的 quick_timeout_ms == 0 拒绝分支
 * （wdt_iwdg_ls.c:175-179）：init 打 LOG_ERR 并 return -EINVAL，
 * 其结果在 Zephyr 设备模型中的可观测形态 = 该设备 init 失败、未就绪
 * （state->init_res == EINVAL，见 kernel/init.c:311-326 与
 * device.h:380-394 的"正值 errno"语义）。
 *
 * controller.overlay 设 quick-enable; quick-timeout-ms = <0>。
 * 本 case 只有一个测试项：设备不可用意味着本套件不应再调用任何 wdt_* API。
 *
 * 2026-09-18（F5 波）：原唯一断言是 device_is_ready()==false 的裸否定，
 * 不能区分"驱动按 quick-timeout-ms=0 拒绝"与"别的原因根本没起来"；
 * 现加三级正控（DT 前置条件 / init 已执行 / init_res==EINVAL）。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/ztest.h>
#include <errno.h>

#include "ztest_glue.h"

ZTEST(app_iwdg_quick_invalid, test_quick_timeout_zero_leaves_device_unready)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(app_iwdg));
	bool ready = device_is_ready(dev);

	ZT_INFO("test_quick_timeout_zero_leaves_device_unready",
		"ready=%d,initialized=%d,init_res=%u", (int)ready,
		(dev != NULL && dev->state != NULL) ? (int)dev->state->initialized : -1,
		(dev != NULL && dev->state != NULL) ?
			(unsigned)dev->state->init_res : 0u);

	/* 结论断言：设备模型可观测形态 = 未就绪 */
	zassert_false(ready,
		      "quick-enable + quick-timeout-ms=0 应使 iwdt_init 返回 -EINVAL"
		      "（设备未就绪），实际 ready=%d", (int)ready);

	/* ================= 正控（本轮新增，堵"裸否定断言"空壳）=================
	 * 原实现唯一断言是上面的 zassert_false(device_is_ready(dev))：设备未
	 * 就绪的**任何**原因（节点 disabled / 驱动未编入 / 无关 init 失败 /
	 * overlay 没生效 / 根本没跑到这里）都会让它 PASS，无法区分"驱动按
	 * quick-timeout-ms=0 拒绝"与"别的原因根本没起来"。
	 * 下面按 前置条件 → init 已执行 → 失败原因精确匹配 三级收窄：
	 */

	/* 1. 前置条件（DT 层）：节点 okay、驱动已编入、overlay 的拒绝参数确实
	 *    生效。这三条把"节点 disabled / 配置写错 / overlay 缺失"排掉 ——
	 *    若 overlay 没生效，下面的 quick 参数断言先炸。（DT_PROP_OR 的默认
	 *    值 -1 使属性缺失时判为"不是 0"，不依赖编译期报错。） */
	zassert_true(DT_NODE_HAS_STATUS(DT_NODELABEL(app_iwdg), okay),
		     "app_iwdg node is not okay in the merged DTS");
	zassert_true(IS_ENABLED(CONFIG_WATCHDOG),
		     "CONFIG_WATCHDOG is off: wdt_iwdg_ls driver not built");
	zassert_true(DT_NODE_HAS_PROP(DT_NODELABEL(app_iwdg), quick_enable),
		     "overlay did not set quick-enable on app_iwdg");
	zassert_equal(DT_PROP_OR(DT_NODELABEL(app_iwdg), quick_timeout_ms, -1), 0,
		      "overlay quick-timeout-ms is not 0 (case conf/overlay wrong)");

	/* 2. init 函数确已执行：Zephyr 设备模型在 do_device_init() 里**无条件**
	 *    置 state->initialized=true（zephyr/kernel/init.c:311-326），失败时
	 *    把**正值** errno 写入 state->init_res。initialized==false 意味着
	 *    init 根本没被调用（设备未链接进镜像）—— 那样"未就绪"不构成证据。 */
	zassert_true(dev != NULL && dev->state != NULL,
		     "device handle/state is NULL (driver not linked?)");
	zassert_true(dev->state->initialized,
		     "iwdt_init was never invoked (device not in the image)");

	/* 3. 失败原因精确匹配：iwdt_init 只有三条返回路径 —— -ENODEV（时钟
	 *    设备未就绪，:155-158）/ 时钟使能失败（:161-165）/ -EINVAL
	 *    （quick_timeout_ms==0，:175-179，见 wdt_iwdg_ls.c）。
	 *    init_res 存正值 errno ⇒ 等于 EINVAL(22) **唯一**指向
	 *    quick-timeout-ms 拒绝分支，排除其余全部原因。 */
	zassert_equal(dev->state->init_res, (uint8_t)EINVAL,
		      "iwdt_init failed with init_res=%u, not EINVAL(22): "
		      "device is unready for a different reason than the "
		      "quick_timeout_ms==0 rejection",
		      (unsigned)dev->state->init_res);
}

ZTEST_SUITE(app_iwdg_quick_invalid, NULL, NULL, NULL, NULL, NULL);
