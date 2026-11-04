/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared throughput reporting for perf_test.
 *
 * One stable line per measurement, easy to grep from the serial archive:
 *   PERF <module> | <op> | bytes=.. | MB/s=.. | KB/s=.. | us=..
 *   PERF <module> | <op> | ops=..   | ops/s=.. | us=..
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

	/* Rate right after the data, elapsed time last: in a log you want
	 * "how much, how fast" first and the raw microseconds only when needed. */
	printk("PERF %s | %s | bytes=%llu | MB/s=%.4f | KB/s=%.2f | us=%llu\n",
	       module, op, (unsigned long long)bytes,
	       mb_s, mb_s * 1000.0, (unsigned long long)us);
}

static inline void perf_ops(const char *module, const char *op,
			    uint64_t ops, uint64_t us)
{
	if (us == 0) {
		us = 1;
	}
	printk("PERF %s | %s | ops=%llu | ops/s=%.2f | us=%llu\n",
	       module, op, (unsigned long long)ops,
	       (double)ops * 1000000.0 / (double)us,
	       (unsigned long long)us);
}

#endif /* PERF_COMMON_H */
