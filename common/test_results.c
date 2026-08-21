/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 测试结果记账模块实现。约定见 test_results.h 文件头。
 */
#include "test_results.h"

#include <stdio.h>
#include <string.h>

/* 全局符号必须是 .bss 里的真实内存：链接地址 = 加载地址（RAM/XIP 执行），
 * GDB 连上后按符号名直接读（p/x 命令），串口不可用时兜底取结果。 */
int8_t test_results[TR_MAX_COUNT];
char test_reasons[TR_MAX_COUNT][TR_REASON_LEN];

void tr_init(uint8_t count)
{
	if (count > TR_MAX_COUNT) {
		count = TR_MAX_COUNT;
	}
	for (int i = 0; i < count; i++) {
		test_results[i] = TR_NOT_RUN;
		test_reasons[i][0] = '\0';
	}
}

void tr_record(int idx, int result, const char *fmt, ...)
{
	if (idx < 0 || idx >= TR_MAX_COUNT) {
		return;
	}
	test_results[idx] = (int8_t)result;

	if (fmt != NULL) {
		va_list ap;
		va_start(ap, fmt);
		(void)vsnprintf(test_reasons[idx], TR_REASON_LEN, fmt, ap);
		va_end(ap);
	} else {
		test_reasons[idx][0] = '\0';
	}
}
