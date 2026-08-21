/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RTC (rtc_ls) 单核测试。
 *
 * 被测驱动：zephyr/drivers/rtc/rtc_ls.c（linkedsemi_ls_rtc，rtc@4006fc00）
 *   - 硬件 RTC 寄存器（reg_rtcv2_type.h），cyc-1hz=32768（属性），CALIB 补偿
 *   - set_time：先 rtc_en=0 → 写 TIME/CAL → 翻转 SET_TGGL → rtc_en=1；
 *     is_valid_time() 校验非法时间返回 -EINVAL（秒/分/时/日/月/年/周逐项检查）
 *   - get_time：CURTIME/CURCAL 稳定读（CURCAL 两次一致才取），硬件 1-60 编码
 *     回读 >0 时 -1（驱动已处理）
 *   - alarm：完整匹配年月日时分秒；ISR（rtc_irq_handler）触发时清 INTR、
 *     关 ALARM_EN、调 alarm_cb（ISR 上下文！回调内禁止打日志，§4.2）
 *   - alarm_is_pending 读 CTRL.ALARM_EN：1=pending 未触发 / 0=已触发
 *   - calibration：ppb 单位；正值路径 calib_cyc=ppb*60/1e9；负值路径走
 *     floor_custom_divisor（cyc_1hz 减 1 + calib_cyc 补 60）；get 反推回读
 *     （100000000 ppb = calib_cyc 6 精确往返；负值反推不等价，只断言 ret==0）
 *   - get_calibration(NULL) → -EINVAL
 *
 * 架构：单核工程（无对侧），token 角色 TGT（见 rtc_test_protocol.h）。
 * 主动自驱动，test_main() 顺序跑完所有 ZTEST，结尾 SUMMARY + DONE。
 * 结果记账：common/test_results.{h,c} 公共模块（§4.5）。
 * 数值型测试按 §13.4 打 INFO token（PASS/FAIL 之前），FAIL reason 可归因（§13.5）。
 */

#include "rtc_test.h"
#include "rtc_test_protocol.h"
#include "test_results.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(rtc_test, LOG_LEVEL_INF);

extern const struct device *rtc_dev;

static enum rtc_test_id current_test_id;
static bool rtc_any_failed;

static char rtc_fail_reason[256];
static bool rtc_failed;

