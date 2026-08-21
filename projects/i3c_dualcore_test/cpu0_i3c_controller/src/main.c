/*
 * Copyright (c) 2025 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i3c_controller_test.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/drivers/i3c/ibi.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i3c_master_test, LOG_LEVEL_INF);

const struct device *i3c_ctrl_dev = DEVICE_DT_GET(DT_NODELABEL(i3c5));

void log_buf_hex(const char *prefix, const uint8_t *buf, size_t len)
{
	char str[READ_LEN_MAX * 3 + 1];
	int pos = 0;

	for (size_t i = 0; i < len; i++) {
		pos += snprintf(str + pos, sizeof(str) - pos, "%02X ", buf[i]);
		if (pos >= sizeof(str)) {
			break;
		}
	}
	LOG_INF("%s%s", prefix, str);
}

int i3c_write_actual(struct i3c_device_desc *target,
			     const uint8_t *write_buf, size_t num_write,
			     size_t *actual)
{
	struct i3c_msg msg = {
		.buf = (uint8_t *)write_buf,
		.len = num_write,
		.flags = I3C_MSG_WRITE | I3C_MSG_STOP,
		.hdr_mode = 0,
		.hdr_cmd_code = 0,
		.num_xfer = 0,
	};

	int ret = i3c_transfer(target, &msg, 1);
	if (ret == 0) {
		*actual = msg.num_xfer;
	}
	return ret;
}

int i3c_write_read_actual(struct i3c_device_desc *target,
				  const uint8_t *write_buf, size_t num_write,
				  uint8_t *read_buf, size_t num_read,
				  size_t *actual)
{
	struct i3c_msg msg[2] = {
		{
			.buf = (uint8_t *)write_buf,
			.len = num_write,
			.flags = I3C_MSG_WRITE,
			.hdr_mode = 0,
			.hdr_cmd_code = 0,
			.num_xfer = 0,
		},
		{
			.buf = read_buf,
			.len = num_read,
			.flags = I3C_MSG_RESTART | I3C_MSG_READ | I3C_MSG_STOP,
			.hdr_mode = 0,
			.hdr_cmd_code = 0,
			.num_xfer = 0,
		},
	};

	int ret = i3c_transfer(target, msg, 2);
	if (ret == 0) {
		*actual = msg[1].num_xfer;
	}
	return ret;
}

int i3c_read_actual(struct i3c_device_desc *target,
			    uint8_t *read_buf, size_t num_read,
			    size_t *actual)
{
	struct i3c_msg msg = {
		.buf = read_buf,
		.len = num_read,
		.flags = I3C_MSG_READ | I3C_MSG_STOP,
		.hdr_mode = 0,
		.hdr_cmd_code = 0,
		.num_xfer = 0,
	};

	int ret = i3c_transfer(target, &msg, 1);
	if (ret == 0) {
		*actual = msg.num_xfer;
	}
	return ret;
}

struct i3c_device_desc *wait_for_target(void)
{
	struct i3c_device_desc *target = NULL;
	int ret;

	for (int attempt = 0; attempt < 30; attempt++) {
		target = i3c_dev_list_i3c_addr_find(i3c_ctrl_dev, I3C_TARGET_ADDR);
		if (target != NULL) {
			return target;
		}
		LOG_INF("Target 0x%02X not found, retry DAA (attempt %d)",
		       I3C_TARGET_ADDR, attempt + 1);
		ret = i3c_do_daa(i3c_ctrl_dev);
		if (ret != 0) {
			LOG_INF("i3c_do_daa failed: %d", ret);
		}
		k_sleep(K_MSEC(20));
	}

	return NULL;
}
