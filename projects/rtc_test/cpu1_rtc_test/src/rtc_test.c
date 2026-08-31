/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RTC (rtc_ls) 单核测试（ztest 原生框架）。
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
 * 测试函数为纯 ztest：zassert_* 原生断言（失败详情 file:line+msg 由 zassert
 * 自身 printk，harness 归因进报告 reason 列）；数值上报用 ZT_INFO 约定行
 * （§13.4，断言之前）。结果记账由 common/ztest_glue 的 zt_run_all 自动完成。
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>

#include "ztest_glue.h"

LOG_MODULE_DECLARE(rtc_test, LOG_LEVEL_INF);

extern const struct device *rtc_dev;

/* alarm 回调在 ISR 上下文（rtc_irq_handler → alarm_cb），只置标志（§4.2 禁 LOG） */
static volatile bool alarm_triggered;

static void alarm_cb(const struct device *dev, uint16_t id, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(id);
	ARG_UNUSED(user_data);

	alarm_triggered = true;
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
	struct rtc_time s = {
		.tm_sec = 56, .tm_min = 34, .tm_hour = 12,
		.tm_mday = 21, .tm_mon = 7, .tm_year = 126, .tm_wday = 5,
	};
	int ret = rtc_set_time(rtc_dev, &s);
	zassert_equal(ret, 0, "rtc_set_time ret %d", ret);

	struct rtc_time g = { 0 };

	ret = rtc_get_time(rtc_dev, &g);
	zassert_equal(ret, 0, "rtc_get_time ret %d", ret);
	zassert_equal(g.tm_mday, s.tm_mday,
		      "mday %d != %d", g.tm_mday, s.tm_mday);
	zassert_equal(g.tm_mon, s.tm_mon,
		      "mon %d != %d", g.tm_mon, s.tm_mon);
	zassert_equal(g.tm_year, s.tm_year,
		      "year %d != %d", g.tm_year, s.tm_year);
	zassert_equal(g.tm_wday, s.tm_wday,
		      "wday %d != %d", g.tm_wday, s.tm_wday);
	int delta = tm_total_sec(&g) - tm_total_sec(&s);

	/* §13.4：往返偏差 INFO 上报 */
	ZT_INFO("set_get_roundtrip", "delta_s=%d", delta);
	zassert_true(delta >= 0 && delta <= 5,
		     "set->get delta %ds out of [0,5]", delta);
}

/* 2. 时间前进：sleep 2.1s 前后两次 get，秒差 ∈ [1,4]（RTC 在计数）。 */
ZTEST(rtc, time_advances)
{
	struct rtc_time t1 = { 0 }, t2 = { 0 };
	int ret = rtc_get_time(rtc_dev, &t1);
	zassert_equal(ret, 0, "get#1 ret %d", ret);

	k_sleep(K_MSEC(2100));

	ret = rtc_get_time(rtc_dev, &t2);
	zassert_equal(ret, 0, "get#2 ret %d", ret);
	int delta = tm_total_sec(&t2) - tm_total_sec(&t1);

	/* §13.4：前进秒数 INFO 上报 */
	ZT_INFO("time_advances", "delta_s=%d", delta);
	zassert_true(delta >= 1 && delta <= 4,
		     "advance delta %ds out of [1,4]", delta);
}

/* 3. 非法 set 参数：sec=99 / mday=32 / 2026-02-29（非闰年）→ -EINVAL
 *    （驱动 is_valid_time 分支）。 */
ZTEST(rtc, invalid_set_args)
{
	struct rtc_time bad = {
		.tm_sec = 99, .tm_min = 0, .tm_hour = 0,
		.tm_mday = 1, .tm_mon = 0, .tm_year = 126, .tm_wday = 0,
	};
	int ret = rtc_set_time(rtc_dev, &bad);
	zassert_equal(ret, -EINVAL, "sec=99 ret %d", ret);

	bad.tm_sec = 0;
	bad.tm_mday = 32;
	ret = rtc_set_time(rtc_dev, &bad);
	zassert_equal(ret, -EINVAL, "mday=32 ret %d", ret);

	bad.tm_mday = 29;
	bad.tm_mon = 1; /* 2026 非闰年，2 月最多 28 天 */
	ret = rtc_set_time(rtc_dev, &bad);
	zassert_equal(ret, -EINVAL, "2026-02-29 ret %d", ret);
}

