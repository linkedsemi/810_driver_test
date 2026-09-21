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

/* 1. 阻塞模式 16B：返回值 0（驱动基本通路）+ 输出必须真被写入 */
ZTEST(trng, basic_16)
{
	uint8_t buf[16];

	/* 预置哨兵值：驱动"返回 0 但不写数据"（短写/静默成功）时输出仍是哨兵。
	 * 旧版只断 ret==0、一个字节都不校验 ⇒ 短写照样 PASS（审计 A 组 P1-3）。 */
	memset(buf, 0xA5, sizeof(buf));

	int ret = entropy_get_entropy(trng_dev, buf, sizeof(buf));
	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);
	zassert_false(buf_is_const(buf, sizeof(buf), 0xA5),
		      "16B 输出与预置哨兵 0xA5 完全相同：驱动返回成功但未写入数据");
}

/* 2. 256B 非全 0（TRNG 未就绪/坏掉时常吐全 0） */
ZTEST(trng, not_all_zero)
{
	/* buf_256 被更早执行的 isr_nonblock 用过；先清成哨兵，避免在"别人写的
	 * 旧数据"上判 PASS（审计 A 组 P1-3）。 */
	memset(buf_256, 0xA5, sizeof(buf_256));

	int ret = entropy_get_entropy(trng_dev, buf_256, sizeof(buf_256));
	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);
	zassert_false(buf_is_const(buf_256, sizeof(buf_256), 0xA5),
		      "输出仍为预置哨兵 0xA5：驱动返回成功但未写入数据");
	zassert_false(buf_is_const(buf_256, sizeof(buf_256), 0x00),
		      "all 256 bytes are 0x00");
}

/* 3. 256B 非全 0xFF */
ZTEST(trng, not_all_ff)
{
	memset(buf_256, 0xA5, sizeof(buf_256));   /* 同上：先清哨兵 */

	int ret = entropy_get_entropy(trng_dev, buf_256, sizeof(buf_256));
	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);
	zassert_false(buf_is_const(buf_256, sizeof(buf_256), 0xA5),
		      "输出仍为预置哨兵 0xA5：驱动返回成功但未写入数据");
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

/* ==================================================================
 * 后续用例独立缓冲区（不与其他用例共享，保证单项可独立通过）
 *   block_freq_8k / runs_8k / autocorr_8k  共用 buf_stat（各用例自行重取）
 *   long_seq_1m  滚动 8KB 复用缓冲（1MB 超出 SRAM，分片累计统计）
 *   concurrent_get 双线程各自 512B
 *   isr_nonblock  64B
 * ================================================================== */
static uint8_t buf_stat[8192];
static uint8_t buf_long[8192];
static uint8_t conc_buf[2][512];
static uint8_t buf_isr_nb[64];
static int conc_ret[2];
static struct k_thread conc_thread_a;
static struct k_thread conc_thread_b;
static K_THREAD_STACK_DEFINE(conc_stack_a, 4096);
static K_THREAD_STACK_DEFINE(conc_stack_b, 4096);

/* 取第 i 个 bit（LSB-first，与 count_ones 的位序口径一致） */
static inline uint32_t bit_get(const uint8_t *buf, size_t i)
{
	return (buf[i >> 3] >> (i & 7u)) & 1u;
}

/* 浮点绝对值：不引 libm（picolibc 下 sqrt/fabs 的链接依赖不确定） */
static inline double dabs(double x)
{
	return x < 0.0 ? -x : x;
}

/* 10. 块频测试（NIST SP800-22 §2.2）：n=65536 bit 分 M=128 块、每块 512 bit，
 *    χ² = 4·m·Σ(πi − 0.5)²，m=512 为块长(bit)。χ² 近似服从 χ²(M)，
 *    均值 = M = 128、标准差 = sqrt(2M) ≈ 16 ⇒ 取 ±6σ 门限 [32, 224]。
 *    门限只拦"分块后局部严重偏置"（如高位恒 1 / 周期性图案），真实 TRNG 不误报。 */
