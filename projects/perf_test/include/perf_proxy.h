/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flash proxy protocol between cpu2 (client) and cpu1 (server).
 *
 * cpu2 sends flash operations over mbox; cpu1 executes them against the qspi2
 * board NOR and returns read data / status.  The client measures the whole
 * round trip (request + data transfer), so the PERF line is an end-to-end
 * proxy throughput, not just the flash device time.
 *
 * Frames are exactly 64 bytes (the mbox channel width).
 */
#ifndef PERF_PROXY_H
#define PERF_PROXY_H

#include <stdint.h>

#define PERF_PROXY_MAGIC      0x464C5052u /* "FLPR" little-endian memory */
#define PERF_PROXY_FRAME_SIZE 64
#define PERF_PROXY_CHUNK      40          /* data bytes per frame (64 - 24 header) */
#define PERF_PROXY_OFFSET     0x10000u    /* 64 KB into the qspi2 NOR */
#define PERF_PROXY_SIZE       4096u       /* bytes per measured operation */

enum perf_proxy_op {
	PERF_PROXY_PING  = 1,
	PERF_PROXY_READ  = 2,
	PERF_PROXY_WRITE = 3,
	PERF_PROXY_ERASE = 4,
};

#define PERF_PROXY_FLAG_LAST 0x01

struct perf_proxy_msg {
	uint32_t magic;
	uint8_t  op;
	uint8_t  flags;
	uint16_t seq;
	uint32_t offset;
	uint32_t len;
	uint32_t status;
	uint8_t  data[44];
};

_Static_assert(sizeof(struct perf_proxy_msg) == PERF_PROXY_FRAME_SIZE,
	       "perf_proxy_msg must match the 64-byte mbox frame");

#endif /* PERF_PROXY_H */
