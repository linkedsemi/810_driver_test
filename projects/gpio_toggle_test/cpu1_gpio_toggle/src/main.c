/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO 基础翻转测试 —— cpu1 单核。
 *
 * 用途：时钟链验证。PJ10/PJ11（I3C5 的 SCL/SDA 引脚）以 1s 周期翻转
 * （k_msleep(500) 半周期），示波器实测周期 vs 软件设定 1s：
 *   - 实测周期 ≈1s     → 系统时钟链准确，I2C 速率偏差根因在分频计算
 *   - 实测周期同比偏差 → 时钟源（PLL/晶振/PBUS4 配置）不准
 *
 * 引脚配置走 HAL io_* 直控（ls_soc_gpio.h），不经 Zephyr pinctrl/gpio
 * 子系统；本工程不使能 i2c5/i3c5，PJ10/PJ11 无其他占用。
 *
 * 无限循环翻转（示波器观察），test_main 按设计不返回（CLAUDE.md §4.1）。
 */

#include "gpio_toggle_test_protocol.h"
#include "test_results.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <ls_soc_gpio.h>

LOG_MODULE_REGISTER(gpio_toggle_test, LOG_LEVEL_INF);

/* I3C5 的 SCL/SDA 引脚（GPIOJ10/J11） */
#define TOGGLE_PIN_SCL PJ10
#define TOGGLE_PIN_SDA PJ11

void test_main(void)
{
	GPIO_TOGGLE_TGT_BOOT();
	LOG_INF("[CPU1 GPIO_TOGGLE] io toggle test started");

	tr_init(GPIO_TOGGLE_TEST_COUNT);

	GPIO_TOGGLE_TGT_START_VAR(gpio_toggle_test_names[TEST_IO_TOGGLE_1S]);

	/* PJ10/PJ11 配置为 GPIO 推挽输出（确保无片上外设功能占用） */
	per_func_disable_all(TOGGLE_PIN_SCL);
	per_func_disable_all(TOGGLE_PIN_SDA);
	io_cfg_output(TOGGLE_PIN_SCL);
	io_cfg_output(TOGGLE_PIN_SDA);
	io_cfg_pushpull(TOGGLE_PIN_SCL);
	io_cfg_pushpull(TOGGLE_PIN_SDA);

	GPIO_TOGGLE_TGT_INFO(gpio_toggle_test_names[TEST_IO_TOGGLE_1S],
			     "pins=PJ10_PJ11,period_ms=%u,ticks_per_sec=%d",
			     TOGGLE_PERIOD_MS, CONFIG_SYS_CLOCK_TICKS_PER_SEC);

	/* 无限翻转：500ms 半周期 → 1s 方波；示波器人工判定，无 DONE */
	while (true) {
		io_toggle_pin(TOGGLE_PIN_SCL);
		io_toggle_pin(TOGGLE_PIN_SDA);
		k_msleep(TOGGLE_PERIOD_MS / 2);
	}
}
