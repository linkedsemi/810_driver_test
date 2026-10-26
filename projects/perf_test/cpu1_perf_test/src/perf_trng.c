/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * TRNG throughput: trng1 (DesignWare) and trng0 (APB ring oscillator).
 */
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>

#include "perf_common.h"
#include "perf_buf.h"

static void trng_measure(const struct device *dev, const char *op,
			 size_t total, size_t chunk)
{
	int ret = 0;
	uint64_t t0 = k_cycle_get_64();

	for (size_t off = 0; off < total && ret == 0; off += chunk) {
		size_t len = MIN(chunk, total - off);

		ret = entropy_get_entropy(dev, &perf_buf[off], (uint16_t)len);
	}

	uint64_t t1 = k_cycle_get_64();

	zassert_equal(ret, 0, "%s entropy_get_entropy failed: %d", op, ret);
	perf_bytes("trng", op, total, k_cyc_to_us_floor64(t1 - t0));
}

ZTEST(perf_trng, trng1_throughput)
{
	trng_measure(DEVICE_DT_GET(DT_NODELABEL(trng1)), "trng1_dw",
		     PERF_BUF_SIZE, KB(8));
}

ZTEST(perf_trng, trng0_throughput)
{
	trng_measure(DEVICE_DT_GET(DT_NODELABEL(trng0)), "trng0_apb",
		     KB(4), KB(4));
}

ZTEST_SUITE(perf_trng, NULL, NULL, NULL, NULL, NULL);
