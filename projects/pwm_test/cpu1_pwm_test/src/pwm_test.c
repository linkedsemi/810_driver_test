/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PWM 接口可用性测试（LinkedSemi PWM 驱动 zephyr/drivers/pwm/pwm_ls.c）
 *
 * 范围：只验 API 层契约 —— 配置 / 使能 / 极性 / 频率查询 / 周期与脉宽回读。
 * **波形正确性不在本工程判据内**（本板无示波器/逻辑分析仪），
 * 详见 tests/COVERAGE.md 顶部备注。
 *
 * 回读方式：Zephyr PWM API 没有"读回周期/脉宽"接口，本工程回读直接读被写寄存器
 *   PWM_PARAM[ch].PWM_CYC / PWM_HIGH（普通读写寄存器，HAL_PWM_SetConfig 亦直接写入）
 *   PWM_CTRL[ch] 的 POL 位（bit3，普通读写寄存器）
 * 寄存器偏移来源：modules/hal/linkedsemi/hal_driver/inc/reg_pwm_type.h 的 reg_pwm_t
 *   （PWM_EN@0x00 / PWM_PRE_DIV@0x04 / PWM_CTRL@0x20 /
 *    PWM_PARAM@0x60，每通道 16 字节；PWM_POL 位于 PWM_CTRL bit3）。
 * 固件不 include HAL 私有头，改用 sys_read32 按偏移读，避免依赖 HAL 内部布局头。
 *
 * PWM_EN 使能位**不做回读断言**：该寄存器为 W1S/W1C 语义（HAL 关闭通道写
 * PWM_EN |= BIT(ch + 16)，见 ls_hal_pwm.c 的 HAL_PWM_Stop），低半字回读值语义
 * 无文档，无法建立确定判据 —— "使能"契约以 pwm_set_cycles 的返回码判，
 * PWM_EN 原始值仅经 ZT_INFO 上报供参考。
 *
 * 驱动分支可达性说明（逐分支扫描结论，对照 tests/COVERAGE.md）：
 *   - 驱动 ls_pwm_set_cycles 的 `pulse_cycles > period_cycles` 分支在经
 *     pwm_set_cycles() 调用时**不可达**（pwm.h 包装层先返回 -EINVAL，L490）；
 *     可达的是驱动自身的 `period == 0 / pulse == 0` 与 `> 0xFFFF` 两个拒绝分支。
 *   - 驱动无 disable/stop 入口，`pwm_driver_api` 只有 set_cycles + get_cycles_per_sec。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/ztest.h>
#include "ztest_glue.h"

#define PWM_NODE DT_NODELABEL(pwm)

/* 驱动通道数上界 = reg_pwm_type.h 的 CHAN_CONT（CONFIG_PWM_LS_V2=y）：
 * 合法通道 0..15，16 起判非法。见 prj.conf / COVERAGE.md 的配置核对记录。 */
#define PWM_CHAN_MAX 16u

/* 寄存器偏移（reg_pwm_type.h 的 reg_pwm_t 布局） */
#define PWM_REG_EN               0x00u
#define PWM_REG_PRE_DIV          0x04u
#define PWM_REG_CTRL(ch)         (0x20u + 4u * (ch))
#define PWM_REG_PARAM_CYC(ch)    (0x60u + 16u * (ch))
#define PWM_REG_PARAM_HIGH(ch)   (0x60u + 16u * (ch) + 4u)
#define PWM_CTRL_POL_BIT         0x8u

/* 期望频率来自 DT 属性（非实机测量）：驱动 get_cycles_per_sec 就是
 * clock_frequency / (PWM_PRE_DIV + 1)，PWM_PRE_DIV 由 init 写入 prescaler。 */
#define PWM_CLK_FREQ  DT_PROP(PWM_NODE, clock_frequency)
#define PWM_PRESCALER DT_PROP(PWM_NODE, prescaler)
#define PWM_CPS       ((uint64_t)PWM_CLK_FREQ / ((uint32_t)PWM_PRESCALER + 1u))

#define PWM_CONC_ITERS 100

static const struct device *pwm_dev;

static void *pwm_suite_setup(void)
{
	pwm_dev = DEVICE_DT_GET(PWM_NODE);
	zassert_true(device_is_ready(pwm_dev),
		     "pwm device not ready (init 通路失败：clock/reset/pinctrl 任一未就绪)");
	return NULL;
}

static uint32_t pwm_reg_read(uint32_t off)
{
	return sys_read32(DT_REG_ADDR(PWM_NODE) + off);
}

