/*
 * Copyright (c) 2025 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I3C_TARGET_TEST_H
#define I3C_TARGET_TEST_H

#include <stdint.h>
#include <stddef.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const struct device *i3c_tgt_dev;
extern uint8_t tgt_tx_buf[256];

void target_test_init(void);
void target_dispatcher_run(void);

#ifdef __cplusplus
}
#endif

#endif /* I3C_TARGET_TEST_H */
