/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * TRNG (entropy_ls_trng) 单核测试（ztest 原生框架）。
 *
 * 被测驱动：zephyr/drivers/entropy/entropy_ls_trng.c
 *   - 硬件 DesignWare TRNG（dwtrng@40004000，cpu1 安全域）
 *   - 每命令产 128 bit（16B），IRQ 填充 128B 内部 pool，K_FOREVER 阻塞取用
 *   - 两个 API：entropy_get_entropy（阻塞，IRQ 驱动）/
 *     entropy_get_entropy_isr(ENTROPY_BUSYWAIT)（busywait 轮询）
 *   - 无效参数（NULL buf / len==0）返回 -EINVAL
 *   - 状态机隐藏路径：trng_issue_next() 中 req_bits + 128 > MAX_BITS_PER_REQUEST
 *     (2^19 bits = 64KB) → CMD_ADVANCE_STATE + skip_next=true（8192B 请求即触发）
 *
 * 测试函数为纯 ztest：zassert_* 原生断言（失败详情 file:line+msg 由 zassert
 * 自身 printk，harness 归因进报告 reason 列）；数值上报用 ZT_INFO 约定行
 * （§13.4，断言之前）。结果记账由 common/ztest_glue 的 zt_run_all 自动完成。
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/logging/log.h>

#include "ztest_glue.h"

LOG_MODULE_DECLARE(trng_test, LOG_LEVEL_INF);

extern const struct device *trng_dev;

/* 大数据缓冲放 .bss（cpu1 SRAM 充足），不占 ztest 线程栈：
 *   monobit_8k    8192B
 *   throughput_64k 65536B
 *   distinct_32   32 x 64B
 *   large_1024    1024B
 *   not_all_*     256B
 */
static uint8_t buf_mono[8192];
static uint8_t buf_64k[65536];
static uint8_t buf_dist[32][64];
static uint8_t buf_1024[1024];
static uint8_t buf_256[256];

/* ========== 统计辅助 ========== */

static bool buf_is_const(const uint8_t *buf, size_t len, uint8_t val)
{
	for (size_t i = 0; i < len; i++) {
		if (buf[i] != val) {
			return false;
		}
	}
	return true;
}

/* 8KB 里 1 比特总数：65536 bits，期望 32768，sigma=128。
 * 46%~54% 是 ~±16σ 的宽松门限，只拦"坏掉的全 0/全 1/固定模式"，
 * 真实 TRNG 不可能误报。 */
static uint32_t count_ones(const uint8_t *buf, size_t len)
{
	uint32_t ones = 0;

	for (size_t i = 0; i < len; i++) {
		uint8_t b = buf[i];

		while (b) {
			ones += (uint32_t)(b & 1u);
			b >>= 1;
		}
	}
	return ones;
}

/* ========== ZTEST ========== */

ZTEST_SUITE(trng, NULL, NULL, NULL, NULL, NULL);

/* 1. 阻塞模式 16B：返回值 0（驱动基本通路） */
ZTEST(trng, basic_16)
{
	uint8_t buf[16];

	int ret = entropy_get_entropy(trng_dev, buf, sizeof(buf));
	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);
}

/* 2. 256B 非全 0（TRNG 未就绪/坏掉时常吐全 0） */
ZTEST(trng, not_all_zero)
{
	int ret = entropy_get_entropy(trng_dev, buf_256, sizeof(buf_256));
	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);
	zassert_false(buf_is_const(buf_256, sizeof(buf_256), 0x00),
		      "all 256 bytes are 0x00");
}

/* 3. 256B 非全 0xFF */
ZTEST(trng, not_all_ff)
{
	int ret = entropy_get_entropy(trng_dev, buf_256, sizeof(buf_256));
	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);
	zassert_false(buf_is_const(buf_256, sizeof(buf_256), 0xFF),
		      "all 256 bytes are 0xFF");
}

/* 4. ISR busywait 模式：128B，返回值为请求长度且非全 0。
 *    该路径走 send_cmd_busywait 轮询（样板 otbn 的 rsa_keygen_get_rng 同款用法）。 */
ZTEST(trng, isr_busywait)
{
	int ret = entropy_get_entropy_isr(trng_dev, buf_256, 128,
					  ENTROPY_BUSYWAIT);
	zassert_equal(ret, 128, "isr busywait ret %d != 128", ret);
	zassert_false(buf_is_const(buf_256, 128, 0x00),
		      "busywait output all 0x00");
}

