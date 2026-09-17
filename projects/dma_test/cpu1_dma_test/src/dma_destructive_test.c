/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DMA destructive/robustness tests (LinkedSemi DMA driver, dmac1)
 *
 * 测试 DMA 驱动在异常参数/边界条件下的鲁棒性：
 *   - 无效参数拒绝（data_size、block_size、同址传输）
 *   - 通道忙碌时重配置
 *   - 极小/极大传输
 *
 * 驱动约束（dma_dw_common.c）：
 *   - source_data_size 必须在 {1,2,4,8,16}
 *   - block_size 不能 > DW_CTLH_BLOCK_TS_MASK（该 IP 的 BLOCK_TS 仅 CTL_HI[10:0] 有效，即 2047）
 *   - source_address 与 dest_address 不能相同（同址属退化配置，显式返回 -EINVAL）
 *   - 忙碌通道重配置返回 -EBUSY
 *
 * 负例的 src/dst 一律用两个不同缓冲区，使每项只压测自己那一维约束，
 * 不被同址校验提前拦下。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>
#include "ztest_glue.h"

#define DMA_NODE DT_NODELABEL(dmac1)
#define DMA_CHANNEL 0

static const struct device *dma_dev;
static volatile bool dma_done;
static volatile int dma_status;

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

static void dma_test_before(void *fixture)
{
	ARG_UNUSED(fixture);
	/* Ensure channel is released before each test (previous test may have
	 * left it active due to timeout or missing cleanup). */
	dma_stop(dma_dev, DMA_CHANNEL);
	dma_done = false;
	dma_status = -1;
}

static void dma_wait_complete(int timeout_ms)
{
	for (int t = 0; t < timeout_ms && !dma_done; t++) {
		k_msleep(1);
	}
	if (!dma_done) {
		/* Transfer timed out - stop channel to release it for next test */
		dma_stop(dma_dev, DMA_CHANNEL);
	}
}

/* TC-1: invalid data_size (3) should be rejected */
ZTEST(dma_destructive, test_dma_invalid_data_size)
{
	__nocache __aligned(32) static uint8_t src[64];
	__nocache __aligned(32) static uint8_t dst[64];
	memset(src, 0, sizeof(src));
	memset(dst, 0, sizeof(dst));

	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.dma_callback = dma_callback,
		.complete_callback_en = 1,
		.error_callback_dis = 0,
		.source_data_size = 3,   /* invalid: not in {1,2,4,8,16} */
		.dest_data_size = 3,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
	};
	struct dma_block_config block = {
		.source_address = (uint32_t)src,
		.dest_address = (uint32_t)dst,
		.block_size = 64,
	};
	cfg.head_block = &block;

	int ret = dma_config(dma_dev, DMA_CHANNEL, &cfg);
	zassert_not_equal(ret, 0, "dma_config with data_size=3 should fail");
}

/* TC-2: data_size=0 should be rejected */
ZTEST(dma_destructive, test_dma_data_size_zero)
{
	__nocache __aligned(32) static uint8_t src[64];
	__nocache __aligned(32) static uint8_t dst[64];
	memset(src, 0, sizeof(src));
	memset(dst, 0, sizeof(dst));

	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.dma_callback = dma_callback,
		.complete_callback_en = 1,
		.error_callback_dis = 0,
		.source_data_size = 0,   /* invalid */
		.dest_data_size = 0,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
	};
	struct dma_block_config block = {
		.source_address = (uint32_t)src,
		.dest_address = (uint32_t)dst,
		.block_size = 64,
	};
	cfg.head_block = &block;

	int ret = dma_config(dma_dev, DMA_CHANNEL, &cfg);
	zassert_not_equal(ret, 0, "dma_config with data_size=0 should fail");
}

/* TC-3: block_size > DW_CTLH_BLOCK_TS_MASK (0x7FF = 2047) should be rejected */
ZTEST(dma_destructive, test_dma_block_too_large)
{
	__nocache __aligned(32) static uint8_t src[256];
	__nocache __aligned(32) static uint8_t dst[256];
	memset(src, 0, sizeof(src));
	memset(dst, 0, sizeof(dst));

	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.dma_callback = dma_callback,
		.complete_callback_en = 1,
		.error_callback_dis = 0,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
	};
	struct dma_block_config block = {
		.source_address = (uint32_t)src,
		.dest_address = (uint32_t)dst,
		.block_size = 70000,   /* > 2047 */
	};
	cfg.head_block = &block;

	int ret = dma_config(dma_dev, DMA_CHANNEL, &cfg);
	zassert_not_equal(ret, 0, "dma_config with block_size=70000 should fail");
}