/* ns → cycles，与被测驱动 pwm_set() 内的换算同式（pwm.h L552-558） */
static uint32_t nsec_to_cycles(uint32_t nsec)
{
	return (uint32_t)(((uint64_t)nsec * PWM_CPS) / NSEC_PER_SEC);
}

/* ---- TC-1 功能通路 / 复位-时钟通路：init 生效 + 频率查询自洽 ---- */
ZTEST(pwm, init_path_readback)
{
	uint32_t pre_div = pwm_reg_read(PWM_REG_PRE_DIV);
	uint64_t cps = 0;
	int ret;

	/* PWM_PRE_DIV 只有 ls_pwm_init 会写 ⇒ 回读等于 DT prescaler 即证明
	 * init 的 clock_control_on / reset_line_toggle / pinctrl_apply_state
	 * 与寄存器初始化段全部走到。 */
	zassert_equal(pre_div, (uint32_t)PWM_PRESCALER,
		      "PWM_PRE_DIV not written by init: reg=%u, DT prescaler=%u",
		      pre_div, (uint32_t)PWM_PRESCALER);

	ret = pwm_get_cycles_per_sec(pwm_dev, 0, &cps);
	zassert_equal(ret, 0, "pwm_get_cycles_per_sec failed: %d", ret);
	zassert_equal(cps, PWM_CPS,
		      "cycles_per_sec %llu != clock_frequency/(pre_div+1) %llu",
		      (unsigned long long)cps, (unsigned long long)PWM_CPS);

	ZT_INFO("init_path_readback", "pre_div=%u,cycles_per_sec=%llu,dev_ready=1",
		pre_div, (unsigned long long)cps);
}

/* ---- TC-2 功能通路：set_cycles 往返 + 周期/脉宽/极性回读 ---- */
ZTEST(pwm, set_cycles_readback)
{
	const uint32_t period = 1000u;
	const uint32_t pulse = 400u;
	int ret = pwm_set_cycles(pwm_dev, 0, period, pulse, PWM_POLARITY_NORMAL);

	zassert_equal(ret, 0, "pwm_set_cycles failed: %d", ret);
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_CYC(0)), period,
		      "PWM_CYC readback %u != %u", pwm_reg_read(PWM_REG_PARAM_CYC(0)), period);
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_HIGH(0)), pulse,
		      "PWM_HIGH readback %u != %u", pwm_reg_read(PWM_REG_PARAM_HIGH(0)), pulse);
	zassert_equal(pwm_reg_read(PWM_REG_CTRL(0)) & PWM_CTRL_POL_BIT, 0u,
		      "POL bit set by PWM_POLARITY_NORMAL");

	ZT_INFO("set_cycles_readback", "ch=0,period=%u,pulse=%u,cyc=%u,high=%u",
		period, pulse, pwm_reg_read(PWM_REG_PARAM_CYC(0)),
		pwm_reg_read(PWM_REG_PARAM_HIGH(0)));
}

/* ---- TC-3 功能通路：极性标志位设置/清除，且不得影响周期与脉宽 ---- */
ZTEST(pwm, polarity_flags)
{
	int ret = pwm_set_cycles(pwm_dev, 1, 800u, 200u, PWM_POLARITY_INVERTED);

	zassert_equal(ret, 0, "set_cycles(INVERTED) failed: %d", ret);
	zassert_not_equal(pwm_reg_read(PWM_REG_CTRL(1)) & PWM_CTRL_POL_BIT, 0u,
			  "POL bit not set for PWM_POLARITY_INVERTED: ctrl=0x%x",
			  pwm_reg_read(PWM_REG_CTRL(1)));
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_CYC(1)), 800u,
		      "polarity must not change PWM_CYC: %u", pwm_reg_read(PWM_REG_PARAM_CYC(1)));
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_HIGH(1)), 200u,
		      "polarity must not change PWM_HIGH: %u", pwm_reg_read(PWM_REG_PARAM_HIGH(1)));

	ret = pwm_set_cycles(pwm_dev, 1, 800u, 200u, PWM_POLARITY_NORMAL);
	zassert_equal(ret, 0, "set_cycles(NORMAL) failed: %d", ret);
	zassert_equal(pwm_reg_read(PWM_REG_CTRL(1)) & PWM_CTRL_POL_BIT, 0u,
		      "POL bit not cleared for PWM_POLARITY_NORMAL: ctrl=0x%x",
		      pwm_reg_read(PWM_REG_CTRL(1)));

	ZT_INFO("polarity_flags", "ch=1,inverted_then_normal=1,ctrl=0x%x",
		pwm_reg_read(PWM_REG_CTRL(1)));
}

