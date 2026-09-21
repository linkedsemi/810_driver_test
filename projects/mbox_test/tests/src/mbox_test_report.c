/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * mbox_test 结果上报 + 判定模块（cpu1/cpu2 两核共用同一份源码，见两边
 * CMakeLists.txt）。双核各持一份实例，互不影响。
 *
 * 为什么需要它
 * ------------
 * 修复前：两个入口 src/main.c 在 case 函数返回后**无条件**打印
 * `PROJECT EXECUTION: SUCCESSFUL`，而失败路径只打 LOG_ERR/LOG_INF
 * —— 驱动全坏（send 全失败、对端一条收不到）时两核照样打 SUCCESS，
 * case 被判假 PASS。
 *
 * 本模块把"结论"变成显式状态：
 *   1. 每项检查调用 mbox_item_pass/mbox_item_fail 上报一行可解析的形式（§6.9.1
 *          D2 同构 token）  MBOX_TEST:<core>:(PASS|FAIL):<name>[:<明细>]
 *      同时按固定槽位记账到公共模块 common/test_results.c（CLAUDE.md §4.1），
 *      串口不可用时可由 GDB 按符号读回（§12.3 兜底路径）。观察项 / 结构性守卫
 *      走 INFO 类别（纯 k=v + observe=1 / guard=1），不承载判定。
 *   2. 任一项 fail、或 mbox_mark_fail() 被调用 ⇒ 本核判负。
 *   3. src/main.c 在 case 函数返回后调用 mbox_print_verdict()，
 *      **按状态**打印 SUCCESSFUL 或 FAILED —— 每核恰一次，二选一。
 *      调用点仍在 k_sleep(K_FOREVER) 之前，`test_main` 依旧不返回（§8.1）。
 *
 * 观察项（observe=1）与结构性守卫（guard=1）
 * ------------------------------------------
 *   observe=1：把"驱动当前缺陷"固化成预期值的检查（如 size=0 被接受、
 *     mtu_get 返回 -ENOSYS）。无论 pass=0 还是 1 都**不**判本核失败，
 *     由报告单独呈现；驱动修复后该项会显示为"与记录不符"以提示改判
 *     （COVERAGE.md §3.4 / §4）。
 *   guard=1：判据全在对端的 case 里，本核只待机 —— 声明"本项不是行为
 *     证据"，禁止它冒充对双核行为验证的一半。
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

#include "mbox_test_report.h"
#include "test_results.h"

LOG_MODULE_REGISTER(mbox_report, LOG_LEVEL_INF);

#define MBOX_REPORT_LINE_MAX 160

/* 本核是否已判负（任一项 fail / mark_fail 置位） */
static bool core_failed;

/* 本核的 D2 token core 段（"TGT" / "CTRL"），由 mbox_report_init 设定 */
static const char *report_core = "TGT";

/*
 * 输出一行 D2 结果 token `MBOX_TEST:<core>:(PASS|FAIL):<name>[:<明细>]`；
 * fmt 非空时以 ':' 接在名字之后（D2 的 reason 段；fmt 内不要自带冒号前缀）。
 */
static void report_line(const char *name, int ok, const char *fmt, va_list ap)
{
	char buf[MBOX_REPORT_LINE_MAX];
	int n = snprintf(buf, sizeof(buf), "MBOX_TEST:%s:%s:%s",
			 report_core, ok ? "PASS" : "FAIL", name);

	if (n < 0) {
		return;
	}
	if (n > (int)sizeof(buf) - 1) {
		n = (int)sizeof(buf) - 1;
	}
	if (fmt != NULL && n < (int)sizeof(buf) - 1) {
		int m;

		buf[n++] = ':';
		buf[n] = '\0';
		m = vsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);
		if (m < 0) {
			buf[n] = '\0';
		}
	}
	printk("%s\n", buf);
}

/*
 * 输出一行 D2 INFO token `MBOX_TEST:<core>:INFO:<name>:<fmt>`。
 * INFO 类别是**数值/附注上报**，不承载判定（观察项 / 结构性守卫走这里）。
 */
