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

/* TC-4: pure function unit test (raw_to_celsius) */
ZTEST(adc_temper, test_adc_raw_to_celsius_pure_function)
{
	int16_t t0 = raw_to_celsius(2331);
	zassert_true(t0 >= -3 && t0 <= 3,
		"raw=2331 -> %d C, expected ~0", t0);

	int16_t t25 = raw_to_celsius(2216);
	zassert_true(t25 >= 22 && t25 <= 28,
		"raw=2216 -> %d C, expected ~25", t25);

	int16_t t_neg = raw_to_celsius(2514);
	zassert_true(t_neg >= -43 && t_neg <= -37,
		"raw=2514 -> %d C, expected ~-40", t_neg);
}

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

ZTEST_SUITE(adc_temper, NULL, adc_temper_setup, NULL, NULL, NULL);
