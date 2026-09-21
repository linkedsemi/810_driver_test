/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC temperature sensor test cases (ch13 on-die, LinkedSemi ADC driver)
 *
 * LinkedSemi ADC 是定制 API（非标准 Zephyr ADC）：
 *   - acquisition_time 用 ADC_SAMPLETIME_15CYCLES（来自 ls_hal_adcv2.h）
 *   - 不设 resolution/gain（硬件固定）
 *   - sequence.buffer_size = 40 * sizeof(uint16_t)
 *
 * Temperature formula: T = (2330.7 - raw) / 4.5807
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/ztest.h>
#include <ls_hal_adcv2.h>
#include <string.h>
#include <errno.h>
#include "ztest_glue.h"   /* ZT_INFO 约定行（§13.4；P1a 新增项使用） */

#define ADC_TEMPER_CHANNEL 13

static const struct device *adc_dev;
/* 静态 buffer（测试固件避免堆分配，栈/堆失败会导致 memset(NULL) 崩溃） */
static uint16_t sample_buffer[40];
static const size_t buf_size = sizeof(sample_buffer);

/* Convert raw ADC value to temperature in degrees Celsius (integer) */
static int16_t raw_to_celsius(uint16_t adc_value)
{
	return (int16_t)((2330.7f - (float)adc_value) / 4.5807f);
}

static void setup_adc_channel(void)
{
	struct adc_channel_cfg channel_config = {
		.channel_id = ADC_TEMPER_CHANNEL,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_SAMPLETIME_15CYCLES,
	};
	adc_channel_setup(adc_dev, &channel_config);
}

static int do_adc_read(void)
{
	struct adc_sequence sequence = {
		.buffer = sample_buffer,
		.buffer_size = buf_size,
		.options = NULL,
	};
	memset(sample_buffer, 0, buf_size);
	int ret = adc_read(adc_dev, &sequence);
	if (ret != 0) {
		return -1;
	}
	return (int)sample_buffer[0];
}

static void *adc_temper_setup(void)
{
	adc_dev = DEVICE_DT_GET(DT_ALIAS(testadc));
	zassert_true(device_is_ready(adc_dev), "ADC device not ready");

	setup_adc_channel();
	return NULL;
}

/* TC-1: adc_read returns 0 */
ZTEST(adc_temper, test_adc_read_returns_ok)
{
	struct adc_sequence sequence = {
		.buffer = sample_buffer,
		.buffer_size = buf_size,
		.options = NULL,
	};
	memset(sample_buffer, 0, buf_size);
	int ret = adc_read(adc_dev, &sequence);
	zassert_equal(ret, 0, "adc_read returned %d, expected 0", ret);
}

/* TC-2: raw value is not boundary (0 or 0xFFFF) */
ZTEST(adc_temper, test_adc_raw_not_boundary)
{
	int raw = do_adc_read();
	zassert_true(raw >= 0, "adc_read failed: %d", raw);
	zassert_true(raw != 0, "raw == 0 (sensor not enabled or error)");
	zassert_true(raw != 0xFFFF, "raw == 0xFFFF (sensor error)");
}

/* TC-3: temperature in [-40, 125] C; room-temperature sanity */
ZTEST(adc_temper, test_temperature_in_range)
{
	int raw = do_adc_read();
	zassert_true(raw >= 0, "adc_read failed: %d", raw);

	int16_t t_deg = raw_to_celsius((uint16_t)raw);

	zassert_true(t_deg >= -40 && t_deg <= 125,
		"temperature %d C out of range [-40, 125]", t_deg);
	zassert_true(t_deg >= 10 && t_deg <= 40,
		"temperature %d C outside room-expected range [10, 40]", t_deg);
}

/* TC-4【已删除 2026-09-18】原 `test_adc_raw_to_celsius_pure_function`：
 * 它测的是**本工程自己的** `raw_to_celsius()`，期望值由同一条公式反解，
 * **全程零驱动调用** ⇒ 驱动全坏也不会 FAIL（测试有效性审计 A 组 P0-3）。
 * 该公式是测试侧的换算辅助（供 TC-3/TC-5 的℃范围判据用），不是被测契约，
 * 无独立可信黄金值可比，故按审计裁定删除本项（不编造替代断言）。
 * 说明见 tests/COVERAGE.md 对应条目。 */

