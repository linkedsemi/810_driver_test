/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * NOR flash throughput: qspi1 boot flash (rwfs partition) and qspi2 board NOR.
 *
 * Both paths are DESTRUCTIVE to their scratch area.  This project runs 2os
 * from RAM, so qspi1 writes do not touch the running image; they only overwrite
 * the rwfs partition.  qspi2 uses the first 32 KB of the board NOR.
 */
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <string.h>

#include "perf_common.h"
#include "perf_buf.h"
#include "perf_flash_lock.h"

#define FLASH_SIZE PERF_BUF_SIZE

static void perf_flash_fill(uint8_t seed)
{
	for (size_t i = 0; i < FLASH_SIZE; i++) {
		perf_buf[i] = (uint8_t)(seed + i);
	}
}

/* ---------------------------------------------------- qspi1 (flash_area) */

ZTEST(perf_flash, flash1_read)
{
	const struct flash_area *fa;
	uint64_t t0, t1;

	zassert_equal(flash_area_open(FIXED_PARTITION_ID(rwfs_partition), &fa),
		      0, "open rwfs partition");

	t0 = k_cycle_get_64();
	zassert_equal(flash_area_read(fa, 0, perf_buf, FLASH_SIZE), 0,
		      "rwfs read");
	t1 = k_cycle_get_64();

	flash_area_close(fa);
	perf_bytes("flash", "qspi1_read", FLASH_SIZE,
		   k_cyc_to_us_floor64(t1 - t0));
}

ZTEST(perf_flash, flash1_write)
{
	const struct flash_area *fa;
	uint64_t t0, t1;

	zassert_equal(flash_area_open(FIXED_PARTITION_ID(rwfs_partition), &fa),
		      0, "open rwfs partition");
	zassert_equal(flash_area_erase(fa, 0, FLASH_SIZE), 0, "rwfs pre-erase");
	perf_flash_fill(0x11);

	t0 = k_cycle_get_64();
	zassert_equal(flash_area_write(fa, 0, perf_buf, FLASH_SIZE), 0,
		      "rwfs write");
	t1 = k_cycle_get_64();

	flash_area_close(fa);
	perf_bytes("flash", "qspi1_write", FLASH_SIZE,
		   k_cyc_to_us_floor64(t1 - t0));
}

ZTEST(perf_flash, flash1_erase)
{
	const struct flash_area *fa;
	uint64_t t0, t1;

	zassert_equal(flash_area_open(FIXED_PARTITION_ID(rwfs_partition), &fa),
		      0, "open rwfs partition");

	t0 = k_cycle_get_64();
	zassert_equal(flash_area_erase(fa, 0, FLASH_SIZE), 0, "rwfs erase");
	t1 = k_cycle_get_64();

	flash_area_close(fa);
	perf_bytes("flash", "qspi1_erase", FLASH_SIZE,
		   k_cyc_to_us_floor64(t1 - t0));
}

/* ------------------------------------------------------- qspi2 (flash2) */

ZTEST(perf_flash, flash2_read)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(flash2));
	uint64_t t0, t1;

	zassert_true(device_is_ready(dev), "flash2 not ready");

	k_mutex_lock(&perf_flash_mutex, K_FOREVER);
	t0 = k_cycle_get_64();
	zassert_equal(flash_read(dev, 0, perf_buf, FLASH_SIZE), 0,
		      "flash2 read");
	t1 = k_cycle_get_64();
	k_mutex_unlock(&perf_flash_mutex);

	perf_bytes("flash", "qspi2_read", FLASH_SIZE,
		   k_cyc_to_us_floor64(t1 - t0));
}

ZTEST(perf_flash, flash2_write)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(flash2));
	uint64_t t0, t1;

	zassert_true(device_is_ready(dev), "flash2 not ready");

	k_mutex_lock(&perf_flash_mutex, K_FOREVER);
	zassert_equal(flash_erase(dev, 0, FLASH_SIZE), 0, "flash2 pre-erase");
	perf_flash_fill(0x22);
	t0 = k_cycle_get_64();
	zassert_equal(flash_write(dev, 0, perf_buf, FLASH_SIZE), 0,
		      "flash2 write");
	t1 = k_cycle_get_64();
	k_mutex_unlock(&perf_flash_mutex);

	perf_bytes("flash", "qspi2_write", FLASH_SIZE,
		   k_cyc_to_us_floor64(t1 - t0));
}

ZTEST(perf_flash, flash2_erase)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(flash2));
	uint64_t t0, t1;

	zassert_true(device_is_ready(dev), "flash2 not ready");

	k_mutex_lock(&perf_flash_mutex, K_FOREVER);
	t0 = k_cycle_get_64();
	zassert_equal(flash_erase(dev, 0, FLASH_SIZE), 0, "flash2 erase");
	t1 = k_cycle_get_64();
	k_mutex_unlock(&perf_flash_mutex);

	perf_bytes("flash", "qspi2_erase", FLASH_SIZE,
		   k_cyc_to_us_floor64(t1 - t0));
}

ZTEST_SUITE(perf_flash, NULL, NULL, NULL, NULL, NULL);
