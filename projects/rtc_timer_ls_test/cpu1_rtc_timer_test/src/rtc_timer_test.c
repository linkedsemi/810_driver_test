/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * rtc_timer_ls（zephyr/drivers/rtc/rtc_timer_ls.c）单核边界测试。
 *
 * 驱动能力：仅 set_time / get_time（无 alarm / calibration）。
 * calendar_valid 边界（tm 字段，年 = 公历 - 1900）：
 *   year ∈ [70,199]  → 1970..2099
 *   mon  ∈ [0,11]
 *   mday 按月/闰年
 *   hour∈[0,23] min/sec∈[0,59]
 *   wday ∈ [-1,6]；yday=-1 或 [0,365/364]
 *   isdst ∈ {-1,0,1}；nsec ∈ [0,999999999]
 * get：未初始化（SFT_CTRL=0xFFFFFFFF）→ -ENODATA；成功时强制 isdst=-1,nsec=0
 *
 * 测试名按字母序 = ztest 链接序（dump_results TEST_NAMES 必须一致）。
 */

#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>

#include "ztest_glue.h"

#include <soc.h>
#include "reg_sec_pmu_rg.h"

LOG_MODULE_DECLARE(rtc_timer_test, LOG_LEVEL_INF);

extern const struct device *rtc_dev;

static void fill_base(struct rtc_time *tm)
{
	memset(tm, 0, sizeof(*tm));
	tm->tm_sec = 0;
	tm->tm_min = 0;
	tm->tm_hour = 12;
	tm->tm_mday = 15;
	tm->tm_mon = 5;   /* June */
	tm->tm_year = 126; /* 2026 */
	tm->tm_wday = -1;
	tm->tm_yday = -1;
	tm->tm_isdst = -1;
	tm->tm_nsec = 0;
}

static void assert_civil_eq(const struct rtc_time *got, const struct rtc_time *exp)
{
	zassert_equal(got->tm_year, exp->tm_year, "year %d != %d", got->tm_year, exp->tm_year);
	zassert_equal(got->tm_mon, exp->tm_mon, "mon %d != %d", got->tm_mon, exp->tm_mon);
	zassert_equal(got->tm_mday, exp->tm_mday, "mday %d != %d", got->tm_mday, exp->tm_mday);
	zassert_equal(got->tm_hour, exp->tm_hour, "hour %d != %d", got->tm_hour, exp->tm_hour);
	zassert_equal(got->tm_min, exp->tm_min, "min %d != %d", got->tm_min, exp->tm_min);
	/* 秒允许 ±1：set→get 可能跨秒 */
	zassert_true(got->tm_sec >= exp->tm_sec - 1 && got->tm_sec <= exp->tm_sec + 1,
		     "sec %d not near %d", got->tm_sec, exp->tm_sec);
}

ZTEST_SUITE(rtc_timer, NULL, NULL, NULL, NULL, NULL);

/* ---- 设备就绪 ---- */
ZTEST(rtc_timer, a_device_ready)
{
	zassert_true(device_is_ready(rtc_dev), "rtc_timer device not ready");
}

/* ---- 未 set：get → -ENODATA（驱动 + platform SFT_CTRL 哨兵） ---- */
ZTEST(rtc_timer, a_get_uninitialized)
{
	struct rtc_time tm;

	SEC_PMU->SFT_CTRL[4] = 0xFFFFFFFFu;
	zassert_equal(rtc_get_time(rtc_dev, &tm), -ENODATA,
		      "get before set should be -ENODATA");
}

