/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "perf_buf.h"

uint8_t perf_buf[PERF_BUF_SIZE + PERF_BUF_PAD] __aligned(32);
