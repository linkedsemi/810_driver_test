/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared measurement buffer for perf_test.
 *
 * The 2os cpu1 RAM region is only 256 KB.  Crypto, TRNG and flash suites run
 * sequentially, so they share one buffer instead of each reserving 64 KB.
 */
#ifndef PERF_BUF_H
#define PERF_BUF_H

#include <zephyr/kernel.h>
#include <stdint.h>

#define PERF_BUF_SIZE KB(32)
/* Extra room so the unaligned hash tests can start at perf_buf + 1. */
#define PERF_BUF_PAD  32

extern uint8_t perf_buf[];

#endif /* PERF_BUF_H */