/* ---- TC-4 功能通路：频率查询契约在通道范围内一致 ---- */
ZTEST(pwm, cycles_per_sec_contract)
{
	static const uint32_t chans[] = {0u, 7u, PWM_CHAN_MAX - 1u};

	for (size_t i = 0; i < ARRAY_SIZE(chans); i++) {
		uint64_t cps = 0;
		int ret = pwm_get_cycles_per_sec(pwm_dev, chans[i], &cps);

		zassert_equal(ret, 0, "ch%u: get_cycles_per_sec failed: %d", chans[i], ret);
		zassert_equal(cps, PWM_CPS,
			      "ch%u: cycles_per_sec %llu != %llu", chans[i],
			      (unsigned long long)cps, (unsigned long long)PWM_CPS);
	}

	ZT_INFO("cycles_per_sec_contract", "chans_tested=%u,cycles_per_sec=%llu",
		(uint32_t)ARRAY_SIZE(chans), (unsigned long long)PWM_CPS);
}

/* ---- TC-5 功能通路：pwm_set() 的 ns→cycles 换算路径 ---- */
ZTEST(pwm, ns_api_path)
{
	const uint32_t period_ns = PWM_USEC(1000);
	const uint32_t pulse_ns = PWM_USEC(250);
	const uint32_t exp_cyc = nsec_to_cycles(period_ns);
	const uint32_t exp_high = nsec_to_cycles(pulse_ns);
	int ret = pwm_set(pwm_dev, 2, period_ns, pulse_ns, PWM_POLARITY_NORMAL);

	zassert_equal(ret, 0, "pwm_set failed: %d", ret);
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_CYC(2)), exp_cyc,
		      "PWM_CYC %u != %u (%u ns @ %llu Hz)", pwm_reg_read(PWM_REG_PARAM_CYC(2)),
		      exp_cyc, period_ns, (unsigned long long)PWM_CPS);
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_HIGH(2)), exp_high,
		      "PWM_HIGH %u != %u (%u ns @ %llu Hz)", pwm_reg_read(PWM_REG_PARAM_HIGH(2)),
		      exp_high, pulse_ns, (unsigned long long)PWM_CPS);

	ZT_INFO("ns_api_path", "ch=2,period_ns=%u,pulse_ns=%u,cyc=%u,high=%u",
		period_ns, pulse_ns, pwm_reg_read(PWM_REG_PARAM_CYC(2)),
		pwm_reg_read(PWM_REG_PARAM_HIGH(2)));
}

/* ---- TC-6 功能通路：pwm_dt_spec 路径（pwm_set_dt / pwm_set_pulse_dt） ---- */
ZTEST(pwm, dt_spec_path)
{
	struct pwm_dt_spec spec = {
		.dev = pwm_dev,
		.channel = 3u,
		.period = PWM_USEC(1000),
		.flags = PWM_POLARITY_NORMAL,
	};
	int ret = pwm_set_dt(&spec, PWM_USEC(2000), PWM_USEC(500));

	zassert_equal(ret, 0, "pwm_set_dt failed: %d", ret);
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_CYC(3)), nsec_to_cycles(PWM_USEC(2000)),
		      "pwm_set_dt: PWM_CYC %u != %u", pwm_reg_read(PWM_REG_PARAM_CYC(3)),
		      nsec_to_cycles(PWM_USEC(2000)));
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_HIGH(3)), nsec_to_cycles(PWM_USEC(500)),
		      "pwm_set_dt: PWM_HIGH %u != %u", pwm_reg_read(PWM_REG_PARAM_HIGH(3)),
		      nsec_to_cycles(PWM_USEC(500)));

	/* pwm_set_pulse_dt 用 spec->period，只改脉宽 */
	ret = pwm_set_pulse_dt(&spec, PWM_USEC(125));
	zassert_equal(ret, 0, "pwm_set_pulse_dt failed: %d", ret);
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_CYC(3)), nsec_to_cycles(spec.period),
		      "pwm_set_pulse_dt changed PWM_CYC: %u != %u",
		      pwm_reg_read(PWM_REG_PARAM_CYC(3)), nsec_to_cycles(spec.period));
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_HIGH(3)), nsec_to_cycles(PWM_USEC(125)),
		      "pwm_set_pulse_dt: PWM_HIGH %u != %u",
		      pwm_reg_read(PWM_REG_PARAM_HIGH(3)), nsec_to_cycles(PWM_USEC(125)));

	ZT_INFO("dt_spec_path", "ch=3,spec_period_ns=%u,cyc=%u,high=%u",
		spec.period, pwm_reg_read(PWM_REG_PARAM_CYC(3)),
		pwm_reg_read(PWM_REG_PARAM_HIGH(3)));
}