ZTEST(trng, block_freq_8k)
{
	int ret = entropy_get_entropy(trng_dev, buf_stat, sizeof(buf_stat));

	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);

	const size_t n_bits = sizeof(buf_stat) * 8u;   /* 65536 */
	const size_t blk_bits = 512u;                  /* NIST 推荐 M=128 时的块长 */
	const size_t n_blk = n_bits / blk_bits;        /* 128 */
	double sum = 0.0;

	for (size_t b = 0; b < n_blk; b++) {
		uint32_t ones = 0;

		for (size_t i = 0; i < blk_bits; i++) {
			ones += bit_get(buf_stat, b * blk_bits + i);
		}

		double pi = (double)ones / (double)blk_bits;

		sum += (pi - 0.5) * (pi - 0.5);
	}

	double chi2 = 4.0 * (double)blk_bits * sum;
	/* n_blk=128 固定 ⇒ 6·sqrt(2·128) = 6·16 = 96，门限 [32, 224]（常量展开，免 libm） */
	const double lo = 32.0;
	const double hi = 224.0;

	ZT_INFO("block_freq_8k", "blocks=%u,chi_square=%.2f,lo=%.1f,hi=%.1f",
		(unsigned int)n_blk, chi2, lo, hi);
	zassert_true(chi2 >= lo && chi2 <= hi,
		     "block chi_square %.2f out of [%.1f, %.1f]", chi2, lo, hi);
}

/* 11. 游程测试（NIST SP800-22 §2.3）：n=65536 bit。
 *    π = 1 占比；先判 |π−0.5| < 2/sqrt(n)（τ）；V = 1 + 相邻不同的次数；
 *    z = |V − 2nπ(1−π)| / (2·sqrt(2n)·π(1−π)) 近似标准正态 ⇒ 取 ±6σ 门限。 */
ZTEST(trng, runs_8k)
{
	int ret = entropy_get_entropy(trng_dev, buf_stat, sizeof(buf_stat));

	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);

	const double n = (double)(sizeof(buf_stat) * 8u);
	uint32_t ones = count_ones(buf_stat, sizeof(buf_stat));
	double pi = (double)ones / n;
	/* 常量展开免 libm：sqrt(65536)=256 ⇒ tau=2/256；sqrt(2·65536)=362.03867 */
	const double tau = 0.0078125;

	zassert_true(dabs(pi - 0.5) < tau,
		     "pi=%.5f deviates from 0.5 by more than tau=%.5f", pi, tau);

	uint32_t v = 1;
	uint32_t prev = bit_get(buf_stat, 0);

	for (size_t i = 1; i < (size_t)n; i++) {
		uint32_t cur = bit_get(buf_stat, i);

		if (cur != prev) {
			v++;
		}
		prev = cur;
	}

	double num = dabs((double)v - 2.0 * n * pi * (1.0 - pi));
	double den = 2.0 * 362.038671968 * pi * (1.0 - pi);
	double z = num / den;

	ZT_INFO("runs_8k", "runs=%u,pi=%.5f,z=%.3f", v, pi, z);
	zassert_true(z < 6.0, "runs z=%.3f >= 6 (runs=%u, pi=%.5f)", z, v, pi);
}

/* 12. 自相关（前 8 个 lag）：比特映射 s=±1 后 R(d)=(1/n)Σ s_i·s_{i+d}。
 *    i.i.d. 下 E[R(d)]=0、sd=1/sqrt(n)=1/256 ⇒ 取 ±6σ = 0.0234 门限。
 *    只拦"周期性/强相关"坏源（real TRNG 不可能越界）。 */
ZTEST(trng, autocorr_8k)
{
	int ret = entropy_get_entropy(trng_dev, buf_stat, sizeof(buf_stat));

	zassert_equal(ret, 0, "entropy_get_entropy ret %d", ret);

	const size_t n = sizeof(buf_stat) * 8u;
	/* 常量展开免 libm：sd=1/sqrt(65536)=1/256 ⇒ 6σ 门限 6/256 */
	const double gate = 0.0234375;
	double worst = 0.0;
	int worst_lag = 0;

	for (int d = 1; d <= 8; d++) {
		int32_t acc = 0;

		for (size_t i = 0; i + (size_t)d < n; i++) {
			int32_t a = bit_get(buf_stat, i) ? -1 : 1;
			int32_t b = bit_get(buf_stat, i + (size_t)d) ? -1 : 1;

			acc += a * b;
		}

		double r = (double)acc / (double)(n - (size_t)d);

		if (dabs(r) > dabs(worst)) {
			worst = r;
			worst_lag = d;
		}
	}

	ZT_INFO("autocorr_8k", "max_abs_r=%.5f,lag=%d,gate=%.5f",
		dabs(worst), worst_lag, gate);
	zassert_true(dabs(worst) < gate,
		     "autocorr |R(%d)|=%.5f >= gate %.5f", worst_lag, dabs(worst), gate);
}

