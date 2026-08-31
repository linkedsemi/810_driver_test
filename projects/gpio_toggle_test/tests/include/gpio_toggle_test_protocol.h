/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GPIO_TOGGLE_TEST_PROTOCOL_H
#define GPIO_TOGGLE_TEST_PROTOCOL_H

#include <zephyr/sys/printk.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GPIO 基础翻转测试（gpio_toggle_test）—— cpu1 单核。
 *
 * 目的：时钟链验证。PJ10/PJ11（I3C5 的 SCL/SDA 引脚）以 1s 周期
 * 翻转（500ms 高 + 500ms 低），示波器实测周期与软件设定对比，
 * 验证系统时钟是否准确（排查 I2C 400K 实测 333K 的时钟源疑点）。
 *
 * 引脚配置：HAL io_* 直控（per_func_disable_all + io_cfg_output +
 * io_cfg_pushpull），不经 pinctrl；本工程不使能 i2c5/i3c5。
 *
 * 判定方式：示波器人工判定（固件不自验，结果不入 test_results）。
 * 无限循环翻转，按设计无 DONE。
 */

/* 翻转引脚与周期（单一事实源） */
#define TOGGLE_PERIOD_MS 1000U	/* 方波周期：500ms 翻转一次 */

/*
 * Test IDs shared with harness tooling.
 * The order must match gpio_toggle_test_names[] below.
 */
enum gpio_toggle_test_id {
	TEST_IO_TOGGLE_1S = 0,	/* PJ10/PJ11 1s 周期翻转（示波器判定） */
	GPIO_TOGGLE_TEST_COUNT,
};

static const char * const gpio_toggle_test_names[GPIO_TOGGLE_TEST_COUNT] = {
	[TEST_IO_TOGGLE_1S] = "io_toggle_1s",
};

/*
 * Structured UART tokens (same scheme as i3c_dualcore_test).
 */
#define GPIO_TOGGLE_TOKEN_PREFIX "GPIO_TOGGLE_TEST"

#define GPIO_TOGGLE_BOOT_CORE(core) \
	printk(GPIO_TOGGLE_TOKEN_PREFIX ":" #core ":BOOT\n")
#define GPIO_TOGGLE_TGT_BOOT()  GPIO_TOGGLE_BOOT_CORE(TGT)

#define GPIO_TOGGLE_START_CORE_VAR(core, name) \
	printk(GPIO_TOGGLE_TOKEN_PREFIX ":" #core ":START:%s\n", name)
#define GPIO_TOGGLE_PASS_CORE_VAR(core, name) \
	printk(GPIO_TOGGLE_TOKEN_PREFIX ":" #core ":PASS:%s\n", name)
#define GPIO_TOGGLE_FAIL_CORE_VAR(core, name, reason) \
	printk(GPIO_TOGGLE_TOKEN_PREFIX ":" #core ":FAIL:%s:%s\n", name, reason)
#define GPIO_TOGGLE_INFO_CORE(core, name, kv, ...) \
	printk(GPIO_TOGGLE_TOKEN_PREFIX ":" #core ":INFO:%s:" kv "\n", \
	       name, ##__VA_ARGS__)

#define GPIO_TOGGLE_TGT_START_VAR(name) GPIO_TOGGLE_START_CORE_VAR(TGT, name)
#define GPIO_TOGGLE_TGT_PASS_VAR(name)  GPIO_TOGGLE_PASS_CORE_VAR(TGT, name)
#define GPIO_TOGGLE_TGT_FAIL_VAR(name, reason) GPIO_TOGGLE_FAIL_CORE_VAR(TGT, name, reason)
#define GPIO_TOGGLE_TGT_INFO(name, kv, ...) \
	GPIO_TOGGLE_INFO_CORE(TGT, name, kv, ##__VA_ARGS__)

#define GPIO_TOGGLE_DONE_CORE_VAR(core, result) \
	printk(GPIO_TOGGLE_TOKEN_PREFIX ":" #core ":DONE:%s\n", result)
#define GPIO_TOGGLE_TGT_DONE_VAR(result) GPIO_TOGGLE_DONE_CORE_VAR(TGT, result)

#define GPIO_TOGGLE_SUMMARY_BEGIN(core) \
	printk("\n" GPIO_TOGGLE_TOKEN_PREFIX ":" #core ":SUMMARY:BEGIN\n")
#define GPIO_TOGGLE_SUMMARY_ITEM(core, name, result) \
	printk(GPIO_TOGGLE_TOKEN_PREFIX ":" #core ":SUMMARY:ITEM:%s:%s\n", name, result)
#define GPIO_TOGGLE_SUMMARY_ITEM_REASON(core, name, result, reason) \
	printk(GPIO_TOGGLE_TOKEN_PREFIX ":" #core ":SUMMARY:ITEM:%s:%s:%s\n", \
	       name, result, reason)
#define GPIO_TOGGLE_SUMMARY_END(core, passed, ran) \
	printk(GPIO_TOGGLE_TOKEN_PREFIX ":" #core ":SUMMARY:END:%d/%d PASS\n\n", \
	       passed, ran)

#ifdef __cplusplus
}
#endif

#endif /* GPIO_TOGGLE_TEST_PROTOCOL_H */