/* TC-5: sampling stability (20 reads, range < 50 counts) */
ZTEST(adc_temper, test_sampling_stability)
{
	int min_raw = 9999, max_raw = 0;

	for (int i = 0; i < 20; i++) {
		int raw = do_adc_read();
		zassert_true(raw >= 0, "adc_read failed on iter %d: %d", i, raw);
		if (raw < min_raw) min_raw = raw;
		if (raw > max_raw) max_raw = raw;
		k_msleep(10);
	}

	/* ★ 堵死"驱动静默降级"的全零路径：驱动缺陷 N2 下
	 * `ADC_RegularGetValue()` 的 ERR 分支只清标志、不写 buffer，每次读回
	 * 调用方 memset 的 0，而 `adc_read()` 仍返回 0 ⇒ 旧断言"range<50"
	 * 在 min=max=0 时恒真（假覆盖）。片内温感在室温下的有效转换必然 > 0
	 * （与 TC-2 `raw != 0`、TC-8 `raw > 0` 同一条驱动不变量）。 */
	zassert_true(min_raw > 0,
		"min raw == 0: 有效转换缺失（采样错误被驱动静默吞掉=缺陷 N2，"
		"或传感器未工作）(min=%d max=%d)", min_raw, max_raw);

	/* 阈值 50 counts 为**环境前提**（温感在恒温环境下的噪声上界），
	 * 非实测标定值；仅拦"读数剧烈跳变"这一失效形态。 */
	int range = max_raw - min_raw;
	zassert_true(range < 50,
		"stability fail: range=%d (min=%d, max=%d), expected < 50",
		range, min_raw, max_raw);
}

/* TC-6: multiple reads all succeed */
ZTEST(adc_temper, test_multiple_adc_reads)
{
	for (int i = 0; i < 5; i++) {
		struct adc_sequence sequence = {
			.buffer = sample_buffer,
			.buffer_size = buf_size,
			.options = NULL,
		};
		memset(sample_buffer, 0, buf_size);
		int ret = adc_read(adc_dev, &sequence);
		zassert_equal(ret, 0, "adc_read #%d returned %d", i, ret);
		k_msleep(10);
	}
}

/* ==================================================================
 * P1a 新增用例（2026-09-17）
 *
 * 说明：`adc_ls_channel_setup()` 每次调用都会 `data->conversion_rank++`，
 * 而 rank > nbr_of_conversion(本板 =1) 即返回 -EINVAL；只有一次成功的
 * `adc_read()`（内部 `ADC_GetValue()` 把 rank 清 0）才能"复位"rank。
 * 因此凡需要连续多次 channel_setup 的用例，都在每次 setup 前先做一次
 * 成功读取——这既是用例自建前置，也让每项可独立通过。
 * ================================================================== */

/* TC-7: adc_channel_setup 入参校验分支（驱动 :609-612）
 *   NULL / channel_id>=16 / acquisition_time>3 三路都必须 -EINVAL，
 *   且都在 rank++ 之前返回，不改动任何寄存器或 rank。 */
ZTEST(adc_temper, test_adc_channel_setup_invalid)
{
	struct adc_channel_cfg cfg = {
		.channel_id = ADC_TEMPER_CHANNEL,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_SAMPLETIME_15CYCLES,
	};

	zassert_equal(adc_channel_setup(adc_dev, NULL), -EINVAL,
		      "channel_setup(NULL) should be -EINVAL");

	cfg.channel_id = 16;   /* 驱动上限 15（:609） */
	zassert_equal(adc_channel_setup(adc_dev, &cfg), -EINVAL,
		      "channel_id=16 should be -EINVAL");

	cfg.channel_id = ADC_TEMPER_CHANNEL;
	cfg.acquisition_time = 4;   /* 驱动上限 3 = ADC_SAMPLETIME_15CYCLES */
	zassert_equal(adc_channel_setup(adc_dev, &cfg), -EINVAL,
		      "acquisition_time=4 should be -EINVAL");
}

/* TC-8: 采样时间与参考源配置变体（驱动 :609-640 / :256-293）
 *   采样时间边界（下界 1 cycle / 上界 15 cycles）与参考源分支
 *   （ADC_REF_VDD_1 / ADC_REF_EXTERNAL0，驱动 :262-271）都必须被接受，
 *   最后还原为温感默认（INTERNAL + 15 cycles）并确认温度读数仍正常。
 *
 * ⚠️ 本项暴露驱动缺陷 N2（采样错误静默降级），**不改驱动，只在本用例内处理**：
 *   实测同用例内驱动打 4 条 `<err> adc_ls: Sampling error`；而
 *   `ADC_RegularGetValue()`（adc_ls.c:89-104）在 `INTR.ERR` 分支只清标志后
 *   **直接 return、不写 data->ctx.sequence.buffer** ⇒ 调用方 buffer 保持
 *   `memset(0)` 的 0，且 `adc_read()` 仍返回 0（成功）。
 *   故 `raw == 0` **不能**解释为"温度对应 0 电压"，只能解释为"转换失败被静默吞掉"。
 *   触发源是本用例自身：`ADC_REF_VDD_1` / `ADC_REF_EXTERNAL0` / 1-cycle 采样
 *   对片内温感通道（ch13）并非有效工作点，切换期间/之后会实际产生采样错误。
 *   修法（TRIAGE_MISC_2026-09-18.md §3.4）：还原默认后做**有限次建立重试**，
 *   并把 raw 的三态语义显式区分（>0 有效 / ==0 静默降级 / <0 API 报错）。
 *   ★ 驱动缺陷 N2 修复后本项需改判：届时采样错误会上抛，raw==0 将伴随
 *     `adc_read() != 0`（本用例的循环同样能正确处理，只是重试计数含义不同）。 */