/* ---- 正常往返 ---- */
ZTEST(rtc_timer, b_set_get_roundtrip)
{
	struct rtc_time set_tm, get_tm;

	fill_base(&set_tm);
	set_tm.tm_sec = 30;
	zassert_equal(rtc_set_time(rtc_dev, &set_tm), 0, "set_time failed");
	zassert_equal(rtc_get_time(rtc_dev, &get_tm), 0, "get_time failed");
	assert_civil_eq(&get_tm, &set_tm);
	ZT_INFO("b_set_get_roundtrip", "y=%d mon=%d d=%d %02d:%02d:%02d",
		get_tm.tm_year + 1900, get_tm.tm_mon + 1, get_tm.tm_mday,
		get_tm.tm_hour, get_tm.tm_min, get_tm.tm_sec);
}

/* ---- 时间前进 ---- */
ZTEST(rtc_timer, b_time_advances)
{
	struct rtc_time t0, t1;
	int64_t s0, s1;

	fill_base(&t0);
	t0.tm_sec = 10;
	zassert_equal(rtc_set_time(rtc_dev, &t0), 0, "set");
	zassert_equal(rtc_get_time(rtc_dev, &t0), 0, "get0");
	s0 = (int64_t)t0.tm_hour * 3600 + t0.tm_min * 60 + t0.tm_sec;
	k_sleep(K_SECONDS(2));
	zassert_equal(rtc_get_time(rtc_dev, &t1), 0, "get1");
	s1 = (int64_t)t1.tm_hour * 3600 + t1.tm_min * 60 + t1.tm_sec;
	if (s1 < s0) {
		s1 += 24 * 3600; /* 跨日 */
	}
	zassert_true(s1 - s0 >= 1 && s1 - s0 <= 3,
		     "delta %lld not in [1,3]", (long long)(s1 - s0));
	ZT_INFO("b_time_advances", "delta_s=%lld", (long long)(s1 - s0));
}

/* ---- get 归一化 isdst/nsec ---- */
ZTEST(rtc_timer, b_get_normalizes_meta)
{
	struct rtc_time set_tm, get_tm;

	fill_base(&set_tm);
	set_tm.tm_isdst = 1;
	set_tm.tm_nsec = 123456789;
	zassert_equal(rtc_set_time(rtc_dev, &set_tm), 0, "set with meta");
	zassert_equal(rtc_get_time(rtc_dev, &get_tm), 0, "get");
	zassert_equal(get_tm.tm_isdst, -1, "isdst should be -1");
	zassert_equal(get_tm.tm_nsec, 0, "nsec should be 0");
}

/* ---- 年边界 1970 / 2099 ---- */
ZTEST(rtc_timer, c_year_bounds)
{
	struct rtc_time tm;

	fill_base(&tm);
	tm.tm_year = 69; /* 1969 */
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "year 69");
	tm.tm_year = 70; /* 1970 */
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "year 70");
	tm.tm_year = 199; /* 2099 */
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "year 199");
	tm.tm_year = 200; /* 2100 */
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "year 200");
}

/* ---- 月边界 ---- */
ZTEST(rtc_timer, c_month_bounds)
{
	struct rtc_time tm;

	fill_base(&tm);
	tm.tm_mon = -1;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "mon -1");
	tm.tm_mon = 12;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "mon 12");
	tm.tm_mon = 0;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "mon 0");
	tm.tm_mon = 11;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "mon 11");
}

/* ---- 30 天月 ---- */
ZTEST(rtc_timer, d_day_bounds_30day)
{
	struct rtc_time tm;

	fill_base(&tm);
	tm.tm_mon = 3; /* April */
	tm.tm_mday = 30;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "Apr 30");
	tm.tm_mday = 31;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "Apr 31");
	tm.tm_mday = 0;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "mday 0");
}

/* ---- 二月非闰 ---- */
ZTEST(rtc_timer, d_day_bounds_feb_nonleap)
{
	struct rtc_time tm;

	fill_base(&tm);
	tm.tm_year = 123; /* 2023 non-leap */
	tm.tm_mon = 1;
	tm.tm_mday = 28;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "2023-02-28");
	tm.tm_mday = 29;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "2023-02-29");
}

