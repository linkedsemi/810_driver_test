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

/* ==================================================================
 * P1a 新增用例（2026-09-17）
 * ================================================================== */

/* TC-7: LLI pool 耗尽边界 —— block_count > CONFIG_DMA_DW_LLI_POOL_SIZE(=2)。
 *   覆盖 dw_dma_config 的 pool 容量检查分支（dma_dw_common.c:226-232）；
 *   拒绝发生在任何状态/描述符改写之前，故通道与缓冲区必须保持原样。 */
ZTEST(dma_destructive, test_dma_lli_pool_exhaust)
{
	__nocache __aligned(32) static uint8_t src[256];
	__nocache __aligned(32) static uint8_t dst[256];

	memset(src, 0x11, sizeof(src));
	memset(dst, 0x00, sizeof(dst));

	struct dma_block_config b2 = {
		.source_address = (uint32_t)(src + 128),
		.dest_address = (uint32_t)(dst + 128),
		.block_size = 64,
		.next_block = NULL,
	};
	struct dma_block_config b1 = {
		.source_address = (uint32_t)(src + 64),
		.dest_address = (uint32_t)(dst + 64),
		.block_size = 64,
		.next_block = &b2,
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
		.block_count = 3,   /* pool = CONFIG_DMA_DW_LLI_POOL_SIZE = 2 */
		.head_block = &b0,
	};

	int ret = dma_config(dma_dev, DMA_CHANNEL, &cfg);

	ZT_INFO("test_dma_lli_pool_exhaust", "block_count=3,pool=2,ret=%d", ret);
	zassert_equal(ret, -EINVAL,
		      "block_count=3 > pool 2 should be -EINVAL, got %d", ret);

	/* 参数校验阶段即拒绝：通道未被占用，缓冲区不应被改动 */
	for (int i = 0; i < 256; i++) {
		zassert_equal(dst[i], 0x00, "rejected config modified dst[%d]", i);
	}

	/* 拒绝后通道仍可用（状态未被污染） */
	__nocache __aligned(32) static uint8_t src2[64];
	__nocache __aligned(32) static uint8_t dst2[64];
	struct dma_block_config ok = {
		.source_address = (uint32_t)src2,
		.dest_address = (uint32_t)dst2,
		.block_size = 64,
	};

	cfg.block_count = 1;
	cfg.head_block = &ok;
	dma_done = false;
	zassert_ok(dma_config(dma_dev, DMA_CHANNEL, &cfg),
		   "channel unusable after pool-exhaust rejection");
	zassert_ok(dma_start(dma_dev, DMA_CHANNEL));
	dma_wait_complete(200);
	zassert_true(dma_done, "post-rejection transfer timed out");
}

/* TC-8: 传输中途 stop/abort（中断/时序）。断言不依赖"stop 时传输是否已结束"：
 *   已完成 → dw_dma_stop 走 !dw_dma_is_enabled() 早退返回 0；
 *   在途 → 走 disable + WAIT_FOR(ChEn 清) 返回 0。两条路径都必须返回 0，
 *   且通道必须仍可重新 config/start 并完成。 */
ZTEST(dma_destructive, test_dma_abort_mid_transfer)
{
	__nocache __aligned(32) static uint8_t big_src[2048];
	__nocache __aligned(32) static uint8_t big_dst[2048];

	memset(big_src, 0x3C, sizeof(big_src));
	memset(big_dst, 0x00, sizeof(big_dst));
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
		.source_address = (uint32_t)big_src,
		.dest_address = (uint32_t)big_dst,
		.block_size = 2047,   /* 单块上限，尽量拉长在途窗口 */
	};

	cfg.head_block = &block;
	zassert_ok(dma_config(dma_dev, DMA_CHANNEL, &cfg));
	zassert_ok(dma_start(dma_dev, DMA_CHANNEL));

	int ret = dma_stop(dma_dev, DMA_CHANNEL);

	ZT_INFO("test_dma_abort_mid_transfer", "stop_ret=%d", ret);
	zassert_equal(ret, 0, "dma_stop (mid/after transfer) ret %d", ret);

	/* ★ 覆盖边界如实标注（审计 A 组 P1-4）：2047B M2M 传输 µs 级完成 + 硬件
	 * 在 TFR 后自清 ChEn ⇒ 本项实际走 `dw_dma_stop` 的
	 * `!dw_dma_is_enabled()` **早退分支**；在途 disable 分支与早退同返 0，
	 * 故本项**不能证明**"传输在途被 abort"（big_dst 多半已搬完，亦不可作判据）。
	 * 可确定覆盖的只有"stop 返回 0 且通道随后可复用"这一**幂等早退路径**：
	 * 重复 stop 必须仍返回 0。 */
	zassert_equal(dma_stop(dma_dev, DMA_CHANNEL), 0,
		      "second stop (idempotent early-exit path) ret != 0");

	/* 通道已释放：可再次 config + start 并完成 */
	__nocache __aligned(32) static uint8_t src2[256];
	__nocache __aligned(32) static uint8_t dst2[256];

	for (int i = 0; i < 256; i++) {
		src2[i] = (uint8_t)i;
	}
	memset(dst2, 0, sizeof(dst2));
	dma_done = false;

	struct dma_block_config blk2 = {
		.source_address = (uint32_t)src2,
		.dest_address = (uint32_t)dst2,
		.block_size = 256,
	};

	cfg.head_block = &blk2;

	zassert_ok(dma_config(dma_dev, DMA_CHANNEL, &cfg),
		   "reconfig after abort failed");
	zassert_ok(dma_start(dma_dev, DMA_CHANNEL), "restart after abort failed");
	dma_wait_complete(200);
	zassert_true(dma_done, "post-abort transfer timed out");
	zassert_mem_equal(src2, dst2, sizeof(src2), "post-abort data mismatch");
}