/* 累积式断言：不中断当前测试，收尾统一判定（同样板 TRNG_EXPECT）。 */
#define RTC_TEST_EXPECT(cond, fmt, ...) \
	do { \
		if (!(cond)) { \
			int _rem = (int)sizeof(rtc_fail_reason) - \
				   (int)strlen(rtc_fail_reason); \
			if (_rem > 0) { \
				snprintf(rtc_fail_reason + strlen(rtc_fail_reason), \
					 (size_t)_rem, fmt "; ", ##__VA_ARGS__); \
			} \
			rtc_failed = true; \
		} \
	} while (0)

/* alarm 回调在 ISR 上下文（rtc_irq_handler → alarm_cb），只置标志（§4.2 禁 LOG） */
static volatile bool alarm_triggered;

static void alarm_cb(const struct device *dev, uint16_t id, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(id);
	ARG_UNUSED(user_data);

	alarm_triggered = true;
}

static const char *rtc_test_name(void)
{
	if (current_test_id < RTC_TEST_COUNT) {
		return rtc_test_names[current_test_id];
	}
	return "unknown";
}

/* 记账 + token + 断言收尾（样板 tgt_finish 模式，zassert 失败前先记账）。 */
static void rtc_finish(void)
{
	tr_record(current_test_id,
		  rtc_failed ? TR_FAIL : TR_PASS,
		  "%s", rtc_fail_reason);

	if (rtc_failed) {
		RTC_TEST_TGT_FAIL_VAR(rtc_test_name(), rtc_fail_reason);
		rtc_any_failed = true;
	} else {
		RTC_TEST_TGT_PASS_VAR(rtc_test_name());
	}
	zassert_false(rtc_failed, "%s", rtc_fail_reason);
}

static void rtc_begin(enum rtc_test_id id)
{
	current_test_id = id;
	rtc_failed = false;
	rtc_fail_reason[0] = '\0';

	RTC_TEST_TGT_START_VAR(rtc_test_names[id]);
}

/* 时间 → 当天秒数（跨小时进位安全，用于 delta 判定） */
static int tm_total_sec(const struct rtc_time *tm)
{
	return tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
}

/* 闹钟全字段匹配掩码（驱动只支持完整匹配，alarm_get_supported_fields） */
static uint16_t rtc_alarm_full_mask(void)
{
	return RTC_ALARM_TIME_MASK_SECOND |
	       RTC_ALARM_TIME_MASK_MINUTE |
	       RTC_ALARM_TIME_MASK_HOUR |
	       RTC_ALARM_TIME_MASK_MONTHDAY |
	       RTC_ALARM_TIME_MASK_MONTH |
	       RTC_ALARM_TIME_MASK_YEAR;
}

/* ========== ZTEST ========== */

ZTEST_SUITE(rtc, NULL, NULL, NULL, NULL, NULL);

/* 1. set → get 往返：设 2026-08-21(五) 12:34:56 后回读逐字段比对。
 *    时分秒在走，用 delta ∈ [0,5] 容差；年月日周 精确相等。 */
ZTEST(rtc, set_get_roundtrip)
{
	rtc_begin(RTC_TEST_SET_GET_ROUNDTRIP);

	struct rtc_time s = {
		.tm_sec = 56, .tm_min = 34, .tm_hour = 12,
		.tm_mday = 21, .tm_mon = 7, .tm_year = 126, .tm_wday = 5,
	};
	int ret = rtc_set_time(rtc_dev, &s);
	RTC_TEST_EXPECT(ret == 0, "rtc_set_time ret %d", ret);

	if (ret == 0) {
		struct rtc_time g = { 0 };

		ret = rtc_get_time(rtc_dev, &g);
		RTC_TEST_EXPECT(ret == 0, "rtc_get_time ret %d", ret);
		if (ret == 0) {
			RTC_TEST_EXPECT(g.tm_mday == s.tm_mday,
					"mday %d != %d", g.tm_mday, s.tm_mday);
			RTC_TEST_EXPECT(g.tm_mon == s.tm_mon,
					"mon %d != %d", g.tm_mon, s.tm_mon);
			RTC_TEST_EXPECT(g.tm_year == s.tm_year,
					"year %d != %d", g.tm_year, s.tm_year);
			RTC_TEST_EXPECT(g.tm_wday == s.tm_wday,
					"wday %d != %d", g.tm_wday, s.tm_wday);
			int delta = tm_total_sec(&g) - tm_total_sec(&s);

			/* §13.4：往返偏差 INFO 上报 */
			RTC_TEST_TGT_INFO_VAR(rtc_test_name(),
					       "delta_s=%d", delta);
			RTC_TEST_EXPECT(delta >= 0 && delta <= 5,
					"set->get delta %ds out of [0,5]", delta);
		}
	}

	rtc_finish();
}

/* 2. 时间前进：sleep 2.1s 前后两次 get，秒差 ∈ [1,4]（RTC 在计数）。 */
ZTEST(rtc, time_advances)
{
	rtc_begin(RTC_TEST_TIME_ADVANCES);

	struct rtc_time t1 = { 0 }, t2 = { 0 };
	int ret = rtc_get_time(rtc_dev, &t1);
	RTC_TEST_EXPECT(ret == 0, "get#1 ret %d", ret);

	k_sleep(K_MSEC(2100));

	ret = rtc_get_time(rtc_dev, &t2);
	RTC_TEST_EXPECT(ret == 0, "get#2 ret %d", ret);
	if (ret == 0) {
		int delta = tm_total_sec(&t2) - tm_total_sec(&t1);

		/* §13.4：前进秒数 INFO 上报 */
		RTC_TEST_TGT_INFO_VAR(rtc_test_name(),
				       "delta_s=%d", delta);
		RTC_TEST_EXPECT(delta >= 1 && delta <= 4,
				"advance delta %ds out of [1,4]", delta);
	}

	rtc_finish();
}

/* 3. 非法 set 参数：sec=99 / mday=32 / 2026-02-29（非闰年）→ -EINVAL
 *    （驱动 is_valid_time 分支）。 */
ZTEST(rtc, invalid_set_args)
{
	rtc_begin(RTC_TEST_INVALID_SET_ARGS);

	struct rtc_time bad = {
		.tm_sec = 99, .tm_min = 0, .tm_hour = 0,
		.tm_mday = 1, .tm_mon = 0, .tm_year = 126, .tm_wday = 0,
	};
	RTC_TEST_EXPECT(rtc_set_time(rtc_dev, &bad) == -EINVAL,
			"sec=99 ret %d", rtc_set_time(rtc_dev, &bad));

	bad.tm_sec = 0;
	bad.tm_mday = 32;
	RTC_TEST_EXPECT(rtc_set_time(rtc_dev, &bad) == -EINVAL,
			"mday=32 ret %d", rtc_set_time(rtc_dev, &bad));

	bad.tm_mday = 29;
	bad.tm_mon = 1; /* 2026 非闰年，2 月最多 28 天 */
	RTC_TEST_EXPECT(rtc_set_time(rtc_dev, &bad) == -EINVAL,
			"2026-02-29 ret %d", rtc_set_time(rtc_dev, &bad));

	rtc_finish();
}

/* 4. alarm set → get 往返：设 2026-07-15 08:20:10 后回读逐字段相等。 */
ZTEST(rtc, alarm_set_get)
{
	rtc_begin(RTC_TEST_ALARM_SET_GET);

	struct rtc_time a = {
		.tm_sec = 10, .tm_min = 20, .tm_hour = 8,
		.tm_mday = 15, .tm_mon = 6, .tm_year = 126, .tm_wday = 3,
	};
	int ret = rtc_alarm_set_time(rtc_dev, 0, rtc_alarm_full_mask(), &a);
	RTC_TEST_EXPECT(ret == 0, "alarm_set_time ret %d", ret);

	if (ret == 0) {
		struct rtc_time g = { 0 };
		uint16_t gmask = 0;

		ret = rtc_alarm_get_time(rtc_dev, 0, &gmask, &g);
		RTC_TEST_EXPECT(ret == 0, "alarm_get_time ret %d", ret);
		if (ret == 0) {
			RTC_TEST_EXPECT(g.tm_sec == a.tm_sec && g.tm_min == a.tm_min &&
					g.tm_hour == a.tm_hour,
					"hms %d:%d:%d != %d:%d:%d",
					g.tm_hour, g.tm_min, g.tm_sec,
					a.tm_hour, a.tm_min, a.tm_sec);
			RTC_TEST_EXPECT(g.tm_mday == a.tm_mday && g.tm_mon == a.tm_mon &&
					g.tm_year == a.tm_year && g.tm_wday == a.tm_wday,
					"date %d-%d-%d(w%d) != %d-%d-%d(w%d)",
					g.tm_year, g.tm_mon, g.tm_mday, g.tm_wday,
					a.tm_year, a.tm_mon, a.tm_mday, a.tm_wday);
		}
	}

	rtc_finish();
}

/* 5. 闹钟中断触发：设当前时间 +2s（带进位不跨天）→ pending=1 →
 *    等 4s → 回调标志置位 + pending=0（ISR 关 ALARM_EN）。
 *    回调在 ISR 上下文，只置 volatile 标志（§4.2）。 */
ZTEST(rtc, alarm_fires)
{
	rtc_begin(RTC_TEST_ALARM_FIRES);

	struct rtc_time cur = { 0 };
	int ret = rtc_get_time(rtc_dev, &cur);
	RTC_TEST_EXPECT(ret == 0, "get_time ret %d", ret);

	if (ret == 0) {
		struct rtc_time a = cur;

		a.tm_sec += 2;
		if (a.tm_sec > 59) { a.tm_sec -= 60; a.tm_min++; }
		if (a.tm_min > 59) { a.tm_min -= 60; a.tm_hour++; }
		if (a.tm_hour > 23) { /* 跨天：固定当天 23:59:02，不跨日 */
			a.tm_hour = 23; a.tm_min = 59; a.tm_sec = 2;
		}

		alarm_triggered = false;
		ret = rtc_alarm_set_callback(rtc_dev, 0, alarm_cb, NULL);
		RTC_TEST_EXPECT(ret == 0, "alarm_set_callback ret %d", ret);
		ret = rtc_alarm_set_time(rtc_dev, 0, rtc_alarm_full_mask(), &a);
		RTC_TEST_EXPECT(ret == 0, "alarm_set_time ret %d", ret);
		if (ret == 0) {
			RTC_TEST_EXPECT(rtc_alarm_is_pending(rtc_dev, 0) == 1,
					"alarm not pending after set");

			k_sleep(K_MSEC(4000));

			RTC_TEST_EXPECT(alarm_triggered,
					"alarm callback not fired in 4s");
			RTC_TEST_EXPECT(rtc_alarm_is_pending(rtc_dev, 0) == 0,
					"alarm still pending after fire");
		}
	}

	rtc_finish();
}

/* 6. 非法闹钟时间 → -EINVAL（驱动 is_valid_time 分支） */
ZTEST(rtc, alarm_invalid_args)
{
	rtc_begin(RTC_TEST_ALARM_INVALID_ARGS);

	struct rtc_time bad = {
		.tm_sec = 99, .tm_min = 0, .tm_hour = 0,
		.tm_mday = 1, .tm_mon = 0, .tm_year = 126, .tm_wday = 0,
	};
	RTC_TEST_EXPECT(rtc_alarm_set_time(rtc_dev, 0, 0, &bad) == -EINVAL,
			"alarm sec=99 ret %d", rtc_alarm_set_time(rtc_dev, 0, 0, &bad));

	rtc_finish();
}

/* 7. 校准往返：set +100000000 ppb（calib_cyc=6，get 反推精确相等）
 *    → get 回读 == 100000000。测试后恢复 0。 */
ZTEST(rtc, calibration_roundtrip)
{
	rtc_begin(RTC_TEST_CALIBRATION_ROUNDTRIP);

	int ret = rtc_set_calibration(rtc_dev, 100000000);
	RTC_TEST_EXPECT(ret == 0, "set_calibration ret %d", ret);

	if (ret == 0) {
		int32_t ppb = 0;

		ret = rtc_get_calibration(rtc_dev, &ppb);
		RTC_TEST_EXPECT(ret == 0, "get_calibration ret %d", ret);
		/* §13.4：回读校准值 INFO 上报 */
		RTC_TEST_TGT_INFO_VAR(rtc_test_name(), "ppb=%d", (int)ppb);
		RTC_TEST_EXPECT(ppb == 100000000,
				"ppb %d != 100000000", (int)ppb);
	}
	rtc_set_calibration(rtc_dev, 0); /* 恢复默认 */

	rtc_finish();
}

/* 8. 负校准值：set -100000000 → ret==0（驱动负值路径 floor_custom_divisor
 *    分支，cyc_1hz 减 1 + calib_cyc 补 60；get 反推不等价，只验证接受）。
 *    测试后恢复 0。 */
ZTEST(rtc, calibration_negative)
{
	rtc_begin(RTC_TEST_CALIBRATION_NEGATIVE);

	int ret = rtc_set_calibration(rtc_dev, -100000000);
	RTC_TEST_EXPECT(ret == 0, "set_calibration(-1e8) ret %d", ret);
	rtc_set_calibration(rtc_dev, 0);

	rtc_finish();
}

/* 9. get_calibration(NULL) → -EINVAL（驱动指针检查分支） */
ZTEST(rtc, calibration_null_arg)
{
	rtc_begin(RTC_TEST_CALIBRATION_NULL_ARG);

	RTC_TEST_EXPECT(rtc_get_calibration(rtc_dev, NULL) == -EINVAL,
			"get_calibration(NULL) ret %d",
			rtc_get_calibration(rtc_dev, NULL));

	rtc_finish();
}

/* ========== 汇总 ========== */

static void rtc_print_summary(void)
{
	int passed = 0, ran = 0;

	RTC_TEST_SUMMARY_BEGIN;
	for (int i = 0; i < RTC_TEST_COUNT; i++) {
		const char *name = rtc_test_names[i];

		if (test_results[i] < 0) {
			RTC_TEST_SUMMARY_ITEM(name, "NOT_RUN");
			continue;
		}
		ran++;
		if (test_results[i] == 0) {
			passed++;
			RTC_TEST_SUMMARY_ITEM(name, "PASS");
		} else if (test_reasons[i][0] != '\0') {
			RTC_TEST_SUMMARY_ITEM_REASON(name, "FAIL",
						      test_reasons[i]);
		} else {
			RTC_TEST_SUMMARY_ITEM(name, "FAIL");
		}
	}
	RTC_TEST_SUMMARY_END(passed, ran, !rtc_any_failed);
}

void rtc_test_run(void)
{
	tr_init(RTC_TEST_COUNT);

	if (!device_is_ready(rtc_dev)) {
		LOG_ERR("RTC device not ready");
		rtc_any_failed = true;
	}

	ztest_run_all(NULL, false, 1, 1);
	ztest_verify_all_test_suites_ran();

	rtc_print_summary();
	RTC_TEST_DONE_VAR(rtc_any_failed ? "FAIL" : "PASS");
}
