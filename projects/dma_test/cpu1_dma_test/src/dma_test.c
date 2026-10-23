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
#include <errno.h>
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
	 * 通道生命周期（config → start → 完成 → stop → 复用）。 */
	__nocache __aligned(32) static uint8_t src_buf[TRANSFER_SIZE];
	__nocache __aligned(32) static uint8_t dst_buf[TRANSFER_SIZE];

	for (int i = 0; i < TRANSFER_SIZE; i++) {
		src_buf[i] = (uint8_t)(0x80 + i);
	}
	memset(dst_buf, 0x00, sizeof(dst_buf));
	dma_done = false;
	dma_status = -1;

	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.dma_callback = dma_callback,
		.complete_callback_en = 1,   /* 使完成可观测（旧版没开，无从等完成） */
		.error_callback_dis = 0,
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

	/* ★ 等完成 + 数据比对：把"ACTIVE→IDLE 迁移"从**不可观测**变成可归因的
	 * 数据证据。旧版只断 3 个 ret==0 —— 256B 传输 µs 级完成、硬件自清 ChEn，
	 * `dma_stop` 走早退分支同返 0，宣称的迁移根本没被验证（审计 A 组 P1-4）。 */
	for (int t = 0; t < 200 && !dma_done; t++) {
		k_msleep(1);
	}
	zassert_true(dma_done, "transfer did not complete in 200ms");
	zassert_mem_equal(src_buf, dst_buf, TRANSFER_SIZE, "lifecycle data mismatch");

	ret = dma_stop(dma_dev, DMA_CHANNEL);
	zassert_equal(ret, 0, "dma_stop failed: %d", ret);

	/* stop 后同通道必须可重新 config/start 并完成（状态确已回到可配置态） */
	memset(dst_buf, 0x00, sizeof(dst_buf));
	dma_done = false;
	zassert_ok(dma_config(dma_dev, DMA_CHANNEL, &cfg), "reconfig after stop failed");
	zassert_ok(dma_start(dma_dev, DMA_CHANNEL), "restart after stop failed");
	for (int t = 0; t < 200 && !dma_done; t++) {
		k_msleep(1);
	}
	zassert_true(dma_done, "post-stop transfer did not complete");
	zassert_mem_equal(src_buf, dst_buf, TRANSFER_SIZE, "post-stop data mismatch");
}

/* TC-4: invalid channel number returns error */
ZTEST(dma, test_dma_invalid_channel)
{
	__nocache __aligned(32) static uint8_t src[TRANSFER_SIZE];
	__nocache __aligned(32) static uint8_t dst[TRANSFER_SIZE];

	memset(src, 0x11, sizeof(src));
	memset(dst, 0x00, sizeof(dst));

	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.source_data_size = 1,
		.dest_data_size = 1,
		/* burst_length 必须显式给 1：缺省为 0 会触发 dw_dma_config() 的
		 * `source_burst_length > 0` 断言（kernel panic），本项会在
		 * 正对照阶段就被打断。 */
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
	};
	struct dma_block_config block = {
		.source_address = (uint32_t)src,
		.dest_address = (uint32_t)dst,   /* 与 src 不同地址 */
		.block_size = TRANSFER_SIZE,
	};
	cfg.head_block = &block;

	/* ★ 正对照 + 精确错误码（审计 A 组 P1-4）：旧版 src==dst，驱动同址校验
	 * 也返 -EINVAL ⇒ 删掉通道越界守卫后本项仍 PASS，不可归因。先证明同一 cfg
	 * 在合法通道号被接受，再要求 ch99 返回**精确的 -EINVAL**。 */
	int ret = dma_config(dma_dev, DMA_CHANNEL, &cfg);
	zassert_equal(ret, 0, "positive control: valid channel rejected: %d", ret);

	ret = dma_config(dma_dev, 99, &cfg);
	ZT_INFO("test_dma_invalid_channel", "dma_config(99) ret=%d", ret);
	zassert_equal(ret, -EINVAL, "dma_config ch99 ret %d != -EINVAL", ret);
}