/* ---- TC-7 异常参数：通道号上下界（驱动 `channel >= CHAN_CONT` 分支） ---- */
ZTEST(pwm, invalid_channel)
{
	static const uint32_t bad_chans[] = {PWM_CHAN_MAX, 99u, UINT32_MAX};
	int ret;

	for (size_t i = 0; i < ARRAY_SIZE(bad_chans); i++) {
		uint64_t cps = 0;

		ret = pwm_set_cycles(pwm_dev, bad_chans[i], 1000u, 500u, PWM_POLARITY_NORMAL);
		zassert_equal(ret, -EINVAL, "set_cycles(ch%u): expected -EINVAL, got %d",
			      bad_chans[i], ret);
		ret = pwm_get_cycles_per_sec(pwm_dev, bad_chans[i], &cps);
		zassert_equal(ret, -EINVAL, "get_cycles_per_sec(ch%u): expected -EINVAL, got %d",
			      bad_chans[i], ret);
	}

	/* ±1 边界的合法侧：最大合法通道必须被接受 */
	ret = pwm_set_cycles(pwm_dev, PWM_CHAN_MAX - 1u, 1000u, 500u, PWM_POLARITY_NORMAL);
	zassert_equal(ret, 0, "set_cycles(ch%u) should be accepted, got %d",
		      PWM_CHAN_MAX - 1u, ret);

	ZT_INFO("invalid_channel", "chan_max=%u,rejected=%u", PWM_CHAN_MAX,
		(uint32_t)ARRAY_SIZE(bad_chans));
}

/* ---- TC-8 异常参数：周期/脉宽非法组合，且拒绝不得改动已配置寄存器 ---- */
ZTEST(pwm, invalid_period_pulse)
{
	uint32_t cyc0, high0;
	int ret = pwm_set_cycles(pwm_dev, 4, 1000u, 400u, PWM_POLARITY_NORMAL);

	zassert_equal(ret, 0, "setup set_cycles failed: %d", ret);
	cyc0 = pwm_reg_read(PWM_REG_PARAM_CYC(4));
	high0 = pwm_reg_read(PWM_REG_PARAM_HIGH(4));

	/* 包装层分支：pulse > period（pwm.h L490，先于驱动） */
	ret = pwm_set_cycles(pwm_dev, 4, 100u, 101u, PWM_POLARITY_NORMAL);
	zassert_equal(ret, -EINVAL, "pulse>period: expected -EINVAL, got %d", ret);

	/* 驱动分支：period == 0（同时 pulse > period 由包装层拦，两者都返回 -EINVAL） */
	ret = pwm_set_cycles(pwm_dev, 4, 0u, 0u, PWM_POLARITY_NORMAL);
	zassert_equal(ret, -EINVAL, "period==0: expected -EINVAL, got %d", ret);

	/* 驱动分支：pulse == 0（驱动用 0 脉宽表达"非法"，不是"关闭"） */
	ret = pwm_set_cycles(pwm_dev, 4, 1000u, 0u, PWM_POLARITY_NORMAL);
	zassert_equal(ret, -EINVAL, "pulse==0: expected -EINVAL, got %d", ret);

	zassert_equal(pwm_reg_read(PWM_REG_PARAM_CYC(4)), cyc0,
		      "rejected call modified PWM_CYC: %u != %u",
		      pwm_reg_read(PWM_REG_PARAM_CYC(4)), cyc0);
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_HIGH(4)), high0,
		      "rejected call modified PWM_HIGH: %u != %u",
		      pwm_reg_read(PWM_REG_PARAM_HIGH(4)), high0);

	ZT_INFO("invalid_period_pulse", "cyc_after=%u,high_after=%u",
		pwm_reg_read(PWM_REG_PARAM_CYC(4)), pwm_reg_read(PWM_REG_PARAM_HIGH(4)));
}

