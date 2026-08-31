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
 *
 * ztest 原生框架版：test_main 打印角色 banner 后 zt_init + ctrl_flow_start
 * （功能流程 dispatcher 线程）+ zt_run_all（common/ztest_glue）。套件内
 * 10 个 ZTEST 项与 10 个功能项一一对应，等待 dispatcher 的完成快照后
 * 断言（§4.5 记账由 ztest_glue 自动完成）。
 */

#include "i3c_i2c_controller_test.h"
#include "i3c_i2c_switch_test_protocol.h"
#include "ztest_glue.h"

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

void test_main(void)
{
	LOG_INF("[CPU2 I3C_I2C_CTRL] controller test started");   /* 角色 banner（harness 依赖，逐字保留） */

	zt_init("CTRL");
	ctrl_flow_start();
	zt_run_all();
	/* 10 个 ZTEST 快照齐 = 10 功能项完成 → 发 'P' 收尾（I1，样板
	 * i3c_dualcore_test 同款：ztest_run_all 返回后发完成命令） */
	ctrl_send_summary();
}
