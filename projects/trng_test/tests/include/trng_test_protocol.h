/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * TRNG 单核测试协议头（810_driver_test 公共规范）。
 *
 * 单核工程：token 角色固定 TGT（无对侧核）。所有 token 用 printk 输出
 * （规避 LOG 前缀，parser 只扫 `<PREFIX>:`）。
 *
 * Token 格式（CLAUDE.md §6 + §13.4/§13.5）：
 *   TRNG_TEST:TGT:START:<name>
 *   TRNG_TEST:TGT:INFO:<name>:<key>=<value>[,<key>=<value>...]   # PASS/FAIL 之前
 *   TRNG_TEST:TGT:PASS:<name>
 *   TRNG_TEST:TGT:FAIL:<name>:<断言位置>:<期望>=X,实际=Y[; ...]  # reason 可归因
 *   TRNG_TEST:TGT:SUMMARY:BEGIN
 *   TRNG_TEST:TGT:SUMMARY:ITEM:<name>:<PASS|FAIL|NOT_RUN>[:reason]
 *   TRNG_TEST:TGT:SUMMARY:END:<passed>/<ran> <PASS|FAIL>
 *   TRNG_TEST:DONE:<PASS|FAIL>
 */

#ifndef TRNG_TEST_PROTOCOL_H
#define TRNG_TEST_PROTOCOL_H

#include <stdio.h>

#define TRNG_TEST_TOKEN_PREFIX "TRNG_TEST"

/* 测试项枚举（索引即 test_results[] 槽位，顺序必须与 trng_test_names 一致） */
enum trng_test_id {
	TRNG_TEST_BASIC_16 = 0,
	TRNG_TEST_NOT_ALL_ZERO,
	TRNG_TEST_NOT_ALL_FF,
	TRNG_TEST_ISR_BUSYWAIT,
	TRNG_TEST_LARGE_1024,
	TRNG_TEST_INVALID_ARGS,
	TRNG_TEST_MONOBIT_8K,
	TRNG_TEST_DISTINCT_32,
	TRNG_TEST_THROUGHPUT_64K,
	TRNG_TEST_COUNT
};

/* 名字即 token 里的 test_name（与 tests/scripts/dump_results.py 的 TEST_NAMES 一致） */
static const char *const trng_test_names[TRNG_TEST_COUNT] = {
	[TRNG_TEST_BASIC_16] = "basic_16",
	[TRNG_TEST_NOT_ALL_ZERO] = "not_all_zero",
	[TRNG_TEST_NOT_ALL_FF] = "not_all_ff",
	[TRNG_TEST_ISR_BUSYWAIT] = "isr_busywait",
	[TRNG_TEST_LARGE_1024] = "large_1024",
	[TRNG_TEST_INVALID_ARGS] = "invalid_args",
	[TRNG_TEST_MONOBIT_8K] = "monobit_8k",
	[TRNG_TEST_DISTINCT_32] = "distinct_32",
	[TRNG_TEST_THROUGHPUT_64K] = "throughput_64k",
};

/* ---- token 宏（全部 printk，TGT 角色） ---- */

#define TRNG_TEST_TGT_START_VAR(name) \
	printk(TRNG_TEST_TOKEN_PREFIX ":TGT:START:%s\n", (name))

/* §13.4 实测数值上报：<key>=<value>[,<key>=<value>...]，须在 PASS/FAIL 之前。
 * kv 是格式串字面量（只拼接一次，不重复入参），数值参数走 ...：
 *   printk(prefix ":TGT:INFO:<name>:" kv "\n", name, args...) */
#define TRNG_TEST_TGT_INFO_VAR(name, kv, ...) \
	printk(TRNG_TEST_TOKEN_PREFIX ":TGT:INFO:%s:" kv "\n", (name), ##__VA_ARGS__)

#define TRNG_TEST_TGT_PASS_VAR(name) \
	printk(TRNG_TEST_TOKEN_PREFIX ":TGT:PASS:%s\n", (name))

/* §13.5 reason 可归因：<断言位置>:<期望>=X,实际=Y[; ...] */
#define TRNG_TEST_TGT_FAIL_VAR(name, reason) \
	printk(TRNG_TEST_TOKEN_PREFIX ":TGT:FAIL:%s:%s\n", (name), (reason))

#define TRNG_TEST_SUMMARY_BEGIN \
	printk(TRNG_TEST_TOKEN_PREFIX ":TGT:SUMMARY:BEGIN\n")

#define TRNG_TEST_SUMMARY_ITEM(name, st) \
	printk(TRNG_TEST_TOKEN_PREFIX ":TGT:SUMMARY:ITEM:%s:%s\n", (name), (st))

#define TRNG_TEST_SUMMARY_ITEM_REASON(name, st, reason) \
	printk(TRNG_TEST_TOKEN_PREFIX ":TGT:SUMMARY:ITEM:%s:%s:%s\n", (name), (st), (reason))

#define TRNG_TEST_SUMMARY_END(passed, ran, ok) \
	printk(TRNG_TEST_TOKEN_PREFIX ":TGT:SUMMARY:END:%d/%d %s\n", \
	       (int)(passed), (int)(ran), (ok) ? "PASS" : "FAIL")

#define TRNG_TEST_DONE_VAR(result) \
	printk(TRNG_TEST_TOKEN_PREFIX ":DONE:%s\n", (result))

#endif /* TRNG_TEST_PROTOCOL_H */