/* ---- TC-9 边界长度：周期/脉宽的 16 位量程上下限（驱动 0xFFFF 掩码） ---- */
ZTEST(pwm, cycle_boundary)
{
	int ret;

	/* 下限：最小合法配置 period == pulse == 1 */
	ret = pwm_set_cycles(pwm_dev, 5, 1u, 1u, PWM_POLARITY_NORMAL);
	zassert_equal(ret, 0, "min config (1/1) failed: %d", ret);
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_CYC(5)), 1u,
		      "min: PWM_CYC %u != 1", pwm_reg_read(PWM_REG_PARAM_CYC(5)));
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_HIGH(5)), 1u,
		      "min: PWM_HIGH %u != 1", pwm_reg_read(PWM_REG_PARAM_HIGH(5)));

	/* 上限：0xFFFF（驱动 `> 0xFFFFu` 的合法侧） */
	ret = pwm_set_cycles(pwm_dev, 5, 0xFFFFu, 0xFFFFu, PWM_POLARITY_NORMAL);
	zassert_equal(ret, 0, "max config (0xFFFF) failed: %d", ret);
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_CYC(5)), 0xFFFFu,
		      "max: PWM_CYC 0x%x != 0xFFFF", pwm_reg_read(PWM_REG_PARAM_CYC(5)));
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_HIGH(5)), 0xFFFFu,
		      "max: PWM_HIGH 0x%x != 0xFFFF", pwm_reg_read(PWM_REG_PARAM_HIGH(5)));

	/* 上限 +1：触驱动自身 `period_cycles > 0xFFFFu` 拒绝分支，寄存器保持原值 */
	ret = pwm_set_cycles(pwm_dev, 5, 0x10000u, 0x10000u, PWM_POLARITY_NORMAL);
	zassert_equal(ret, -EINVAL, "period 0x10000: expected -EINVAL, got %d", ret);
	zassert_equal(pwm_reg_read(PWM_REG_PARAM_CYC(5)), 0xFFFFu,
		      "rejected call modified PWM_CYC: 0x%x", pwm_reg_read(PWM_REG_PARAM_CYC(5)));

	/* pulse 越界（period 合法）：包装层 pulse>period 分支 */
	ret = pwm_set_cycles(pwm_dev, 5, 0xFFFFu, 0x10000u, PWM_POLARITY_NORMAL);
	zassert_equal(ret, -EINVAL, "pulse 0x10000: expected -EINVAL, got %d", ret);

	ZT_INFO("cycle_boundary", "min=1,max=0x%x,over=0x%x", 0xFFFFu, 0x10000u);
}

/* ---- TC-10 功能通路 / 通道隔离：多通道各自保持自己的配置 ---- */
ZTEST(pwm, channel_isolation)
{
	static const struct {
		uint32_t ch;
		uint32_t period;
		uint32_t pulse;
	} cfg[] = {
		{2u, 500u, 100u},
		{3u, 900u, 800u},
		{4u, 1000u, 400u},
	};

	for (size_t i = 0; i < ARRAY_SIZE(cfg); i++) {
		int ret = pwm_set_cycles(pwm_dev, cfg[i].ch, cfg[i].period, cfg[i].pulse,
					 PWM_POLARITY_NORMAL);

		zassert_equal(ret, 0, "set ch%u failed: %d", cfg[i].ch, ret);
	}

	for (size_t i = 0; i < ARRAY_SIZE(cfg); i++) {
		zassert_equal(pwm_reg_read(PWM_REG_PARAM_CYC(cfg[i].ch)), cfg[i].period,
			      "ch%u PWM_CYC %u != %u (channel registers crosstalk)", cfg[i].ch,
			      pwm_reg_read(PWM_REG_PARAM_CYC(cfg[i].ch)), cfg[i].period);
		zassert_equal(pwm_reg_read(PWM_REG_PARAM_HIGH(cfg[i].ch)), cfg[i].pulse,
			      "ch%u PWM_HIGH %u != %u (channel registers crosstalk)", cfg[i].ch,
			      pwm_reg_read(PWM_REG_PARAM_HIGH(cfg[i].ch)), cfg[i].pulse);
	}

	ZT_INFO("channel_isolation", "chans=3,first_cyc=%u,last_cyc=%u",
		pwm_reg_read(PWM_REG_PARAM_CYC(cfg[0].ch)),
		pwm_reg_read(PWM_REG_PARAM_CYC(cfg[ARRAY_SIZE(cfg) - 1].ch)));
}

/* ---- TC-11 并发：spinlock 保护下"周期+脉宽+极性"三元组不得被撕开 ---- */
struct pwm_conc_arg {
	uint32_t period;
	uint32_t pulse;
};

