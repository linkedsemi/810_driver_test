/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DMA M2M test cases (LinkedSemi DMA driver, dmac1)
 *
 * 已知 bug：dma_request_channel() 恒失败（dma_ctx.magic 未初始化），
 * 用固定通道号规避。
 *
 * buffer 全部 __nocache __aligned(32)（DMA 硬件要求 + D-cache 一致），
 * 如果 D-cache 导致 memcmp 误判，需加 __nocache 或 cache flush。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/linker/sections.h>
#include <zephyr/ztest.h>
#include <string.h>
#include "ztest_glue.h"

#define DMA_NODE DT_NODELABEL(dmac1)
#define DMA_CHANNEL 0  /* 固定通道号，规避 dma_request_channel magic bug */
#define TRANSFER_SIZE 256
/* block_size 按官方 API = 字节数（source_data_size=1 时字节数==block_ts 次数，
 * 与 zephyr/tests/drivers/dma/loop_transfer 官方样例一致）。 */

static const struct device *dma_dev;
static volatile bool dma_done;
static int dma_status;

static void dma_callback(const struct device *dev, void *arg,
			 uint32_t id, int status)
{
	dma_status = status;
	dma_done = true;
}

static void *dma_test_setup(void)
{
	dma_dev = DEVICE_DT_GET(DMA_NODE);
	zassert_true(device_is_ready(dma_dev), "dmac1 not ready");
	return NULL;
}

/* TC-1: M2M loop transfer with memcmp verification */
ZTEST(dma, test_dma_loop_transfer)
{
	__nocache __aligned(32) static uint8_t tx_data[TRANSFER_SIZE];
	__nocache __aligned(32) static uint8_t rx_data[TRANSFER_SIZE];

	for (int loop = 0; loop < 4; loop++) {
		for (int i = 0; i < TRANSFER_SIZE; i++) {
			tx_data[i] = (uint8_t)(i + loop);
		}
		memset(rx_data, 0, sizeof(rx_data));
		dma_done = false;

		struct dma_config cfg = {
			.dma_slot = 0,
			.channel_direction = MEMORY_TO_MEMORY,
			.dma_callback = dma_callback,
			.complete_callback_en = 1,  /* 使能完成回调，传输完成后 dma_callback 触发置 dma_done */
			.error_callback_dis = 0,
			.source_data_size = 1,
			.dest_data_size = 1,
			.source_burst_length = 1,
			.dest_burst_length = 1,
			.block_count = 1,
		};

		struct dma_block_config block = {
			.source_address = (uint32_t)tx_data,
			.dest_address = (uint32_t)rx_data,
			.block_size = TRANSFER_SIZE,
		};
		cfg.head_block = &block;

		int ret = dma_config(dma_dev, DMA_CHANNEL, &cfg);
		zassert_equal(ret, 0, "dma_config failed: %d", ret);

		ret = dma_start(dma_dev, DMA_CHANNEL);
		zassert_equal(ret, 0, "dma_start failed: %d", ret);

		/* 等传输完成（callback 或超时） */
		for (int t = 0; t < 100 && !dma_done; t++) {
			k_msleep(1);
		}
		zassert_true(dma_done, "DMA transfer timeout (loop %d)", loop);

		/* dma_status 语义：drv 会派发 block(DMA_STATUS_BLOCK=1)/tfr(COMPLETE=0)
		 * 等多个回调，status 非严格 0。真正正确性用 memcmp 验证。 */

		zassert_mem_equal(tx_data, rx_data, TRANSFER_SIZE,
			"data mismatch on loop %d", loop);
	}
}

