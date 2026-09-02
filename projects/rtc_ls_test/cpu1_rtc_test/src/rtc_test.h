/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RTC_TEST_H
#define RTC_TEST_H

/* 被测 RTC 设备（app.overlay 使能 rtc@4006fc00） */
extern const struct device *rtc_dev;

/* 跑完全部 ZTEST 套件 + 汇总 + DONE token（test_main 入口调用） */
void rtc_test_run(void);

#endif /* RTC_TEST_H */