static void info_line(const char *name, const char *fmt, va_list ap)
{
	char buf[MBOX_REPORT_LINE_MAX];
	int n = snprintf(buf, sizeof(buf), "MBOX_TEST:%s:INFO:%s:",
			 report_core, name);

	if (n < 0 || n > (int)sizeof(buf) - 1) {
		return;                 /* 前缀都放不下就不打半行 */
	}
	if (fmt != NULL && n < (int)sizeof(buf) - 1) {
		int m = vsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);

		if (m < 0) {
			buf[n] = '\0';
		}
	}
	printk("%s\n", buf);
}

/* info_line 的变参包装：调用方本身不是变参函数时用它
 * （va_list 不能直接当值传 —— 直传会把字符串指针当成 va_list 解引用）。 */
static void info_line_v(const char *name, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	info_line(name, fmt, ap);
	va_end(ap);
}

static void slot_record(enum mbox_item id, int result)
{
	if (id >= 0 && id < MBOX_I_COUNT) {
		tr_record((int)id, result, NULL);
	}
}

void mbox_report_init(const char *core)
{
	/* 槽位全量置 NOT_RUN：未上报的项由 GDB 兜底读成 NOT_RUN 而非零值 PASS */
	report_core = (core != NULL && core[0] != '\0') ? core : "TGT";
	tr_init(MBOX_I_COUNT);
	core_failed = false;
}

void mbox_item_pass(enum mbox_item id, const char *name, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	report_line(name, 1, fmt, ap);
	va_end(ap);
	slot_record(id, TR_PASS);
}

void mbox_item_fail(enum mbox_item id, const char *name, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	report_line(name, 0, fmt, ap);
	va_end(ap);
	slot_record(id, TR_FAIL);
	core_failed = true;
	LOG_ERR("%s: FAIL", name);
}

/* 把 "tag[,...]" 压进 buf（tag 由调用方给，如 "pass=1,observe=1" / "pass=1,guard=1"） */
static void build_tagged(char *buf, size_t len, const char *tag,
			 const char *fmt, va_list ap)
{
	int n = snprintf(buf, len, "%s", tag);

	if (fmt != NULL && n >= 0 && n < (int)len - 1) {
		int m;

		buf[n++] = ',';
		buf[n] = '\0';
		m = vsnprintf(buf + n, len - (size_t)n, fmt, ap);
		if (m < 0) {
			buf[n] = '\0';
		}
	}
}

void mbox_item_observe(enum mbox_item id, const char *name, bool ok, const char *fmt, ...)
{
	va_list ap;
	char buf[MBOX_REPORT_LINE_MAX];

	/* 观察项：INFO 类别（不承载判定），pass=<现状是否与记录一致> + observe=1
	 * 供脚本摘出。注意此处**不**碰 core_failed —— 观察项不判负。 */
	va_start(ap, fmt);
	build_tagged(buf, sizeof(buf), ok ? "pass=1,observe=1" : "pass=0,observe=1",
		     fmt, ap);
	va_end(ap);
	info_line_v(name, "%s", buf);
	slot_record(id, ok ? TR_PASS : TR_FAIL);
	if (!ok) {
		LOG_WRN("%s: 与记录不符（观察项，不判负；驱动修复后须改判）", name);
	}
}

void mbox_item_guard(enum mbox_item id, const char *name, const char *fmt, ...)
{
	va_list ap;
	char buf[MBOX_REPORT_LINE_MAX];

	/* 结构性守卫：本核无行为判据。INFO 类别上报 pass=1 + guard=1，脚本据此
	 * 把它与行为项区分呈现，且**不**把它计入"全项 PASS"。 */
	va_start(ap, fmt);
	build_tagged(buf, sizeof(buf), "pass=1,guard=1", fmt, ap);
	va_end(ap);
	info_line_v(name, "%s", buf);
	slot_record(id, TR_PASS);
}

void mbox_mark_fail(const char *reason)
{
	core_failed = true;
	LOG_ERR("判定失败: %s", reason);
}

bool mbox_failed(void)
{
	return core_failed;
}

void mbox_print_verdict(void)
{
	/* 每核恰一次；FAILED 与 SUCCESSFUL 互斥（修复前两个入口无条件打
	 * SUCCESSFUL，是本工程假 PASS 的根因）。
	 * §6.9.1 D1 收尾形态（空格，不是冒号）—— 冒号变体不被 `_EXEC_RE` 匹配，
	 * 会让 harness 的"完成标记"判据失效。 */
	printk("PROJECT EXECUTION %s\n", core_failed ? "FAILED" : "SUCCESSFUL");
}