/* 13. 1 MiB 连续读取（边界长度）：驱动 req_bits 计数在 2^19 bit(64KB) 处
 *    触发 trng_issue_next 的 ADVANCE_STATE + skip_next 复位分支；1 MiB 会让
 *    该隐藏分支反复走 16 次（64KB × 16 = 1 MiB），验证长序列下状态机不丢数据。
 *    1 MiB 超过 SRAM，用 8KB 滚动缓冲分 128 片，累计统计 + 逐片非退化检查。 */
ZTEST(trng, long_seq_1m)
{
	uint64_t ones = 0;
	uint64_t total_bits = 0;
	bool degenerate = false;
	int ret = 0;
	int fail_chunk = -1;
	uint32_t t0 = k_cycle_get_32();

	for (int i = 0; i < 128 && ret == 0; i++) {
		ret = entropy_get_entropy(trng_dev, buf_long, sizeof(buf_long));
		if (ret != 0) {
			fail_chunk = i;
			break;
		}
		if (buf_is_const(buf_long, sizeof(buf_long), 0x00) ||
		    buf_is_const(buf_long, sizeof(buf_long), 0xFF)) {
			degenerate = true;
			fail_chunk = i;
			break;
		}
		ones += count_ones(buf_long, sizeof(buf_long));
		total_bits += (uint64_t)sizeof(buf_long) * 8u;
	}
	uint32_t t1 = k_cycle_get_32();

	zassert_equal(ret, 0, "entropy_get_entropy ret %d at chunk %d", ret, fail_chunk);
	zassert_false(degenerate, "constant 8KB chunk at %d in 1MiB stream", fail_chunk);

	uint64_t us = k_cyc_to_us_floor64(t1 - t0);
	double percent = 100.0 * (double)ones / (double)total_bits;
	/* 二项分布 B(8e6,0.5)：sd=sqrt(n)/2≈1414 计数 ⇒ 44%~56% 远松于 ±6σ */
	double frac = (double)ones / (double)total_bits;

	ZT_INFO("long_seq_1m", "bytes=%llu,us=%llu,ones_pct=%.3f",
		(unsigned long long)(total_bits / 8u), (unsigned long long)us, percent);
	zassert_true(total_bits == 128ull * 8192ull * 8ull,
		     "1MiB stream short: bits=%llu", (unsigned long long)total_bits);
	zassert_true(frac >= 0.44 && frac <= 0.56,
		     "1MiB ones fraction %.5f out of [0.44,0.56]", frac);
}

/* 14. 并发取熵：两线程各自反复取熵。
 *
 * ⚠️ 覆盖声明降级（审计 A 组 P1-3）：本项断言只有"两线程 ret==0 + 两块数据
 * 不同"，**删掉驱动的 k_mutex 后仍然通过**（k_pipe 自带 spinlock，且两线程
 * 各自写自己的 conc_buf）⇒ 它**不能**证明 k_mutex 互斥。旧 COVERAGE 把它
 * 标为"覆盖 k_mutex 互斥"属**假覆盖声明**，已改为"并发 smoke：不崩溃/不死锁/
 * 输出非退化"，不再声称覆盖 mutex。真正可归因的互斥判据需要"同一切正在被
 * 两线程交错破坏"的观测（如同一 buf 被两线程同时读到），当前不可得。 */
static void trng_conc_worker(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int idx = (int)(intptr_t)p1;

	conc_ret[idx] = -1;
	for (int i = 0; i < 8; i++) {
		int r = entropy_get_entropy(trng_dev, conc_buf[idx],
					    sizeof(conc_buf[idx]));

		if (r != 0) {
			conc_ret[idx] = r;
			return;
		}
	}
	conc_ret[idx] = 0;
}

