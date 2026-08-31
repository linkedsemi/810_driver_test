/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I3C_I2C_TARGET_TEST_H
#define I3C_I2C_TARGET_TEST_H

#include <stdint.h>
#include <stddef.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/* i3c6：I3C target（I3C 模式）；i2c6：I2C slave（I2C 模式，同一模块两个驱动视图） */
extern const struct device *tgt_i3c_dev;
extern const struct device *tgt_i2c_dev;

/* 读预填数据缓冲（0xAA + i），i3c/i2c 读回调共用 */
extern uint8_t tgt_tx_buf[256];

void target_test_init(void);
void target_dispatcher_run(void);

#ifdef __cplusplus
}
#endif

#endif /* I3C_I2C_TARGET_TEST_H */
