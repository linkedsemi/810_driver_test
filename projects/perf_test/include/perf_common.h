/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared throughput reporting for perf_test.
 *
 * One stable line per measurement, easy to grep from the serial archive:
 *   PERF <module> | <op> | bytes=.. | us=.. | MB/s=.. | KB/s=..
 *   PERF <module> | <op> | ops=..   | us=.. | ops/s=..
 * 1 byte/us == 1 MB/s (decimal). Rates are informational; the zasserts in
 * each suite keep the functional contract.
 */
#ifndef PERF_COMMON_H
#define PERF_COMMON_H

#include <zephyr/kernel.h>
#include <stdint.h>

static inline void perf_bytes(const char *module, const char *op,
			      uint64_t bytes, uint64_t us)
{
	if (us == 0) {
		us = 1;
	}
	double mb_s = (double)bytes / (double)us;

	printk("PERF %s | %s | bytes=%llu | us=%llu | MB/s=%.4f | KB/s=%.2f\n",
	       module, op, (unsigned long long)bytes,
	       (unsigned long long)us, mb_s, mb_s * 1000.0);
}

static inline void perf_ops(const char *module, const char *op,
			    uint64_t ops, uint64_t us)
{
	if (us == 0) {
		us = 1;
	}
	printk("PERF %s | %s | ops=%llu | us=%llu | ops/s=%.2f\n",
	       module, op, (unsigned long long)ops,
	       (unsigned long long)us,
	       (double)ops * 1000000.0 / (double)us);
}

#endif /* PERF_COMMON_H */