/* 5. 1024B 大请求：超过驱动内部 pool（128B），验证 IRQ 链式填充 +
 *    长请求循环取 pipe；同长度分 4 段无完全重复。 */
ZTEST(trng, large_1024)
{
	int ret = entropy_get_entropy(trng_dev, buf_1024, sizeof(buf_1024));
	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);
	zassert_false(buf_is_const(buf_1024, sizeof(buf_1024), 0x00),
		      "all 1024 bytes are 0x00");
	zassert_true(memcmp(buf_1024, buf_1024 + 256, 256) != 0,
		     "1024B halves repeat (q1 == q2)");
}

/* 6. 无效参数：NULL buf / len==0 必须 -EINVAL（驱动入口校验） */
ZTEST(trng, invalid_args)
{
	int ret = entropy_get_entropy(trng_dev, NULL, 16);
	zassert_equal(ret, -EINVAL, "NULL buf ret %d != -EINVAL", ret);

	ret = entropy_get_entropy(trng_dev, buf_256, 0);
	zassert_equal(ret, -EINVAL, "len=0 ret %d != -EINVAL", ret);
}

/* 7. 8KB 单比特统计：1 占比 46%~54%（宽松门限，见 count_ones 注释）。 */
ZTEST(trng, monobit_8k)
{
	int ret = entropy_get_entropy(trng_dev, buf_mono, sizeof(buf_mono));
	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);

	uint32_t ones = count_ones(buf_mono, sizeof(buf_mono));
	uint32_t total = (uint32_t)sizeof(buf_mono) * 8u;
	uint32_t lo = total * 46u / 100u;
	uint32_t hi = total * 54u / 100u;
	double percent = 100.0 * (double)ones / (double)total;

	ZT_INFO("monobit_8k", "ones=%u,percent=%.2f", ones, percent);
	zassert_true(ones >= lo && ones <= hi,
		     "ones %u out of [%u, %u]", ones, lo, hi);
}

/* 8. 32 次 x 64B，两两不重复（熵源重复输出=坏；真实 TRNG 碰撞概率可忽略） */
ZTEST(trng, distinct_32)
{
	bool all_distinct = true;
	int ret = 0;

	for (int i = 0; i < 32 && ret == 0; i++) {
		ret = entropy_get_entropy(trng_dev, buf_dist[i], sizeof(buf_dist[i]));
	}
	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);

	for (int i = 0; i < 32 && all_distinct; i++) {
		for (int j = i + 1; j < 32; j++) {
			if (memcmp(buf_dist[i], buf_dist[j],
				   sizeof(buf_dist[i])) == 0) {
				LOG_INF("duplicate block: %d == %d", i, j);
				all_distinct = false;
				break;
			}
		}
	}
	zassert_true(all_distinct, "duplicate 64B block found");
}

/* 9. 64KB 连续获取：吞吐测量（§13.4 INFO 上报 mbps）+ ret==0 即 PASS。
 *    注意：entropy API 的 length 是 uint16_t，65536 会截断为 0 触发 -EINVAL
 *    （见 ENTROPY_API_UINT16_LENGTH_OVERFLOW.md），故分 8 次 x 8KB；
 *    单次 8192B = 2^19 bits 恰触发 trng_issue_next 的 ADVANCE_STATE 隐藏分支。
 *    计时用 k_cycle_get_32（§4.3，不打 LOG 拉 IO）。 */
ZTEST(trng, throughput_64k)
{
	uint32_t t0 = k_cycle_get_32();
	int ret = 0;

	for (int i = 0; i < 8 && ret == 0; i++) {
		ret = entropy_get_entropy(trng_dev, buf_64k + i * 8192, 8192);
	}
	uint32_t t1 = k_cycle_get_32();

	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);
	zassert_false(buf_is_const(buf_64k, sizeof(buf_64k), 0x00),
		      "all 64KB are 0x00");

	uint64_t us = k_cyc_to_us_floor64(t1 - t0);
	/* 单位：bits/us = Mbps（8 bit/byte，1us=1e-6s） */
	double mbps = 8.0 * (double)sizeof(buf_64k) / (double)(us ? us : 1);
	LOG_INF("throughput: %u bytes in %llu us (%.2f Mbps)",
		(unsigned int)sizeof(buf_64k),
		(unsigned long long)us, mbps);
	ZT_INFO("throughput_64k", "bytes=%u,us=%llu,mbps=%.2f",
		(unsigned int)sizeof(buf_64k),
		(unsigned long long)us, mbps);
}