/* TC-9: suspend/resume 状态守卫（既有零覆盖）。成功路径（ACTIVE→SUSPENDED）
 *   需在传输在途时命中，时序不可确定 ⇒ 只覆盖可确定的状态守卫分支：
 *   非 ACTIVE 时 suspend → -EINVAL；非 SUSPENDED 时 resume → -EINVAL；
 *   越界通道号两 API → -EINVAL（另在 ch0=ACTIVE 状态下复验，见下）。 */
ZTEST(dma_destructive, test_dma_suspend_resume_guards)
{
	int ret = dma_suspend(dma_dev, DMA_CHANNEL);

	ZT_INFO("test_dma_suspend_resume_guards", "suspend_idle_ret=%d", ret);
	zassert_equal(ret, -EINVAL,
		      "suspend while IDLE ret %d != -EINVAL", ret);

	ret = dma_resume(dma_dev, DMA_CHANNEL);
	ZT_INFO("test_dma_suspend_resume_guards", "resume_idle_ret=%d", ret);
	zassert_equal(ret, -EINVAL,
		      "resume while not SUSPENDED ret %d != -EINVAL", ret);

	zassert_equal(dma_suspend(dma_dev, 99), -EINVAL,
		      "suspend(ch99) should be -EINVAL");
	zassert_equal(dma_resume(dma_dev, 99), -EINVAL,
		      "resume(ch99) should be -EINVAL");

	/* ★ 越界守卫可归因化（审计 A 组 P1-4）：旧版 ch99 半段只断 -EINVAL ——
	 * 删掉 `channel >= DW_CHAN_COUNT` 守卫后，越界 `chan_data->state` 读到 0
	 * (=IDLE) 仍返 -EINVAL，断言照过、不可归因。改为：先令 **ch0 = ACTIVE**，
	 * 再调 ch99 两个 API，最后断言 ch0 状态**未被越界访问破坏**（仍可正常 stop
	 * 返回 0）。若越界访问写坏了 ch0 的 state（例如落在同一 cache line），
	 * dma_stop 将不再返回 0。 */
	__nocache __aligned(32) static uint8_t src[2048];
	__nocache __aligned(32) static uint8_t dst[2048];

	memset(src, 0x5A, sizeof(src));
	memset(dst, 0x00, sizeof(dst));
	dma_done = false;

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
		.block_size = 2047,
	};

	cfg.head_block = &block;
	zassert_ok(dma_config(dma_dev, DMA_CHANNEL, &cfg), "pre ch99 config failed");
	zassert_ok(dma_start(dma_dev, DMA_CHANNEL), "pre ch99 start failed");

	zassert_equal(dma_suspend(dma_dev, 99), -EINVAL,
		      "suspend(ch99) after active ch0 should be -EINVAL");
	zassert_equal(dma_resume(dma_dev, 99), -EINVAL,
		      "resume(ch99) after active ch0 should be -EINVAL");

	/* ch0 软件状态仍是 ACTIVE（越界调用不得改动它）⇒ dma_stop 必须返回 0 */
	zassert_equal(dma_stop(dma_dev, DMA_CHANNEL), 0,
		      "ch99 guard call corrupted ch0 state (stop ret != 0)");
}

ZTEST_SUITE(dma_destructive, NULL, dma_test_setup, dma_test_before, NULL, NULL);