/* ---- 二月闰年 ---- */
ZTEST(rtc_timer, d_day_bounds_feb_leap)
{
	struct rtc_time tm;

	fill_base(&tm);
	tm.tm_year = 124; /* 2024 leap */
	tm.tm_mon = 1;
	tm.tm_mday = 29;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "2024-02-29");
	tm.tm_mday = 30;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "2024-02-30");
}

/* ---- 时分秒 ---- */
ZTEST(rtc_timer, e_hms_bounds)
{
	struct rtc_time tm;

	fill_base(&tm);
	tm.tm_hour = 23;
	tm.tm_min = 59;
	tm.tm_sec = 59;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "23:59:59");
	tm.tm_hour = 24;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "hour 24");
	tm.tm_hour = 12;
	tm.tm_min = 60;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "min 60");
	tm.tm_min = 0;
	tm.tm_sec = 60;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "sec 60");
}

/* ---- wday ---- */
ZTEST(rtc_timer, e_wday_bounds)
{
	struct rtc_time tm;

	fill_base(&tm);
	tm.tm_wday = -1;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "wday -1");
	tm.tm_wday = 6;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "wday 6");
	tm.tm_wday = -2;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "wday -2");
	tm.tm_wday = 7;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "wday 7");
}

/* ---- yday ---- */
ZTEST(rtc_timer, e_yday_bounds)
{
	struct rtc_time tm;

	fill_base(&tm);
	tm.tm_year = 124; /* leap 2024 */
	tm.tm_yday = -1;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "yday -1");
	tm.tm_yday = 365;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "leap yday 365");
	tm.tm_yday = 366;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "leap yday 366");
	tm.tm_year = 123; /* non-leap */
	tm.tm_yday = 364;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "nonleap yday 364");
	tm.tm_yday = 365;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "nonleap yday 365");
	tm.tm_yday = -2;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "yday -2");
}

/* ---- isdst / nsec ---- */
ZTEST(rtc_timer, e_isdst_nsec_bounds)
{
	struct rtc_time tm;

	fill_base(&tm);
	tm.tm_isdst = -1;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "isdst -1");
	tm.tm_isdst = 0;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "isdst 0");
	tm.tm_isdst = 1;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "isdst 1");
	tm.tm_isdst = 2;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "isdst 2");

	fill_base(&tm);
	tm.tm_nsec = 0;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "nsec 0");
	tm.tm_nsec = 999999999;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "nsec max");
	tm.tm_nsec = -1;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "nsec -1");
	tm.tm_nsec = 1000000000;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "nsec 1e9");
}

/* ---- 31 天月合法 + 日历滚动抽样（12-31 → 次日） ---- */
ZTEST(rtc_timer, f_day_31_and_rollover_sample)
{
	struct rtc_time tm, got;

	fill_base(&tm);
	tm.tm_mon = 0; /* Jan */
	tm.tm_mday = 31;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "Jan 31");
	tm.tm_mon = 0;
	tm.tm_mday = 32;
	zassert_equal(rtc_set_time(rtc_dev, &tm), -EINVAL, "Jan 32");

	fill_base(&tm);
	tm.tm_mon = 11; /* Dec */
	tm.tm_mday = 31;
	tm.tm_hour = 23;
	tm.tm_min = 59;
	tm.tm_sec = 58;
	tm.tm_year = 126;
	zassert_equal(rtc_set_time(rtc_dev, &tm), 0, "set near year end");
	k_sleep(K_SECONDS(3));
	zassert_equal(rtc_get_time(rtc_dev, &got), 0, "get after rollover wait");
	/* 应进入 2027-01-01 附近 */
	zassert_true(got.tm_year == 127 || (got.tm_year == 126 && got.tm_mon == 11),
		     "unexpected after rollover y=%d mon=%d", got.tm_year, got.tm_mon);
	ZT_INFO("f_day_31_and_rollover_sample", "y=%d mon=%d d=%d",
		got.tm_year + 1900, got.tm_mon + 1, got.tm_mday);
}