#define ADC_VARIANT_SETTLE_TRIES 50   /* 50 × 10 ms = 500 ms 建立窗口 */
#define ADC_VARIANT_SETTLE_MS    10

ZTEST(adc_temper, test_adc_setup_variants)
{
	/* 每次 setup 前先做一次成功读取以复位 conversion_rank */
#define ADC_SETUP_OK(refv, att) \
	do { \
		struct adc_channel_cfg _c = { \
			.channel_id = ADC_TEMPER_CHANNEL, \
			.reference = (refv), \
			.acquisition_time = (att), \
		}; \
		zassert_true(do_adc_read() >= 0, \
			     "pre-read for ref=%d at=%d failed", (int)(refv), (int)(att)); \
		zassert_equal(adc_channel_setup(adc_dev, &_c), 0, \
			      "channel_setup ref=%d at=%d rejected", \
			      (int)(refv), (int)(att)); \
	} while (0)

	ADC_SETUP_OK(ADC_REF_INTERNAL, ADC_SAMPLETIME_1CYCLE);
	ADC_SETUP_OK(ADC_REF_INTERNAL, ADC_SAMPLETIME_15CYCLES);
	ADC_SETUP_OK(ADC_REF_VDD_1, ADC_SAMPLETIME_15CYCLES);
	ADC_SETUP_OK(ADC_REF_EXTERNAL0, ADC_SAMPLETIME_15CYCLES);

	/* 还原温感默认配置，确认恢复正常量程读数 */
	ADC_SETUP_OK(ADC_REF_INTERNAL, ADC_SAMPLETIME_15CYCLES);
#undef ADC_SETUP_OK

	/* 还原后的首次转换可能仍在采样错误窗口内（参考源刚切回内部），
	 * 做有限次建立重试：只接受 raw > 0 这一种"有效转换"结果。 */
	int raw = 0;
	int tries = 0;

	for (; tries < ADC_VARIANT_SETTLE_TRIES; tries++) {
		raw = do_adc_read();
		if (raw > 0) {
			break;
		}
		k_msleep(ADC_VARIANT_SETTLE_MS);
	}

	int16_t t = raw_to_celsius((uint16_t)raw);

	ZT_INFO("test_adc_setup_variants", "raw=%d,tries=%d,t_deg=%d", raw, tries, (int)t);
	zassert_true(raw > 0,
		     "restored read invalid after %d tries: raw=%d "
		     "(raw==0 = 采样错误被驱动静默吞掉，驱动缺陷 N2，见本用例注释；"
		     "raw<0 = adc_read 报错)，非真实 0 电压",
		     tries, raw);
	zassert_true(t >= 10 && t <= 40,
		     "restored temperature %d C outside [10, 40]", t);
}

/* TC-9: 连续 setup 的 rank 守卫（驱动 :644-681 的 else 分支
 *   "Regular Channel Mode Illegal rank"）。nbr_of_conversion=1，
 *   第二次连续 setup（中间无 read）rank=2 > 1 → -EINVAL。 */
ZTEST(adc_temper, test_adc_rank_guard)
{
	struct adc_channel_cfg c = {
		.channel_id = ADC_TEMPER_CHANNEL,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_SAMPLETIME_15CYCLES,
	};

	zassert_true(do_adc_read() >= 0, "pre-read failed");   /* rank → 0 */

	zassert_equal(adc_channel_setup(adc_dev, &c), 0,
		      "first setup should succeed");

	zassert_equal(adc_channel_setup(adc_dev, &c), -EINVAL,
		      "second consecutive setup must hit the rank guard (-EINVAL)");

	/* 复位 rank 并还原通道配置，避免影响同套件其他用例 */
	zassert_true(do_adc_read() >= 0, "post-read failed");
	zassert_equal(adc_channel_setup(adc_dev, &c), 0, "restore setup rejected");
	zassert_true(do_adc_read() >= 0, "final read failed");
}

/* TC-10: 量程边界（12 位满量程）。驱动 init 把 MISC_CTRL 的 ADC_RES 置位
 *   （reg_adcv2 `ADC_ADC_RES_MASK=0x30`，:478-482），输出上界 4095。
 *   温感在室温下应落在 (0, 4095] 内且温度 ∈ [-40,125]。 */
ZTEST(adc_temper, test_adc_full_scale_bound)
{
	int raw = do_adc_read();

	zassert_true(raw >= 0, "adc_read failed: %d", raw);
	zassert_true(raw > 0 && raw <= 4095,
		     "raw %d outside 12-bit full scale (0, 4095]", raw);

	int16_t t = raw_to_celsius((uint16_t)raw);

	ZT_INFO("test_adc_full_scale_bound", "raw=%d,t_deg=%d,fs_max=4095",
		raw, (int)t);
	zassert_true(t >= -40 && t <= 125,
		     "temperature %d C out of range [-40, 125]", t);
}

ZTEST_SUITE(adc_temper, NULL, adc_temper_setup, NULL, NULL, NULL);
