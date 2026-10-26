/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DMA memory-to-memory throughput.
 *
 * dmac1 on this SoC limits a single block transfer to the
 * ctlh-block-ts-mask value (0x7ff -> 2047 bytes), so larger totals are
 * measured as a series of block transfers.  The PERF line reports the total
 * bytes moved, which is the number that matters for sustained throughput.
 */
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <string.h>

#include "perf_common.h"

#define DMA_NODE DT_NODELABEL(dmac1)
#define DMA_CHANNEL 0
#define DMA_BLOCK_SIZE KB(1)
#define DMA_MAX_SIZE KB(16)

static __nocache __aligned(32) uint8_t dma_src[DMA_MAX_SIZE];
static __nocache __aligned(32) uint8_t dma_dst[DMA_MAX_SIZE];
static K_SEM_DEFINE(dma_done_sem, 0, 1);

static void dma_cb(const struct device *dev, void *arg, uint32_t id, int status)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(arg);
	ARG_UNUSED(id);
	ARG_UNUSED(status);
	k_sem_give(&dma_done_sem);
}

static void dma_run_once(const struct device *dev, size_t size)
{
	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.dma_callback = dma_cb,
		.complete_callback_en = 1,
		.error_callback_dis = 0,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
	};
	struct dma_block_config block = {
		.source_address = (uint32_t)dma_src,
		.dest_address = (uint32_t)dma_dst,
		.block_size = size,
	};

	cfg.head_block = &block;

	/* The driver may emit more than one callback per transfer (block and
	 * completion); clear stale counts before each start. */
	k_sem_reset(&dma_done_sem);
	zassert_ok(dma_config(dev, DMA_CHANNEL, &cfg), "dma_config size=%u",
		   (unsigned)size);
	zassert_ok(dma_start(dev, DMA_CHANNEL), "dma_start size=%u",
		   (unsigned)size);
	zassert_ok(k_sem_take(&dma_done_sem, K_MSEC(2000)),
		   "dma timeout size=%u", (unsigned)size);
}

ZTEST(perf_dma, mem2mem)
{
	const struct device *dev = DEVICE_DT_GET(DMA_NODE);
	static const size_t totals[] = { KB(1), KB(4), KB(16) };

	zassert_true(device_is_ready(dev), "dmac1 not ready");

	for (int i = 0; i < (int)sizeof(dma_src); i++) {
		dma_src[i] = (uint8_t)i;
	}

	for (size_t s = 0; s < ARRAY_SIZE(totals); s++) {
		size_t total = totals[s];
		size_t blocks = total / DMA_BLOCK_SIZE;
		uint64_t t0, t1;

		memset(dma_dst, 0, total);
		t0 = k_cycle_get_64();
		for (size_t b = 0; b < blocks; b++) {
			dma_run_once(dev, DMA_BLOCK_SIZE);
		}
		t1 = k_cycle_get_64();

		zassert_mem_equal(dma_src, dma_dst, DMA_BLOCK_SIZE,
				  "dma data mismatch total=%u", (unsigned)total);
		perf_bytes("dma", "mem2mem", total, k_cyc_to_us_floor64(t1 - t0));
	}
}

ZTEST_SUITE(perf_dma, NULL, NULL, NULL, NULL, NULL);
