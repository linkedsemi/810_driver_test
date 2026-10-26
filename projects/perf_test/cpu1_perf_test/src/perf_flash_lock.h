/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Serializes access to the qspi2 NOR between the local perf_flash suite and
 * the cpu2 flash-proxy server thread (both run on cpu1).
 */
#ifndef PERF_FLASH_LOCK_H
#define PERF_FLASH_LOCK_H

#include <zephyr/kernel.h>

extern struct k_mutex perf_flash_mutex;

#endif /* PERF_FLASH_LOCK_H */
