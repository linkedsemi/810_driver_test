/*
 * Copyright (c) 2025 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I3C_CONTROLLER_TEST_H
#define I3C_CONTROLLER_TEST_H

#include <stddef.h>
#include <stdint.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/drivers/i3c/ibi.h>

#include "i3c_dualcore_test_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define READ_LEN_MAX   256
#define PHASE_RETRY    3
#define TEST_DELAY_MS  5

extern const struct device *i3c_ctrl_dev;

void log_buf_hex(const char *prefix, const uint8_t *buf, size_t len);

int i3c_write_actual(struct i3c_device_desc *target,
			     const uint8_t *write_buf, size_t num_write,
			     size_t *actual);
int i3c_read_actual(struct i3c_device_desc *target,
			    uint8_t *read_buf, size_t num_read,
			    size_t *actual);
int i3c_write_read_actual(struct i3c_device_desc *target,
				  const uint8_t *write_buf, size_t num_write,
				  uint8_t *read_buf, size_t num_read,
				  size_t *actual);
struct i3c_device_desc *wait_for_target(void);

#ifdef __cplusplus
}
#endif

#endif /* I3C_CONTROLLER_TEST_H */
