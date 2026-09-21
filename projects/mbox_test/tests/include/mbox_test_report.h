/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * mbox_test 结果上报 / 判定模块（见 src/mbox_test_report.c 文件头）。
 *
 * 上报格式（§6.9.1 D2 同构 token，`common/ztest_report.py` 的登记前缀白名单
 * 已含 `MBOX_TEST`）：
 *     MBOX_TEST:<core>:(PASS|FAIL):<name>[:<明细>]      行为项（判定走结果 token）
 *     MBOX_TEST:<core>:INFO:<name>:pass=<0|1>,observe=1[,<k=v>...]  观察项
 *     MBOX_TEST:<core>:INFO:<name>:pass=1,guard=1[,<k=v>...]        结构性守卫
 * 结尾判定：PROJECT EXECUTION SUCCESSFUL | FAILED（每核恰一次）。
 *
 * 为什么改：旧形态 `INFO <name>: pass=<0|1>` 把**判定**塞进 INFO（数值上报）
 * 类别，违反 §6.9.1「判定必须走结果 token，不得挤在 INFO 上」。改后 INFO 只留
 * 纯 k=v（观察项 / 守卫的附注），判定语义由 (core, name, state, reason) 承载。
 */
#ifndef MBOX_TEST_REPORT_H
#define MBOX_TEST_REPORT_H

#include <stdbool.h>

/*
 * 槽位索引：**固定序**，供串口不可用时按符号读 test_results[]（§12.3 兜底）。
 * 两核共用同一份索引表，各自只上报自己负责的槽位；未上报的槽位保持
 * TR_NOT_RUN（由 mbox_report_init 置位）。增删项只能**追加**，不要插队。
 */
enum mbox_item {
	/* case_mbox_basic（MBOX_TEST_CASE_ID = 0） */
	MBOX_I_BASIC_CH4 = 0,
	MBOX_I_BASIC_CH5,
	MBOX_I_BASIC_MTU,
	/* case_mbox_big_crc（=1） */
	MBOX_I_BIG_PAYLOAD_CRC,
	MBOX_I_BIG_CRC_CHUNK_ACK,
	/* case_mbox_stress（=2） */
	MBOX_I_STRESS_N_MESSAGES,
	MBOX_I_STRESS_RTT_STATS,
	/* case_mbox_consumers（=3） */
	MBOX_I_CONSUMER_CH1,
	MBOX_I_CONSUMER_CH2,
	MBOX_I_CONSUMER_CH3,
	MBOX_I_CONSUMER_CH4,
	MBOX_I_CONSUMER_BIDIR,
	MBOX_I_CONSUMER_CH5_SHORT,
	/* case_mbox_invalid（=4） */
	MBOX_I_INV_SEND_NULL_MSG,
	MBOX_I_INV_SEND_BAD_CHANNEL,
	MBOX_I_INV_SEND_OVERSIZE_CH4,
	MBOX_I_INV_SEND_OVERSIZE_CH2,
	MBOX_I_INV_SEND_NULL_DATA,
	MBOX_I_INV_REGISTER_BAD_CHANNEL,
	MBOX_I_INV_SET_ENABLED_BAD_CHANNEL,
	MBOX_I_INV_ZERO_SIZE,        /* 观察项 */
	MBOX_I_INV_MTU_GET,          /* 观察项 */
	/* case_mbox_fifo_full（=6） */
	MBOX_I_FIFO_FULL_ENOSPC,
	MBOX_I_FIFO_FULL_DRAIN,
	/* case_mbox_rtc_sync（=5） */
	MBOX_I_RTC_READ_ADVANCES,
	MBOX_I_RTC_SYNC_DELTA,
	MBOX_I_RTC_SYNC_MONOTONIC,
	/* 结构性守卫（判据全在对端的 case） */
	MBOX_I_STRUCT_GUARD,
	MBOX_I_COUNT
};

/* test_main 起始调用：置全部槽位 NOT_RUN + 清本核判负状态。
 * core = "TGT"（cpu1 target）/ "CTRL"（cpu2 controller），进 D2 token 的 core 段 */
void mbox_report_init(const char *core);

/* 行为项通过。fmt 为附加明细（可为 NULL），不要自带前导逗号 */
void mbox_item_pass(enum mbox_item id, const char *name, const char *fmt, ...);

/* 行为项失败：上报 + 本核判负（末尾打 PROJECT EXECUTION FAILED） */
void mbox_item_fail(enum mbox_item id, const char *name, const char *fmt, ...);

/* 观察项：ok = 现状是否与"已登记的驱动现状"一致。**不**判负 */
void mbox_item_observe(enum mbox_item id, const char *name, bool ok,
		       const char *fmt, ...);

/* 结构性守卫：本核对该 case 无行为判据，只声明"本项非行为证据" */
void mbox_item_guard(enum mbox_item id, const char *name, const char *fmt, ...);

/* 非项级失败（本核整体判负，不产生测试项） */
void mbox_mark_fail(const char *reason);

/* 本核是否已判负 */
bool mbox_failed(void);

/* 结尾单行判定（每核恰一次）：SUCCESSFUL / FAILED */
void mbox_print_verdict(void);

#endif /* MBOX_TEST_REPORT_H */