/* 4. alarm set → get 往返：设 2026-07-15 08:20:10 后回读逐字段相等。 */
ZTEST(rtc, alarm_set_get)
{
	struct rtc_time a = {
		.tm_sec = 10, .tm_min = 20, .tm_hour = 8,
		.tm_mday = 15, .tm_mon = 6, .tm_year = 126, .tm_wday = 3,
	};
	int ret = rtc_alarm_set_time(rtc_dev, 0, rtc_alarm_full_mask(), &a);
	zassert_equal(ret, 0, "alarm_set_time ret %d", ret);

	struct rtc_time g = { 0 };
	uint16_t gmask = 0;

	ret = rtc_alarm_get_time(rtc_dev, 0, &gmask, &g);
	zassert_equal(ret, 0, "alarm_get_time ret %d", ret);
	zassert_true(g.tm_sec == a.tm_sec && g.tm_min == a.tm_min &&
		     g.tm_hour == a.tm_hour,
		     "hms %d:%d:%d != %d:%d:%d",
		     g.tm_hour, g.tm_min, g.tm_sec,
		     a.tm_hour, a.tm_min, a.tm_sec);
	zassert_true(g.tm_mday == a.tm_mday && g.tm_mon == a.tm_mon &&
		     g.tm_year == a.tm_year && g.tm_wday == a.tm_wday,
		     "date %d-%d-%d(w%d) != %d-%d-%d(w%d)",
		     g.tm_year, g.tm_mon, g.tm_mday, g.tm_wday,
		     a.tm_year, a.tm_mon, a.tm_mday, a.tm_wday);
}

/* 5. 闹钟中断触发：设当前时间 +2s（带进位不跨天）→ pending=1 →
 *    等 4s → 回调标志置位 + pending=0（ISR 关 ALARM_EN）。
 *    回调在 ISR 上下文，只置 volatile 标志（§4.2）。 */
ZTEST(rtc, alarm_fires)
{
	struct rtc_time cur = { 0 };
	int ret = rtc_get_time(rtc_dev, &cur);
	zassert_equal(ret, 0, "get_time ret %d", ret);

	struct rtc_time a = cur;

	a.tm_sec += 2;
	if (a.tm_sec > 59) { a.tm_sec -= 60; a.tm_min++; }
	if (a.tm_min > 59) { a.tm_min -= 60; a.tm_hour++; }
	if (a.tm_hour > 23) { /* 跨天：固定当天 23:59:02，不跨日 */
		a.tm_hour = 23; a.tm_min = 59; a.tm_sec = 2;
	}

	alarm_triggered = false;
	ret = rtc_alarm_set_callback(rtc_dev, 0, alarm_cb, NULL);
	zassert_equal(ret, 0, "alarm_set_callback ret %d", ret);
	ret = rtc_alarm_set_time(rtc_dev, 0, rtc_alarm_full_mask(), &a);
	zassert_equal(ret, 0, "alarm_set_time ret %d", ret);
	zassert_equal(rtc_alarm_is_pending(rtc_dev, 0), 1,
		      "alarm not pending after set");

	k_sleep(K_MSEC(4000));

	zassert_true(alarm_triggered,
		     "alarm callback not fired in 4s");
	zassert_equal(rtc_alarm_is_pending(rtc_dev, 0), 0,
		      "alarm still pending after fire");
}

/* 6. 非法闹钟时间 → -EINVAL（驱动 is_valid_time 分支） */
ZTEST(rtc, alarm_invalid_args)
{
	struct rtc_time bad = {
		.tm_sec = 99, .tm_min = 0, .tm_hour = 0,
		.tm_mday = 1, .tm_mon = 0, .tm_year = 126, .tm_wday = 0,
	};
	int ret = rtc_alarm_set_time(rtc_dev, 0, 0, &bad);
	zassert_equal(ret, -EINVAL, "alarm sec=99 ret %d", ret);
}

/* 7. 校准往返：set +100000000 ppb（calib_cyc=6，get 反推精确相等）
 *    → get 回读 == 100000000。测试后恢复 0。 */
ZTEST(rtc, calibration_roundtrip)
{
	int ret = rtc_set_calibration(rtc_dev, 100000000);
	zassert_equal(ret, 0, "set_calibration ret %d", ret);

	int32_t ppb = 0;

	ret = rtc_get_calibration(rtc_dev, &ppb);
	zassert_equal(ret, 0, "get_calibration ret %d", ret);
	/* §13.4：回读校准值 INFO 上报 */
	ZT_INFO("calibration_roundtrip", "ppb=%d", (int)ppb);
	zassert_equal(ppb, 100000000,
		      "ppb %d != 100000000", (int)ppb);
	rtc_set_calibration(rtc_dev, 0); /* 恢复默认 */
}

/* 8. 负校准值：set -100000000 → ret==0（驱动负值路径 floor_custom_divisor
 *    分支，cyc_1hz 减 1 + calib_cyc 补 60；get 反推不等价，只验证接受）。
 *    测试后恢复 0。 */
ZTEST(rtc, calibration_negative)
{
	int ret = rtc_set_calibration(rtc_dev, -100000000);
	zassert_equal(ret, 0, "set_calibration(-1e8) ret %d", ret);
	rtc_set_calibration(rtc_dev, 0);
}

/* 9. get_calibration(NULL) → -EINVAL（驱动指针检查分支） */
ZTEST(rtc, calibration_null_arg)
{
	int ret = rtc_get_calibration(rtc_dev, NULL);
	zassert_equal(ret, -EINVAL, "get_calibration(NULL) ret %d", ret);
}
