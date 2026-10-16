/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * i2cf_test 单核测试入口（cpu1 安全域）。
 * 纯 ztest：test_main 打印角色 banner 后 zt_init + zt_run_all
 * （common/ztest_glue），不返回（同 ztest 语义）。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

#include "ztest_glue.h"
#include "i2cf_wave.h"

LOG_MODULE_REGISTER(i2cf_test, LOG_LEVEL_INF);

/*
 * ★ 2026-09-22：注册**片内 I2C target**（从机 `i2c3` 上的 `eeprom@50`）。
 *
 * 为什么需要：用户裁定「**不管 EEPROM 的存在**，只测芯片自己的两路 I2C + I2CF」
 * ⇒ 用**片内从设备**取代原先"外接 EEPROM（0x51/0x52，未经实测）"的假设，
 * B 组三项随之变成**零外部器件**。
 *
 * ⚠️ 驱动 init **不自注册**（`zephyr,i2c-target-eeprom` 只建实例），必须由应用
 *    显式调 `i2c_target_driver_register()` —— 做法同
 *    `modules/lib/pfr_tpm/samples/i2c_filter_loopback/src/main.c:111,119`。
 *
 * 注册失败**不 panic、不判失败**：只打一条 ERR。理由 —— 注册的作用是"给对端造
 * 一个应答者"，失败时后续用例会以「扫不到从设备 ⇒ SKIP」的自然语义收场，
 * **不会静默变成假 PASS**（SKIP 是确定结果，不是通过）。
 */
static void i2cf_register_onchip_target(void)
{
	const struct device *tgt = DEVICE_DT_GET(DT_ALIAS(i2ctargeteeprom));

	if (!device_is_ready(tgt)) {
		LOG_ERR("%s: on-chip i2c target not ready", tgt->name);
		return;
	}
	if (i2c_target_driver_register(tgt) < 0) {
		LOG_ERR("%s: i2c_target_driver_register failed", tgt->name);
		return;
	}
	LOG_INF("on-chip i2c target registered: %s @0x%02x", tgt->name,
		(unsigned int)DT_REG_ADDR(DT_ALIAS(i2ctargeteeprom)));
}

void test_main(void)
{
	LOG_INF("[CPU1 I2CF] I2C filter test started");   /* 角色 banner（harness 依赖） */

	i2cf_register_onchip_target();   /* 必须在 zt_run_all 之前：用例依赖它作对端 */

#if defined(CONFIG_I2CF_WAVE_GEN)
	/* 循环波形发生器：永不返回（不跑 ztest，无 zassert）—— 示波器抓取用 */
	LOG_INF("[CPU1 I2CF] wave generator mode (CONFIG_I2CF_WAVE_GEN)");
	i2cf_wave_gen_run();
#else
	/* 常规回归：ztest 跑全部用例（含 CONFIG_I2CF_ENUM_MATRIX 时的 i2cf::enum_matrix）。 */
	zt_init("TGT");
	zt_run_all();
#endif
}