/* ==================================================================
 * P1a 新增用例（2026-09-17）
 *
 * 覆盖既有 4 项之外的驱动路径：多块 LLI 链、多通道并发、dmac2 实例。
 * 每项自建前置（各自 config/start），可独立通过。
 * ================================================================== */

/* dmac2（0x40042000，与 dmac1 同 compatible、同 8 通道）：
 * 既有用例全部只跑 dmac1；本项需 app.overlay 追加 `&dmac2 { status="okay"; };`。 */
#define DMA2_NODE DT_NODELABEL(dmac2)

static volatile bool dma2_done;
static volatile int dma2_status;

static void dma2_callback(const struct device *dev, void *arg,
			  uint32_t id, int status)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(arg);
	ARG_UNUSED(id);
	dma2_status = status;
	dma2_done = true;
}

/* 多通道并发：按通道号分别记账（回调的 id = channel 号，见 dw_dma_isr） */
static volatile bool dma_ch_done[2];
static volatile int dma_ch_status[2];

static void dma_ch_callback(const struct device *dev, void *arg,
			    uint32_t id, int status)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(arg);

	if (id < 2) {
		dma_ch_status[id] = status;
		dma_ch_done[id] = true;
	}
}

/* TC-5: LLI 多块 scatter-gather（block_count=2）—— 覆盖 dw_dma_config 的
 *   多块循环与描述符 lp 链接（dma_dw_common.c:265-506）、dw_dma_start 的
 *   LLP 写回（:611-624）。pool 上限 = CONFIG_DMA_DW_LLI_POOL_SIZE = 2。 */
ZTEST(dma, test_dma_lli_chain)
{
	__nocache __aligned(32) static uint8_t src[128];
	__nocache __aligned(32) static uint8_t dst[128];

	for (int i = 0; i < 128; i++) {
		src[i] = (uint8_t)(0x40 + i);
	}
	memset(dst, 0, sizeof(dst));
	dma_done = false;
	dma_status = -1;

	/* 两块各 64B：src[0..63] → dst[0..63]，src[64..127] → dst[64..127] */
	struct dma_block_config b1 = {
		.source_address = (uint32_t)(src + 64),
		.dest_address = (uint32_t)(dst + 64),
		.block_size = 64,
		.next_block = NULL,
	};
	struct dma_block_config b0 = {
		.source_address = (uint32_t)src,
		.dest_address = (uint32_t)dst,
		.block_size = 64,
		.next_block = &b1,
	};
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
		.block_count = 2,
		.head_block = &b0,
	};

	int ret = dma_config(dma_dev, DMA_CHANNEL, &cfg);

	zassert_equal(ret, 0, "dma_config(2 blocks) failed: %d", ret);

	ret = dma_start(dma_dev, DMA_CHANNEL);
	zassert_equal(ret, 0, "dma_start(2 blocks) failed: %d", ret);

	for (int t = 0; t < 200 && !dma_done; t++) {
		k_msleep(1);
	}
	zassert_true(dma_done, "LLI chain transfer timeout");

	ZT_INFO("test_dma_lli_chain", "blocks=2,block_size=64,status=%d", dma_status);
	zassert_mem_equal(src, dst, sizeof(src), "LLI chain data mismatch");
}

