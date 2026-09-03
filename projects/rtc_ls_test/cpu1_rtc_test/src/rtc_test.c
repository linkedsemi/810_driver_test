/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RTC (rtc_ls) 单核测试。
 *
 * 被测驱动：zephyr/drivers/rtc/rtc_ls.c（linkedsemi_ls_rtc，rtc@4006fc00）
 *   - 硬件 RTC 寄存器（reg_rtcv2_type.h），cyc-1hz=32768（属性），CALIB 补偿
 *   - set_time：先 rtc_en=0 → 写 TIME/CAL → 翻转 SET_TGGL → rtc_en=1；
 *     TGGL 后硬件异步载入 CUR（us~ms），返回后不可立即 get_time，调用方需自行等待
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
 #include <stdint.h>
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
 
 /* alarm 回调在 ISR 上下文（rtc_irq_handler → alarm_cb），置标志 */
 static volatile bool alarm_triggered;
 
 static void alarm_cb(const struct device *dev, uint16_t id, void *user_data)
 {
	 ARG_UNUSED(dev);
	 ARG_UNUSED(id);
	 ARG_UNUSED(user_data);
 
	 alarm_triggered = true;
	 printk(RTC_TEST_TOKEN_PREFIX ":TGT:INFO:alarm_cb:callback triggered\n");
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
 #if RTC_EN_SET_GET_ROUNDTRIP
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
 
		 /* 硬件载入 ms 级异步；set 后短暂延时再 get（见 rtc_ls_set_time 注释） */
		 k_sleep(K_MSEC(5));
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
 #endif /* RTC_EN_SET_GET_ROUNDTRIP */
 
 /*
  * 日历边界（Zephyr rtc.h：tm_mon [0,11]，tm_year=公历年-1900，tm_wday 0=周日）。
  * 设 23:59:57，等 3s，期望次日/次月 00:00:00；每条用例均打印 set/expect/get。
  */
 #if RTC_EN_CALENDAR_ROLLOVER
 
 #define RTC_HUMAN_FMT "%04d年%02d月%02d日 周%d %02d:%02d:%02d"
 #define RTC_HUMAN_ARGS(t) \
	 (t)->tm_year + 1900, (t)->tm_mon + 1, (t)->tm_mday, (t)->tm_wday, \
	 (t)->tm_hour, (t)->tm_min, (t)->tm_sec
 
 /* Sakamoto：tm_wday，0=周日 */
 static int rtc_test_wday(int year_since_1900, int mon, int mday)
 {
	 static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	 int y = year_since_1900 + 1900;
	 int m = mon + 1;
 
	 if (m < 3) {
		 y--;
	 }
	 return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + mday) % 7;
 }
 
 static void tm_at(struct rtc_time *tm, int year, int mon, int mday,
		   int hour, int min, int sec)
 {
	 memset(tm, 0, sizeof(*tm));
	 tm->tm_year = year;
	 tm->tm_mon = mon;
	 tm->tm_mday = mday;
	 tm->tm_wday = rtc_test_wday(year, mon, mday);
	 tm->tm_hour = hour;
	 tm->tm_min = min;
	 tm->tm_sec = sec;
	 tm->tm_yday = -1;
	 tm->tm_isdst = -1;
 }
 
 static void rtc_print_case(const char *tag,
				const struct rtc_time *set,
				const struct rtc_time *expect,
				const struct rtc_time *get)
 {
	 RTC_TEST_TGT_INFO_VAR(rtc_test_name(),
					"%s:set=" RTC_HUMAN_FMT
					" expect=" RTC_HUMAN_FMT
					" get=" RTC_HUMAN_FMT,
					tag,
					RTC_HUMAN_ARGS(set),
					RTC_HUMAN_ARGS(expect),
					RTC_HUMAN_ARGS(get));
 }
 
 struct rtc_rollover_case {
	 const char *tag;
	 int year;
	 int mon;
	 int mday;
	 int exp_year;
	 int exp_mon;
	 int exp_mday;
 };
 
 /* 23:59:57 + 3s → 期望 exp_* 日 00:00:00（秒容差 [0,1]） */
 static void run_rollover_case(const struct rtc_rollover_case *c)
 {
	 struct rtc_time before;
	 struct rtc_time expect;
	 struct rtc_time got = { 0 };
	 int ret;
 
	 tm_at(&before, c->year, c->mon, c->mday, 23, 59, 57);
	 tm_at(&expect, c->exp_year, c->exp_mon, c->exp_mday, 0, 0, 0);
 
	 ret = rtc_set_time(rtc_dev, &before);
	 if (ret != 0) {
		 RTC_TEST_EXPECT(false, "%s: rtc_set_time ret %d", c->tag, ret);
		 rtc_print_case(c->tag, &before, &expect, &before);
		 return;
	 }
 
	 k_sleep(K_MSEC(3100));
 
	 ret = rtc_get_time(rtc_dev, &got);
	 if (ret != 0) {
		 RTC_TEST_EXPECT(false, "%s: rtc_get_time ret %d", c->tag, ret);
		 return;
	 }
 
	 rtc_print_case(c->tag, &before, &expect, &got);
 
	 if (got.tm_year != expect.tm_year || got.tm_mon != expect.tm_mon ||
		 got.tm_mday != expect.tm_mday || got.tm_hour != 0 ||
		 got.tm_min != 0 || got.tm_sec > 1 ||
		 got.tm_wday != expect.tm_wday) {
		 RTC_TEST_EXPECT(false,
				 "%s: got " RTC_HUMAN_FMT " expect " RTC_HUMAN_FMT,
				 c->tag, RTC_HUMAN_ARGS(&got), RTC_HUMAN_ARGS(&expect));
	 }
 }
 
 static void add_in_month_rollover(const char *tag, int year, int mon, int mday)
 {
	 struct rtc_rollover_case c = {
		 .tag = tag,
		 .year = year,
		 .mon = mon,
		 .mday = mday,
		 .exp_year = year,
		 .exp_mon = mon,
		 .exp_mday = mday + 1,
	 };
 
	 run_rollover_case(&c);
 }
 
 static void add_month_end_rollover(const char *tag, int year, int mon, int mday)
 {
	 struct rtc_rollover_case c = {
		 .tag = tag,
		 .year = year,
		 .mon = mon,
		 .mday = mday,
		 .exp_year = year,
		 .exp_mon = mon + 1,
		 .exp_mday = 1,
	 };
 
	 if (c.exp_mon > 11) {
		 c.exp_mon = 0;
		 c.exp_year++;
	 }
 
	 run_rollover_case(&c);
 }
 
 ZTEST(rtc, calendar_rollover)
 {
	 rtc_begin(RTC_TEST_CALENDAR_ROLLOVER);
 
	 /*
	  * 2026 年（tm_year=126，tm_mon 0=一月）。
	  * 设 23:59:57，等 3s，期望 00:00:00（秒允许 0 或 1）。
	  */
 
	 /* 1. 2026-01-30 23:59:57 → 2026-01-31 00:00:00 */
	 add_in_month_rollover("jan30_to_31", 126, 0, 30);
 
	 /* 2. 2026-01-31 23:59:57 → 2026-02-01 00:00:00 */
	 add_month_end_rollover("jan31_to_feb1", 126, 0, 31);
 
	 /* 3. 2026-02-27 23:59:57 → 2026-02-28 00:00:00 */
	 add_in_month_rollover("feb27_to_28", 126, 1, 27);
 
	 /* 4. 2026-02-28 23:59:57 → 2026-03-01 00:00:00（平年） */
	 add_month_end_rollover("feb28_to_mar1", 126, 1, 28);
 
	 /* 5–6. 三月（31 天） */
	 add_in_month_rollover("mar30_to_31", 126, 2, 30);
	 add_month_end_rollover("mar31_to_apr1", 126, 2, 31);
 
	 /* 7–8. 四月（30 天） */
	 add_in_month_rollover("apr29_to_30", 126, 3, 29);
	 add_month_end_rollover("apr30_to_may1", 126, 3, 30);
 
	 /* 9–10. 五月（31 天） */
	 add_in_month_rollover("may30_to_31", 126, 4, 30);
	 add_month_end_rollover("may31_to_jun1", 126, 4, 31);
 
	 /* 六月（30 天） */
	 add_in_month_rollover("jun29_to_30", 126, 5, 29);
	 add_month_end_rollover("jun30_to_jul1", 126, 5, 30);
 
	 /* 七月（31 天） */
	 add_in_month_rollover("jul30_to_31", 126, 6, 30);
	 add_month_end_rollover("jul31_to_aug1", 126, 6, 31);
 
	 /* 八月（31 天） */
	 add_in_month_rollover("aug30_to_31", 126, 7, 30);
	 add_month_end_rollover("aug31_to_sep1", 126, 7, 31);
 
	 /* 九月（30 天） */
	 add_in_month_rollover("sep29_to_30", 126, 8, 29);
	 add_month_end_rollover("sep30_to_oct1", 126, 8, 30);
 
	 /* 十月（31 天） */
	 add_in_month_rollover("oct30_to_31", 126, 9, 30);
	 add_month_end_rollover("oct31_to_nov1", 126, 9, 31);
 
	 /* 十一月（30 天） */
	 add_in_month_rollover("nov29_to_30", 126, 10, 29);
	 add_month_end_rollover("nov30_to_dec1", 126, 10, 30);
 
	 /* 十二月（31 天）→ 2027-01-01 */
	 add_in_month_rollover("dec30_to_31", 126, 11, 30);
	 add_month_end_rollover("dec31_to_jan1", 126, 11, 31);
 
	 /* 2028 闰年二月（tm_year=128） */
	 add_in_month_rollover("feb28_to_29_leap28", 128, 1, 28);
	 add_month_end_rollover("feb29_to_mar1_leap28", 128, 1, 29);
 
	 rtc_finish();
 }
 #endif /* RTC_EN_CALENDAR_ROLLOVER */
 
 /* 2. 时间前进：sleep 2.1s 前后两次 get，秒差 ∈ [1,4]（RTC 在计数）。 */
 #if RTC_EN_TIME_ADVANCES
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
 #endif /* RTC_EN_TIME_ADVANCES */
 
 /* 3. 非法 set 参数：sec=99 / mday=32 / 2026-02-29（非闰年）→ -EINVAL
  *    （驱动 is_valid_time 分支）。 */
 #if RTC_EN_INVALID_SET_ARGS
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
 #endif /* RTC_EN_INVALID_SET_ARGS */
 
 /* 4. alarm set → get 往返：设 2026-07-15 08:20:10 后回读逐字段相等。 */
 #if RTC_EN_ALARM_SET_GET
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
 #endif /* RTC_EN_ALARM_SET_GET */
 
 /* 5. 闹钟：2026-09-01 08:00:00 → 等 1s → 武装 08:00:03 → 等 6s 检查 */
 #if RTC_EN_ALARM_FIRES
 
 /* 2026-09-01(二) 08:00:00 */
 static const struct rtc_time rtc_test_alarm_base = {
	 .tm_sec = 0, .tm_min = 0, .tm_hour = 8,
	 .tm_mday = 1, .tm_mon = 8, .tm_year = 126, .tm_wday = 2,
 };
 
 #define RTC_TEST_ALARM_OFFSET_SEC  3     /* 08:00:03，约武装后 2s 响 */
 #define RTC_TEST_ALARM_SETTLE_MS   1000  /* set_time 后等 CUR 载入 */
 #define RTC_TEST_ALARM_WAIT_MS     6000  /* 约 6s 内应已响，再留余量 */
 
 static void rtc_test_alarm_add_sec(struct rtc_time *t, int sec)
 {
	 t->tm_sec += sec;
	 if (t->tm_sec > 59) {
		 t->tm_sec -= 60;
		 t->tm_min++;
	 }
	 if (t->tm_min > 59) {
		 t->tm_min -= 60;
		 t->tm_hour++;
	 }
 }
 
 static int rtc_test_set_fixed_base_time(void)
 {
	 return rtc_set_time(rtc_dev, &rtc_test_alarm_base);
 }
 
 /* 年月日必须仍是基准日；时分秒落在 [lo_sec, hi_sec]（当天秒数） */
 static void rtc_test_expect_now_window(const char *phase,
						int lo_sec, int hi_sec,
						const struct rtc_time *now)
 {
	 RTC_TEST_EXPECT(now->tm_year == rtc_test_alarm_base.tm_year &&
			 now->tm_mon == rtc_test_alarm_base.tm_mon &&
			 now->tm_mday == rtc_test_alarm_base.tm_mday,
			 "%s: date %04d-%02d-%02d != 2026-09-01",
			 phase,
			 now->tm_year + 1900, now->tm_mon + 1, now->tm_mday);
 
	 int sec = tm_total_sec(now);
 
	 RTC_TEST_EXPECT(sec >= lo_sec && sec <= hi_sec,
			 "%s: time %02d:%02d:%02d sec=%d not in [%d,%d]",
			 phase,
			 now->tm_hour, now->tm_min, now->tm_sec,
			 sec, lo_sec, hi_sec);
 }
 
 static int rtc_test_arm_alarm_at_offset(int sec)
 {
	 struct rtc_time a = rtc_test_alarm_base;
 
	 rtc_test_alarm_add_sec(&a, sec);
	 alarm_triggered = false;
 
	 int ret = rtc_alarm_set_callback(rtc_dev, 0, alarm_cb, NULL);
 
	 if (ret != 0) {
		 return ret;
	 }
 
	 ret = rtc_alarm_set_time(rtc_dev, 0, rtc_alarm_full_mask(), &a);
	 if (ret == 0) {
		 RTC_TEST_TGT_INFO_VAR(rtc_test_name(),
						"alarm_at=%04d-%02d-%02d %02d:%02d:%02d",
						a.tm_year + 1900, a.tm_mon + 1, a.tm_mday,
						a.tm_hour, a.tm_min, a.tm_sec);
	 }
	 return ret;
 }
 
 static void rtc_test_expect_alarm_fired(const char *phase)
 {
	 struct rtc_time now = { 0 };
	 int get_ret = rtc_get_time(rtc_dev, &now);
	 int pending = rtc_alarm_is_pending(rtc_dev, 0);
	 /* 武装约在 08:00:01，等 8s → 约 08:00:09；闹钟 08:00:03 */
	 const int lo = 8 * 3600 + 3;   /* >= 08:00:03 */
	 const int hi = 8 * 3600 + 15;  /* <= 08:00:15 */
 
	 if (get_ret == 0) {
		 RTC_TEST_TGT_INFO_VAR(rtc_test_name(),
						"%s:triggered=%d,pending=%d,"
						"now=%04d-%02d-%02d %02d:%02d:%02d",
						phase,
						(int)alarm_triggered,
						pending,
						now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
						now.tm_hour, now.tm_min, now.tm_sec);
	 } else {
		 RTC_TEST_TGT_INFO_VAR(rtc_test_name(),
						"%s:triggered=%d,pending=%d,get_time_ret=%d",
						phase,
						(int)alarm_triggered,
						pending,
						get_ret);
	 }
 
	 RTC_TEST_EXPECT(alarm_triggered,
			 "%s: alarm callback not seen in %ds", phase,
			 RTC_TEST_ALARM_WAIT_MS / 1000);
	 RTC_TEST_EXPECT(pending == 0,
			 "%s: alarm still pending after fire", phase);
	 RTC_TEST_EXPECT(get_ret == 0, "%s: rtc_get_time ret %d", phase, get_ret);
	 if (get_ret == 0) {
		 rtc_test_expect_now_window(phase, lo, hi, &now);
	 }
 }
 
 ZTEST(rtc, alarm_fires)
 {
	 rtc_begin(RTC_TEST_ALARM_FIRES);
 
	 int ret = rtc_test_set_fixed_base_time();
 
	 RTC_TEST_EXPECT(ret == 0, "rtc_set_time ret %d", ret);
	 if (ret != 0) {
		 rtc_finish();
		 return;
	 }
 
	 /* set 08:00:00 后等 1s，get 期望 ~08:00:01，允许 ±1s */
	 k_sleep(K_MSEC(RTC_TEST_ALARM_SETTLE_MS));
 
	 struct rtc_time after_set = { 0 };
	 const int expect_sec = tm_total_sec(&rtc_test_alarm_base) + 1; /* 08:00:01 */
 
	 ret = rtc_get_time(rtc_dev, &after_set);
	 RTC_TEST_EXPECT(ret == 0, "get after set ret %d", ret);
	 if (ret == 0) {
		 int delta = tm_total_sec(&after_set) - expect_sec;
 
		 RTC_TEST_TGT_INFO_VAR(rtc_test_name(),
						"after_set:now=%04d-%02d-%02d %02d:%02d:%02d,"
						"expect=08:00:01,delta_s=%d",
						after_set.tm_year + 1900, after_set.tm_mon + 1,
						after_set.tm_mday,
						after_set.tm_hour, after_set.tm_min, after_set.tm_sec,
						delta);
		 RTC_TEST_EXPECT(after_set.tm_year == rtc_test_alarm_base.tm_year &&
				 after_set.tm_mon == rtc_test_alarm_base.tm_mon &&
				 after_set.tm_mday == rtc_test_alarm_base.tm_mday,
				 "after_set: date %04d-%02d-%02d != 2026-09-01",
				 after_set.tm_year + 1900, after_set.tm_mon + 1,
				 after_set.tm_mday);
		 RTC_TEST_EXPECT(delta >= -1 && delta <= 1,
				 "after_set: delta %ds out of ±1s (got %02d:%02d:%02d)",
				 delta,
				 after_set.tm_hour, after_set.tm_min, after_set.tm_sec);
	 }
 
	 ret = rtc_test_arm_alarm_at_offset(RTC_TEST_ALARM_OFFSET_SEC);
	 RTC_TEST_EXPECT(ret == 0, "rtc_alarm_set_time ret %d", ret);
	 if (ret == 0) {
		 RTC_TEST_EXPECT(rtc_alarm_is_pending(rtc_dev, 0) == 1,
				 "alarm not pending after set");
		 k_sleep(K_MSEC(RTC_TEST_ALARM_WAIT_MS));
		 rtc_test_expect_alarm_fired("fire1");
	 }
 
	 rtc_finish();
 }
 #endif /* RTC_EN_ALARM_FIRES */
 
 /* 6. 非法闹钟时间 → -EINVAL（驱动 is_valid_time 分支） */
 #if RTC_EN_ALARM_INVALID_ARGS
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
 #endif /* RTC_EN_ALARM_INVALID_ARGS */
 
 /* 7. 校准往返：set +100000000 ppb（calib_cyc=6，get 反推精确相等）
  *    → get 回读 == 100000000。测试后恢复 0。 */
 #if RTC_EN_CALIBRATION_ROUNDTRIP
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
 #endif /* RTC_EN_CALIBRATION_ROUNDTRIP */
 
 /* 8. 负校准值：set -100000000 → ret==0（驱动负值路径 floor_custom_divisor
  *    分支，cyc_1hz 减 1 + calib_cyc 补 60；get 反推不等价，只验证接受）。
  *    测试后恢复 0。 */
 #if RTC_EN_CALIBRATION_NEGATIVE
 ZTEST(rtc, calibration_negative)
 {
	 rtc_begin(RTC_TEST_CALIBRATION_NEGATIVE);
 
	 int ret = rtc_set_calibration(rtc_dev, -100000000);
	 RTC_TEST_EXPECT(ret == 0, "set_calibration(-1e8) ret %d", ret);
	 rtc_set_calibration(rtc_dev, 0);
 
	 rtc_finish();
 }
 #endif /* RTC_EN_CALIBRATION_NEGATIVE */
 
 /* 9. get_calibration(NULL) → -EINVAL（驱动指针检查分支） */
 #if RTC_EN_CALIBRATION_NULL_ARG
 ZTEST(rtc, calibration_null_arg)
 {
	 rtc_begin(RTC_TEST_CALIBRATION_NULL_ARG);
 
	 RTC_TEST_EXPECT(rtc_get_calibration(rtc_dev, NULL) == -EINVAL,
			 "get_calibration(NULL) ret %d",
			 rtc_get_calibration(rtc_dev, NULL));
 
	 rtc_finish();
 }
 #endif /* RTC_EN_CALIBRATION_NULL_ARG */
 
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
 