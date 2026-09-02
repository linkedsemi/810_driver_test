/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I3C_I2C_CONTROLLER_TEST_H
#define I3C_I2C_CONTROLLER_TEST_H

#include <stdint.h>
#include <stddef.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i3c.h>

#ifdef __cplusplus
extern "C" {
#endif

/* i3c5：I3C controller（I3C 模式）；i2c5：I2C master（I2C 模式，同一模块两个驱动视图） */
extern const struct device *ctrl_i3c_dev;
extern const struct device *ctrl_i2c_dev;

/* 查找/分配 target 动态地址（30 次重试 DAA），失败返回 NULL */
struct i3c_device_desc *ctrl_wait_for_target(void);

/* I3C 私有传输辅助（带 STOP；I3C 模式命令通道） */
int ctrl_i3c_write(struct i3c_device_desc *target, const uint8_t *buf, size_t len);
int ctrl_i3c_read(struct i3c_device_desc *target, uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* I3C_I2C_CONTROLLER_TEST_H */