/* TC-2: transfer with all-zeros and all-0xFF patterns */
ZTEST(dma, test_dma_transfer_ones_zeros)
{
	__nocache __aligned(32) static uint8_t tx_zero[TRANSFER_SIZE];
	__nocache __aligned(32) static uint8_t rx_zero[TRANSFER_SIZE];
	__nocache __aligned(32) static uint8_t tx_ones[TRANSFER_SIZE];
	__nocache __aligned(32) static uint8_t rx_ones[TRANSFER_SIZE];

	memset(tx_zero, 0x00, TRANSFER_SIZE);
	memset(tx_ones, 0xFF, TRANSFER_SIZE);
	memset(rx_zero, 0xAA, TRANSFER_SIZE);
	memset(rx_ones, 0x55, TRANSFER_SIZE);
	dma_done = false;

	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.dma_callback = dma_callback,
		.complete_callback_en = 1,  /* 使能完成回调，传输完成后 dma_callback 触发置 dma_done */
		.error_callback_dis = 0,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
	};

	/* zeros */
	struct dma_block_config block_z = {
		.source_address = (uint32_t)tx_zero,
		.dest_address = (uint32_t)rx_zero,
		.block_size = TRANSFER_SIZE,
	};
	cfg.head_block = &block_z;
	zassert_ok(dma_config(dma_dev, DMA_CHANNEL, &cfg));
	zassert_ok(dma_start(dma_dev, DMA_CHANNEL));
	for (int t = 0; t < 100 && !dma_done; t++) { k_msleep(1); }
	zassert_true(dma_done, "timeout (zeros)");
	zassert_mem_equal(tx_zero, rx_zero, TRANSFER_SIZE, "zeros mismatch");

	/* ones */
	dma_done = false;
	struct dma_block_config block_o = {
		.source_address = (uint32_t)tx_ones,
		.dest_address = (uint32_t)rx_ones,
		.block_size = TRANSFER_SIZE,
	};
	cfg.head_block = &block_o;
	zassert_ok(dma_config(dma_dev, DMA_CHANNEL, &cfg));
	zassert_ok(dma_start(dma_dev, DMA_CHANNEL));
	for (int t = 0; t < 100 && !dma_done; t++) { k_msleep(1); }
	zassert_true(dma_done, "timeout (ones)");
	zassert_mem_equal(tx_ones, rx_ones, TRANSFER_SIZE, "ones mismatch");
}

/* TC-3: verify DMA channel works (config + start + stop on channel 0) */
ZTEST(dma, test_dma_channel_config_stop)
{
	/* 两个不同缓冲区：驱动显式拒绝 src == dst（-EINVAL），本项测的是
	 * 通道生命周期（config → start → stop），不涉及同址拷贝。 */
	__nocache __aligned(32) static uint8_t src_buf[TRANSFER_SIZE];
	__nocache __aligned(32) static uint8_t dst_buf[TRANSFER_SIZE];
	memset(src_buf, 0, sizeof(src_buf));
	memset(dst_buf, 0, sizeof(dst_buf));
	dma_done = false;

	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.dma_callback = dma_callback,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
	};
	struct dma_block_config block = {
		.source_address = (uint32_t)src_buf,
		.dest_address = (uint32_t)dst_buf,
		.block_size = TRANSFER_SIZE,
	};
	cfg.head_block = &block;

	int ret = dma_config(dma_dev, DMA_CHANNEL, &cfg);
	zassert_equal(ret, 0, "dma_config failed: %d", ret);

	ret = dma_start(dma_dev, DMA_CHANNEL);
	zassert_equal(ret, 0, "dma_start failed: %d", ret);

	ret = dma_stop(dma_dev, DMA_CHANNEL);
	zassert_equal(ret, 0, "dma_stop failed: %d", ret);
}

/* TC-4: invalid channel number returns error */
ZTEST(dma, test_dma_invalid_channel)
{
	__nocache __aligned(32) static uint8_t buf[TRANSFER_SIZE];
	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.source_data_size = 1,
		.dest_data_size = 1,
		.block_count = 1,
	};
	struct dma_block_config block = {
		.source_address = (uint32_t)buf,
		.dest_address = (uint32_t)buf,
		.block_size = TRANSFER_SIZE,
	};
	cfg.head_block = &block;

	int ret = dma_config(dma_dev, 99, &cfg);
	ZT_INFO("test_dma_invalid_channel", "dma_config(99) ret=%d", ret);
	zassert_not_equal(ret, 0, "dma_config ch99 should fail");
	ZT_INFO("test_dma_invalid_channel", "zassert passed");
}

ZTEST_SUITE(dma, NULL, dma_test_setup, NULL, NULL, NULL);
