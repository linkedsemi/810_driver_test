/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * I3C/I2C 模块切换测试 —— cpu2 controller 侧。
 *
 * 被测对象：同一硬件模块（I3C5）在 I3C 与 I2C 两种模式间切换，模式由
 * 引脚复用（pinctrl）区分。本侧控制器以两个驱动视图存在：
 *   i3c5 = I3C controller（"default" 状态）
 *   i2c5 = I2C master（"pinmux_i2c" 状态，i3c5 节点的 pinctrl-1）
 * 切换 = pinctrl_apply_state()（CONFIG_PINCTRL_DYNAMIC）。
 *
 * 命令通道（与 target 的解析状态机对应）：
 *   I3C 模式：i3c_transfer 私有写（target 动态地址 0x32）
 *   I2C 模式：i2c_write（slave 地址 0x50）
 * 首字节为 opcode（ASCII 助记符），见 i3c_i2c_switch_test_protocol.h。
 */

#include "i3c_i2c_controller_test.h"
#include "i3c_i2c_switch_test_protocol.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i3c_i2c_master_test, LOG_LEVEL_INF);

const struct device *ctrl_i3c_dev = DEVICE_DT_GET(DT_NODELABEL(i3c5));
const struct device *ctrl_i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c5));

int ctrl_i3c_write(struct i3c_device_desc *target, const uint8_t *buf, size_t len)
{
	struct i3c_msg msg = {
		.buf = (uint8_t *)buf,
		.len = len,
		.flags = I3C_MSG_WRITE | I3C_MSG_STOP,
		.hdr_mode = 0,
		.hdr_cmd_code = 0,
		.num_xfer = 0,
	};

	int ret = i3c_transfer(target, &msg, 1);

	if (ret != 0) {
		/* 探针：write 失败时抓 pad 配置 + I3C 控制器错误码
		 * （i3c_transfer 返回时 ERRF 已被驱动清掉，但 SER 的
		 *  ANACK/DNACK/DOVR 类型位保留到下一次帧开始才更新）。
		 * 调试完必须删除（探针纪律 §8）。 */
		extern volatile uint32_t dbg_err[12];
		extern volatile int dbg_err_valid;

		dbg_err[0] = *(volatile uint32_t *)0x4006f2a4;	/* PU1_PU0 */
		dbg_err[1] = *(volatile uint32_t *)0x4006f2a8;	/* PD_PU2  */
		dbg_err[2] = *(volatile uint32_t *)0x4006f2ac;	/* IEN     */
		dbg_err[3] = *(volatile uint32_t *)0x4006f2b0;	/* DS1_DS0 */
		dbg_err[4] = *(volatile uint32_t *)0x4006f2b4;	/* AE_DS2  */
		dbg_err[5] = *(volatile uint32_t *)0x4006f2b8;	/* OD_FIR  */
		dbg_err[6] = *(volatile uint32_t *)0x4006f2bc;	/* SL_ST   */
		dbg_err[7] = *(volatile uint32_t *)0x400610b0;	/* IO_FUNC[1][4] */
		dbg_err[8] = *(volatile uint32_t *)0x400610d0;	/* IO_FUNC[2][4] */
		dbg_err[9] = *(volatile uint32_t *)0x4008fcd8;	/* FUNC_SEL[9][2] */
		dbg_err[10] = *(volatile uint32_t *)0x40094158;	/* EVR     */
		dbg_err[11] = *(volatile uint32_t *)0x40094160;	/* SER     */
		dbg_err_valid = (int)ret;	/* 非 0 = 有效，同时记录返回码 */
	}

	return ret;
}

int ctrl_i3c_read(struct i3c_device_desc *target, uint8_t *buf, size_t len)
{
	struct i3c_msg msg = {
		.buf = buf,
		.len = len,
		.flags = I3C_MSG_READ | I3C_MSG_STOP,
		.hdr_mode = 0,
		.hdr_cmd_code = 0,
		.num_xfer = 0,
	};

	int ret = i3c_transfer(target, &msg, 1);

	return ret;
}

struct i3c_device_desc *ctrl_wait_for_target(void)
{
	struct i3c_device_desc *target = NULL;

	for (int attempt = 0; attempt < 30; attempt++) {
		target = i3c_dev_list_i3c_addr_find(ctrl_i3c_dev, I3C_SWITCH_TARGET_ADDR);
		if (target != NULL) {
			return target;
		}
		LOG_INF("Target 0x%02X not found, retry DAA (attempt %d)",
			I3C_SWITCH_TARGET_ADDR, attempt + 1);
		int ret = i3c_do_daa(ctrl_i3c_dev);

		if (ret != 0) {
			LOG_INF("i3c_do_daa failed: %d", ret);
		}
		k_sleep(K_MSEC(20));
	}

	return NULL;
}