ZTEST(trng, concurrent_get)
{
	conc_ret[0] = -1;
	conc_ret[1] = -1;
	memset(conc_buf, 0, sizeof(conc_buf));

	k_thread_create(&conc_thread_a, conc_stack_a, K_THREAD_STACK_SIZEOF(conc_stack_a),
			trng_conc_worker, (void *)(intptr_t)0, NULL, NULL,
			K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
	k_thread_create(&conc_thread_b, conc_stack_b, K_THREAD_STACK_SIZEOF(conc_stack_b),
			trng_conc_worker, (void *)(intptr_t)1, NULL, NULL,
			K_PRIO_PREEMPT(2), 0, K_NO_WAIT);

	k_thread_join(&conc_thread_a, K_FOREVER);
	k_thread_join(&conc_thread_b, K_FOREVER);

	ZT_INFO("concurrent_get", "ret0=%d,ret1=%d", conc_ret[0], conc_ret[1]);
	zassert_equal(conc_ret[0], 0, "thread0 ret %d", conc_ret[0]);
	zassert_equal(conc_ret[1], 0, "thread1 ret %d", conc_ret[1]);
	zassert_true(memcmp(conc_buf[0], conc_buf[1], sizeof(conc_buf[0])) != 0,
		     "two threads returned identical 512B blocks");
}

/* 15. ISR 接口非阻塞分支（不置 ENTROPY_BUSYWAIT）：返回本次"池中现有"的字节数
 *    （0..len），不阻塞；同时覆盖其入参校验分支（NULL / len==0 → -EINVAL）。
 *
 * 驱动真实语义（entropy_ls_trng.c）：**按需驱动 + 池满自关中断**，非缺陷：
 *   · 池容量 128B（:37），ISR 每次写 16B；ISR 内 k_pipe_put(K_NO_WAIT) 在池内
 *     可用 < 16B 时 written==0，遂执行 `IE &= ~DWTRNG_IE_DONE_Msk`（:123-125）
 *     —— 池满即自关 DONE 中断（有意的省中断设计）。
 *   · 阻塞路径 ls_trng_get_entropy() 每轮取前 trng_irq_enable()（:147），
 *     取满 len 即退出（:144-152）⇒ 返回时池被恰好排空，且中断已在期间被 ISR 关闭。
 *   · 非阻塞路径 ls_trng_get_entropy_isr() 是**先取后使能**（:169-172）——
 *     "重新使能中断"对本次调用已太晚，故紧随阻塞读之后的首次调用必然取到 0。
 *   ⇒ 原断言"k_msleep(20) 后一次取满 64B"隐含假设"池会自由回填"（旧 COVERAGE 口径），
 *     与实现不符（实测 first got=0，见 TRIAGE_MISC_2026-09-18.md §4.2）。
 *   修法：首调只断言返回值 ∈ [0, len]（它同时会在返回前重新使能 DONE 中断）；
 *     此后池才回填，k_msleep(20) 后第二次调用才断言 == len。 */
ZTEST(trng, isr_nonblock)
{
	int ret = entropy_get_entropy_isr(trng_dev, NULL, 16, 0);

	zassert_equal(ret, -EINVAL, "isr NULL buf ret %d != -EINVAL", ret);

	ret = entropy_get_entropy_isr(trng_dev, buf_isr_nb, 0, 0);
	zassert_equal(ret, -EINVAL, "isr len=0 ret %d != -EINVAL", ret);

	/* 先用阻塞接口取 128B：把池排空，并使 ISR 因池满而自关 DONE 中断 */
	ret = entropy_get_entropy(trng_dev, buf_256, 128);
	zassert_equal(ret, 0, "prime get ret %d", ret);

	/* 首次非阻塞调用：池为空 ⇒ 返回 0；返回前重新使能 DONE 中断（:172） */
	ret = entropy_get_entropy_isr(trng_dev, buf_isr_nb, sizeof(buf_isr_nb), 0);

	ZT_INFO("isr_nonblock", "first_got=%d,len=%u", ret, (unsigned int)sizeof(buf_isr_nb));
	zassert_true(ret >= 0 && (size_t)ret <= sizeof(buf_isr_nb),
		     "isr nonblock first got %d out of [0, %u]", ret,
		     (unsigned int)sizeof(buf_isr_nb));

	k_msleep(20);   /* 中断已被上一次调用重新使能，池此时回填 */

	ret = entropy_get_entropy_isr(trng_dev, buf_isr_nb, sizeof(buf_isr_nb), 0);

	ZT_INFO("isr_nonblock", "second_got=%d,len=%u", ret, (unsigned int)sizeof(buf_isr_nb));
	zassert_equal(ret, (int)sizeof(buf_isr_nb),
		      "pool not refilled after re-arm: got %d != %u", ret,
		      (unsigned int)sizeof(buf_isr_nb));
	zassert_false(buf_is_const(buf_isr_nb, sizeof(buf_isr_nb), 0x00),
		      "isr nonblock output all 0x00");
}