/* TC-4: 同址传输（src == dst）属退化配置，驱动须显式拒绝 */
ZTEST(dma_destructive, test_dma_self_copy_rejected)
{
	__nocache __aligned(32) static uint8_t buf[64];
	for (int i = 0; i < 64; i++) {
		buf[i] = (uint8_t)i;
	}

	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.dma_callback = dma_callback,
		.complete_callback_en = 1,
		.error_callback_dis = 0,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
	};
	struct dma_block_config block = {
		.source_address = (uint32_t)buf,
		.dest_address = (uint32_t)buf,  /* src == dst */
		.block_size = 64,
	};
	cfg.head_block = &block;

	int ret = dma_config(dma_dev, DMA_CHANNEL, &cfg);
	zassert_equal(ret, -EINVAL, "dma_config src==dst should be rejected: %d", ret);

	/* 拒绝发生在参数校验阶段，通道未被占用、缓冲区不应被改动 */
	for (int i = 0; i < 64; i++) {
		zassert_equal(buf[i], (uint8_t)i, "rejected config corrupted byte[%d]", i);
	}
}

/* TC-5: minimal transfer (1 byte) */
ZTEST(dma_destructive, test_dma_one_byte)
{
	__nocache __aligned(32) static uint8_t src[32] = {0};
	__nocache __aligned(32) static uint8_t dst[32] = {0};
	src[0] = 0xAB;
	dma_done = false;
	dma_status = -1;

	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.dma_callback = dma_callback,
		.complete_callback_en = 1,
		.error_callback_dis = 0,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
	};
	struct dma_block_config block = {
		.source_address = (uint32_t)src,
		.dest_address = (uint32_t)dst,
		.block_size = 1,   /* minimal */
	};
	cfg.head_block = &block;

	int ret = dma_config(dma_dev, DMA_CHANNEL, &cfg);
	zassert_equal(ret, 0, "dma_config failed: %d", ret);
	ret = dma_start(dma_dev, DMA_CHANNEL);
	zassert_equal(ret, 0, "dma_start failed: %d", ret);

	dma_wait_complete(200);
	zassert_true(dma_done, "1-byte transfer timed out");
	zassert_equal(dst[0], 0xAB, "1-byte transfer data mismatch");
}

/* TC-6: 最大合法单块传输（BLOCK_TS 实际有效位宽 11 位，单块上限 2047 字节） */
ZTEST(dma_destructive, test_dma_large_block)
{
	/* block_size 上限 2047：BLOCK_TS 的 bit11 无效，超限值被硬件截断
	 * （2048 → 0 → 通道挂住；其余只搬运 block_size & 0x7FF 次）。 */
	__nocache __aligned(32) static uint8_t large_src[4096];
	__nocache __aligned(32) static uint8_t large_dst[4096];
	memset(large_src, 0x55, sizeof(large_src));
	memset(large_dst, 0xAA, sizeof(large_dst));
	dma_done = false;
	dma_status = -1;

	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.dma_callback = dma_callback,
		.complete_callback_en = 1,
		.error_callback_dis = 0,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
	};
	struct dma_block_config block = {
		.source_address = (uint32_t)large_src,
		.dest_address = (uint32_t)large_dst,
		.block_size = 2047,  /* BLOCK_TS[10:0] 最大值 */
	};
	cfg.head_block = &block;

	int ret = dma_config(dma_dev, DMA_CHANNEL, &cfg);
	zassert_equal(ret, 0, "dma_config failed for 2047-byte block: %d", ret);
	ret = dma_start(dma_dev, DMA_CHANNEL);
	zassert_equal(ret, 0, "dma_start failed: %d", ret);

	dma_wait_complete(2000);
	zassert_true(dma_done, "2047-byte transfer timed out");
	/* verify first and last bytes */
	zassert_equal(large_dst[0], 0x55, "first byte mismatch");
	zassert_equal(large_dst[2046], 0x55, "last byte mismatch");
}

ZTEST_SUITE(dma_destructive, NULL, dma_test_setup, dma_test_before, NULL, NULL);
