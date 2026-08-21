/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RTC 单核测试协议头（810_driver_test 公共规范）。
 *
 * 单核工程：token 角色固定 TGT（无对侧核）。所有 token 用 printk 输出
 * （规避 LOG 前缀，parser 只扫 `<PREFIX>:`）。
 *
 * Token 格式（CLAUDE.md §6 + §13.4/§13.5）：
 *   RTC_TEST:TGT:START:<name>
 *   RTC_TEST:TGT:INFO:<name>:<key>=<value>[,<key>=<value>...]   # PASS/FAIL 之前
 *   RTC_TEST:TGT:PASS:<name>
 *   RTC_TEST:TGT:FAIL:<name>:<断言位置>:<期望>=X,实际=Y[; ...]  # reason 可归因
 *   RTC_TEST:TGT:SUMMARY:BEGIN
 *   RTC_TEST:TGT:SUMMARY:ITEM:<name>:<PASS|FAIL|NOT_RUN>[:reason]
 *   RTC_TEST:TGT:SUMMARY:END:<passed>/<ran> <PASS|FAIL>
 *   RTC_TEST:DONE:<PASS|FAIL>
 */

#ifndef RTC_TEST_PROTOCOL_H
#define RTC_TEST_PROTOCOL_H

#include <stdio.h>

#define RTC_TEST_TOKEN_PREFIX "RTC_TEST"

/* 测试项枚举（索引即 test_results[] 槽位，顺序必须与 rtc_test_names 一致） */
enum rtc_test_id {
	RTC_TEST_SET_GET_ROUNDTRIP = 0,
	RTC_TEST_TIME_ADVANCES,
	RTC_TEST_INVALID_SET_ARGS,
	RTC_TEST_ALARM_SET_GET,
	RTC_TEST_ALARM_FIRES,
	RTC_TEST_ALARM_INVALID_ARGS,
	RTC_TEST_CALIBRATION_ROUNDTRIP,
	RTC_TEST_CALIBRATION_NEGATIVE,
	RTC_TEST_CALIBRATION_NULL_ARG,
	RTC_TEST_COUNT
};

/* 名字即 token 里的 test_name（与 tests/scripts/dump_results.py 的 TEST_NAMES 一致） */
static const char *const rtc_test_names[RTC_TEST_COUNT] = {
	[RTC_TEST_SET_GET_ROUNDTRIP] = "set_get_roundtrip",
	[RTC_TEST_TIME_ADVANCES] = "time_advances",
	[RTC_TEST_INVALID_SET_ARGS] = "invalid_set_args",
	[RTC_TEST_ALARM_SET_GET] = "alarm_set_get",
	[RTC_TEST_ALARM_FIRES] = "alarm_fires",
	[RTC_TEST_ALARM_INVALID_ARGS] = "alarm_invalid_args",
	[RTC_TEST_CALIBRATION_ROUNDTRIP] = "calibration_roundtrip",
	[RTC_TEST_CALIBRATION_NEGATIVE] = "calibration_negative",
	[RTC_TEST_CALIBRATION_NULL_ARG] = "calibration_null_arg",
};

/* ---- token 宏（全部 printk，TGT 角色） ---- */

#define RTC_TEST_TGT_START_VAR(name) \
	printk(RTC_TEST_TOKEN_PREFIX ":TGT:START:%s\n", (name))

/* §13.4 实测数值上报：<key>=<value>[,<key>=<value>...]，须在 PASS/FAIL 之前。
 * kv 是格式串字面量（只拼接一次，不重复入参），数值参数走 ...：
 *   printk(prefix ":TGT:INFO:<name>:" kv "\n", name, args...) */
#define RTC_TEST_TGT_INFO_VAR(name, kv, ...) \
	printk(RTC_TEST_TOKEN_PREFIX ":TGT:INFO:%s:" kv "\n", (name), ##__VA_ARGS__)

#define RTC_TEST_TGT_PASS_VAR(name) \
	printk(RTC_TEST_TOKEN_PREFIX ":TGT:PASS:%s\n", (name))

/* §13.5 reason 可归因：<断言位置>:<期望>=X,实际=Y[; ...] */
#define RTC_TEST_TGT_FAIL_VAR(name, reason) \
	printk(RTC_TEST_TOKEN_PREFIX ":TGT:FAIL:%s:%s\n", (name), (reason))

#define RTC_TEST_SUMMARY_BEGIN \
	printk(RTC_TEST_TOKEN_PREFIX ":TGT:SUMMARY:BEGIN\n")

#define RTC_TEST_SUMMARY_ITEM(name, st) \
	printk(RTC_TEST_TOKEN_PREFIX ":TGT:SUMMARY:ITEM:%s:%s\n", (name), (st))

#define RTC_TEST_SUMMARY_ITEM_REASON(name, st, reason) \
	printk(RTC_TEST_TOKEN_PREFIX ":TGT:SUMMARY:ITEM:%s:%s:%s\n", (name), (st), (reason))

#define RTC_TEST_SUMMARY_END(passed, ran, ok) \
	printk(RTC_TEST_TOKEN_PREFIX ":TGT:SUMMARY:END:%d/%d %s\n", \
	       (int)(passed), (int)(ran), (ok) ? "PASS" : "FAIL")

#define RTC_TEST_DONE_VAR(result) \
	printk(RTC_TEST_TOKEN_PREFIX ":DONE:%s\n", (result))

#endif /* RTC_TEST_PROTOCOL_H */