/* TC-6: 多通道同时传输（ch0 + ch1），验证通道状态互相隔离、数据不串扰。 */
ZTEST(dma, test_dma_multi_channel_concurrent)
{
	__nocache __aligned(32) static uint8_t src0[128];
	__nocache __aligned(32) static uint8_t dst0[128];
	__nocache __aligned(32) static uint8_t src1[128];
	__nocache __aligned(32) static uint8_t dst1[128];

	for (int i = 0; i < 128; i++) {
		src0[i] = (uint8_t)i;
		src1[i] = (uint8_t)(0xFF - i);
	}
	memset(dst0, 0xA5, sizeof(dst0));
	memset(dst1, 0x5A, sizeof(dst1));
	dma_ch_done[0] = dma_ch_done[1] = false;
	dma_ch_status[0] = dma_ch_status[1] = -1;

	struct dma_config cfg0 = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.dma_callback = dma_ch_callback,
		.complete_callback_en = 1,
		.error_callback_dis = 0,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
	};
	struct dma_block_config blk0 = {
		.source_address = (uint32_t)src0,
		.dest_address = (uint32_t)dst0,
		.block_size = 128,
	};
	struct dma_config cfg1 = cfg0;
	struct dma_block_config blk1 = {
		.source_address = (uint32_t)src1,
		.dest_address = (uint32_t)dst1,
		.block_size = 128,
	};

	cfg0.head_block = &blk0;
	cfg1.head_block = &blk1;

	zassert_ok(dma_config(dma_dev, 0, &cfg0), "ch0 config failed");
	zassert_ok(dma_config(dma_dev, 1, &cfg1), "ch1 config failed");
	zassert_ok(dma_start(dma_dev, 0), "ch0 start failed");
	zassert_ok(dma_start(dma_dev, 1), "ch1 start failed");

	for (int t = 0; t < 200 && !(dma_ch_done[0] && dma_ch_done[1]); t++) {
		k_msleep(1);
	}

	ZT_INFO("test_dma_multi_channel_concurrent", "done0=%d,done1=%d,st0=%d,st1=%d",
		(int)dma_ch_done[0], (int)dma_ch_done[1],
		dma_ch_status[0], dma_ch_status[1]);
	zassert_true(dma_ch_done[0], "ch0 transfer did not complete");
	zassert_true(dma_ch_done[1], "ch1 transfer did not complete");
	zassert_mem_equal(src0, dst0, sizeof(src0), "ch0 data mismatch (crosstalk?)");
	zassert_mem_equal(src1, dst1, sizeof(src1), "ch1 data mismatch (crosstalk?)");

	/* 释放两个通道，避免影响后续用例 */
	dma_stop(dma_dev, 0);
	dma_stop(dma_dev, 1);
}

/* TC-7: dmac2 控制器 M2M 传输（第二个 DW DMAC 实例，既有用例只跑 dmac1）。 */
ZTEST(dma, test_dma_dmac2_transfer)
{
	const struct device *dev2 = DEVICE_DT_GET(DMA2_NODE);
	__nocache __aligned(32) static uint8_t src[256];
	__nocache __aligned(32) static uint8_t dst[256];

	zassert_true(device_is_ready(dev2),
		     "dmac2 not ready (app.overlay 需 &dmac2 status=okay?)");

	for (int i = 0; i < 256; i++) {
		src[i] = (uint8_t)(0x80 + i);
	}
	memset(dst, 0, sizeof(dst));
	dma2_done = false;
	dma2_status = -1;

	struct dma_config cfg = {
		.dma_slot = 0,
		.channel_direction = MEMORY_TO_MEMORY,
		.dma_callback = dma2_callback,
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
		.block_size = 256,
	};

	cfg.head_block = &block;

	int ret = dma_config(dev2, 0, &cfg);

	zassert_equal(ret, 0, "dmac2 dma_config failed: %d", ret);

	ret = dma_start(dev2, 0);
	zassert_equal(ret, 0, "dmac2 dma_start failed: %d", ret);

	for (int t = 0; t < 200 && !dma2_done; t++) {
		k_msleep(1);
	}
	zassert_true(dma2_done, "dmac2 transfer timeout");

	ZT_INFO("test_dma_dmac2_transfer", "bytes=256,status=%d", dma2_status);
	zassert_mem_equal(src, dst, sizeof(src), "dmac2 data mismatch");

	dma_stop(dev2, 0);
}

ZTEST_SUITE(dma, NULL, dma_test_setup, NULL, NULL, NULL);