K_THREAD_STACK_DEFINE(pwm_conc_stack0, 1024);
K_THREAD_STACK_DEFINE(pwm_conc_stack1, 1024);
static struct k_thread pwm_conc_thread0;
static struct k_thread pwm_conc_thread1;

static void pwm_conc_worker(void *p1, void *p2, void *p3)
{
	const struct pwm_conc_arg *arg = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (int i = 0; i < PWM_CONC_ITERS; i++) {
		(void)pwm_set_cycles(pwm_dev, 6, arg->period, arg->pulse,
				     PWM_POLARITY_NORMAL);
	}
}

ZTEST(pwm, concurrent_same_channel)
{
	static struct pwm_conc_arg arg0 = {1000u, 300u};
	static struct pwm_conc_arg arg1 = {2000u, 700u};
	uint32_t cyc, high;
	bool is_arg0, is_arg1;

	k_thread_create(&pwm_conc_thread0, pwm_conc_stack0,
			K_THREAD_STACK_SIZEOF(pwm_conc_stack0),
			pwm_conc_worker, &arg0, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	k_thread_create(&pwm_conc_thread1, pwm_conc_stack1,
			K_THREAD_STACK_SIZEOF(pwm_conc_stack1),
			pwm_conc_worker, &arg1, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

	k_thread_join(&pwm_conc_thread0, K_FOREVER);
	k_thread_join(&pwm_conc_thread1, K_FOREVER);

	/* 两线程只写同一通道的同一组寄存器：最终状态必须是两个完整三元组之一，
	 * 不得出现"周期来自 A、脉宽来自 B"的撕裂组合。 */
	cyc = pwm_reg_read(PWM_REG_PARAM_CYC(6));
	high = pwm_reg_read(PWM_REG_PARAM_HIGH(6));
	is_arg0 = (cyc == arg0.period && high == arg0.pulse);
	is_arg1 = (cyc == arg1.period && high == arg1.pulse);

	zassert_true(is_arg0 || is_arg1,
		     "concurrent writes torn: cyc=%u high=%u, expected (%u,%u) or (%u,%u)",
		     cyc, high, arg0.period, arg0.pulse, arg1.period, arg1.pulse);

	ZT_INFO("concurrent_same_channel", "iters=%u,cyc=%u,high=%u,winner=%s",
		(uint32_t)PWM_CONC_ITERS, cyc, high, is_arg0 ? "arg0" : "arg1");
}

/* ---- TC-12 功能通路 / 契约边界：API 层不存在"关闭通道"通路 ---- */
ZTEST(pwm, no_disable_api)
{
	uint32_t cyc0;
	int ret = pwm_set_cycles(pwm_dev, 7, 1000u, 400u, PWM_POLARITY_NORMAL);

	zassert_equal(ret, 0, "setup set_cycles failed: %d", ret);
	cyc0 = pwm_reg_read(PWM_REG_PARAM_CYC(7));

	/* 驱动 pwm_driver_api 只有 set_cycles + get_cycles_per_sec，无 disable/stop；
	 * 且 set_cycles 拒绝 period/pulse = 0 ⇒ 零周期不能被当作关闭手段，
	 * 已配置寄存器也不得被这类调用破坏。 */
	ret = pwm_set_cycles(pwm_dev, 7, 0u, 400u, PWM_POLARITY_NORMAL);
	zassert_equal(ret, -EINVAL, "period==0 (as disable): expected -EINVAL, got %d", ret);

	ret = pwm_set_cycles(pwm_dev, 7, 1000u, 0u, PWM_POLARITY_NORMAL);
	zassert_equal(ret, -EINVAL, "pulse==0 (as disable): expected -EINVAL, got %d", ret);

	zassert_equal(pwm_reg_read(PWM_REG_PARAM_CYC(7)), cyc0,
		      "rejected disable attempt modified PWM_CYC: %u != %u",
		      pwm_reg_read(PWM_REG_PARAM_CYC(7)), cyc0);

	/* PWM_EN 原始值仅上报：该寄存器 W1S/W1C 语义，回读值不构成判据 */
	ZT_INFO("no_disable_api", "disable_path=none,cyc=%u,pwm_en_raw=0x%x",
		pwm_reg_read(PWM_REG_PARAM_CYC(7)), pwm_reg_read(PWM_REG_EN));
}

ZTEST_SUITE(pwm, NULL, pwm_suite_setup, NULL, NULL, NULL);
