/*
 * Copyright (c) 2025 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I3C_DUALCORE_TEST_PROTOCOL_H
#define I3C_DUALCORE_TEST_PROTOCOL_H

#include <zephyr/sys/printk.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inter-core command protocol.
 *
 * Controller issues all commands as private writes to target address 0x32.
 * First byte = opcode, remaining bytes = payload.
 *
 * opcode 选用可打印 ASCII：逻辑分析仪上能直接认出命令含义，
 * 且不会与 I3C CCC 命令码（direct ENEC=0x80 / DISEC=0x81 等）混淆。
 */
#define CMD_TEST_START         'S'	/* 0x53 */
#define CMD_TEST_END           'E'	/* 0x45 */
#define CMD_WRITE_DATA         'W'	/* 0x57 */
#define CMD_SET_READ_LEN       'L'	/* 0x4C */
#define CMD_TRIGGER_IBI        'I'	/* 0x49 */
#define CMD_TRIGGER_HJ         'H'	/* 0x48 */
#define CMD_RESET_TARGET_STATE 'R'	/* 0x52 */
#define CMD_PRINT_SUMMARY      'P'	/* 0x50 */
#define CMD_RUN_TGT_NEG        'N'	/* 0x4E：让 target 执行 IBI 负路径自检 */

/* write_read 写阶段的占位字节：不参与解析，仅用于产生 restart 前的写段。
 * 读长度统一由 CMD_SET_READ_LEN 告知 target。 */
#define CMD_DUMMY_BYTE         0xA5

/* Result codes carried by CMD_TEST_END. */
#define TEST_RESULT_PASS  0
#define TEST_RESULT_FAIL  1
#define TEST_RESULT_SKIP  2

/*
 * Target address used by both controller and target.
 */
#define I3C_TARGET_ADDR 0x32U

/*
 * Test IDs shared by controller and target.
 *
 * The order must match i3c_test_names[] below.
 */
enum i3c_test_id {
	I3C_TEST_DAA_DISCOVERY = 0,
	I3C_TEST_WRITE_8,
	I3C_TEST_WRITE_100,
	I3C_TEST_READ_32_DIRECT,
	I3C_TEST_WRITE_READ_1,
	I3C_TEST_WRITE_READ_15,
	I3C_TEST_WRITE_READ_16,
	I3C_TEST_WRITE_READ_17,
	I3C_TEST_WRITE_READ_32,
	I3C_TEST_IBI,
	I3C_TEST_HOT_JOIN,
	I3C_TEST_CCC_BASIC,
	I3C_TEST_WRITE_STOP_READ_1,
	I3C_TEST_WRITE_STOP_READ_15,
	I3C_TEST_WRITE_STOP_READ_16,
	I3C_TEST_WRITE_STOP_READ_17,
	I3C_TEST_WRITE_STOP_READ_32,
	I3C_TEST_READ_UNDERRUN_8_16,
	I3C_TEST_READ_UNDERRUN_16_32,
	I3C_TEST_NACK_BAD_ADDR,
	I3C_TEST_BACK_TO_BACK_8X8,
	I3C_TEST_BIG_256,
	I3C_TEST_CCC_RAPID_8,
	I3C_TEST_READ_MASTER_ABORT,
	I3C_TEST_IBI_NEG_PATHS,
	I3C_TEST_API_NEG_PATHS,
	I3C_TEST_MULTI_MSG_4,
	I3C_TEST_COUNT,
};

static const char * const i3c_test_names[I3C_TEST_COUNT] = {
	[I3C_TEST_DAA_DISCOVERY] = "daa_discovery",
	[I3C_TEST_WRITE_8] = "write_8",
	[I3C_TEST_WRITE_100] = "write_100",
	[I3C_TEST_READ_32_DIRECT] = "read_32_direct",
	[I3C_TEST_WRITE_READ_1] = "write_read_1",
	[I3C_TEST_WRITE_READ_15] = "write_read_15",
	[I3C_TEST_WRITE_READ_16] = "write_read_16",
	[I3C_TEST_WRITE_READ_17] = "write_read_17",
	[I3C_TEST_WRITE_READ_32] = "write_read_32",
	[I3C_TEST_IBI] = "ibi",
	[I3C_TEST_HOT_JOIN] = "hot_join",
	[I3C_TEST_CCC_BASIC] = "ccc_basic",
	[I3C_TEST_WRITE_STOP_READ_1] = "write_stop_read_1",
	[I3C_TEST_WRITE_STOP_READ_15] = "write_stop_read_15",
	[I3C_TEST_WRITE_STOP_READ_16] = "write_stop_read_16",
	[I3C_TEST_WRITE_STOP_READ_17] = "write_stop_read_17",
	[I3C_TEST_WRITE_STOP_READ_32] = "write_stop_read_32",
	[I3C_TEST_READ_UNDERRUN_8_16] = "read_underrun_8_16",
	[I3C_TEST_READ_UNDERRUN_16_32] = "read_underrun_16_32",
	[I3C_TEST_NACK_BAD_ADDR] = "nack_bad_addr",
	[I3C_TEST_BACK_TO_BACK_8X8] = "back_to_back_8x8",
	[I3C_TEST_BIG_256] = "big_256",
	[I3C_TEST_CCC_RAPID_8] = "ccc_rapid_8",
	[I3C_TEST_READ_MASTER_ABORT] = "read_master_abort",
	[I3C_TEST_IBI_NEG_PATHS] = "ibi_neg_paths",
	[I3C_TEST_API_NEG_PATHS] = "api_neg_paths",
	[I3C_TEST_MULTI_MSG_4] = "multi_msg_4",
};

/*
 * 测试分块开关：置 0 可在编译期裁掉对应测试，减少一次性跑出的波形数量，
 * 便于示波器/逻辑分析仪上定位某个测试的波形。
 *
 * 只影响 controller 侧注册的 ZTEST 数量；target 侧是纯命令驱动
 * （收到哪个测试的 CMD_TEST_START/END 就跑哪个校验），无需改动。
 * 修改本头文件后 test.sh 会同时重编双核，两端天然一致。
 *
 * 注意：ccc_basic 末尾的 DISEC(EVT_ALL) 会关闭 target 的 IBI/HJ 事件，
 * 若单独分块跑 ibi/hot_join 而不跑 ccc_basic，行为不受影响；
 * 若一起跑，controller 会在 ibi/hot_join 测试前重新 ENEC。
 */
/*
 * 测试开关：一个测试项一个宏，1 = 注册运行，0 = 直接不跑。
 * 只影响 controller 侧注册的 ZTEST 数量；target 侧是纯命令驱动
 * （收到哪个测试的 CMD_TEST_START/END 就跑哪个校验），无需改动。
 * 修改本头文件后 test.sh 会同时重编双核，两端天然一致。
 *
 * 注意：ccc_basic 末尾的 DISEC(EVT_ALL) 会关闭 target 的 IBI/HJ 事件，
 * 若单独分块跑 ibi/hot_join 而不跑 ccc_basic，行为不受影响；
 * 若一起跑，controller 会在 ibi/hot_join 测试前重新 ENEC。
 */
/* 全量测试开关（2026-08-24：write_read 根因闭环后恢复全开） */
#define I3C_TEST_EN_CCC_BASIC          1
#define I3C_TEST_EN_DAA_DISCOVERY      1
#define I3C_TEST_EN_WRITE_8            1
#define I3C_TEST_EN_WRITE_100          1
#define I3C_TEST_EN_READ_32_DIRECT     1
#define I3C_TEST_EN_WRITE_READ_1       1
#define I3C_TEST_EN_WRITE_READ_15      1
#define I3C_TEST_EN_WRITE_READ_16      1
#define I3C_TEST_EN_WRITE_READ_17      1
#define I3C_TEST_EN_WRITE_READ_32      1
#define I3C_TEST_EN_IBI                1
#define I3C_TEST_EN_HOT_JOIN           1
#define I3C_TEST_EN_WRITE_STOP_READ_1  1
#define I3C_TEST_EN_WRITE_STOP_READ_15 1
#define I3C_TEST_EN_WRITE_STOP_READ_16 1
#define I3C_TEST_EN_WRITE_STOP_READ_17 1
#define I3C_TEST_EN_WRITE_STOP_READ_32 1
#define I3C_TEST_EN_READ_UNDERRUN_8_16  1
#define I3C_TEST_EN_READ_UNDERRUN_16_32 1
#define I3C_TEST_EN_NACK_BAD_ADDR       1
#define I3C_TEST_EN_BACK_TO_BACK_8X8    1
#define I3C_TEST_EN_BIG_256             1
#define I3C_TEST_EN_CCC_RAPID_8         1
#define I3C_TEST_EN_READ_MASTER_ABORT   1
#define I3C_TEST_EN_IBI_NEG_PATHS       1
#define I3C_TEST_EN_API_NEG_PATHS       1
#define I3C_TEST_EN_MULTI_MSG_4         1

/*
 * Structured UART tokens.
 *
 * Both cores emit these machine-readable lines on their respective UARTs.
 * They bypass the logging subsystem so formatting is deterministic.
 */
#define I3C_TEST_TOKEN_PREFIX "I3C_TEST"

/* String-literal variants (compile-time name). */
#define I3C_TEST_START_CORE(core, name) \
	printk(I3C_TEST_TOKEN_PREFIX ":" #core ":START:" name "\n")
#define I3C_TEST_PASS_CORE(core, name) \
	printk(I3C_TEST_TOKEN_PREFIX ":" #core ":PASS:" name "\n")
#define I3C_TEST_FAIL_CORE(core, name, reason) \
	printk(I3C_TEST_TOKEN_PREFIX ":" #core ":FAIL:" name ":" reason "\n")

/* Run-time name variants (name is a const char * variable). */
#define I3C_TEST_START_CORE_VAR(core, name) \
	printk(I3C_TEST_TOKEN_PREFIX ":" #core ":START:%s\n", name)
#define I3C_TEST_PASS_CORE_VAR(core, name) \
	printk(I3C_TEST_TOKEN_PREFIX ":" #core ":PASS:%s\n", name)
#define I3C_TEST_FAIL_CORE_VAR(core, name, reason) \
	printk(I3C_TEST_TOKEN_PREFIX ":" #core ":FAIL:%s:%s\n", name, reason)

#define I3C_TEST_DONE(result) \
	printk(I3C_TEST_TOKEN_PREFIX ":DONE:" result "\n")

#define I3C_TEST_CTRL_START(name) I3C_TEST_START_CORE(CTRL, name)
#define I3C_TEST_CTRL_PASS(name)  I3C_TEST_PASS_CORE(CTRL, name)
#define I3C_TEST_CTRL_FAIL(name, reason) I3C_TEST_FAIL_CORE(CTRL, name, reason)

#define I3C_TEST_CTRL_START_VAR(name) I3C_TEST_START_CORE_VAR(CTRL, name)
#define I3C_TEST_CTRL_PASS_VAR(name)  I3C_TEST_PASS_CORE_VAR(CTRL, name)
#define I3C_TEST_CTRL_FAIL_VAR(name, reason) I3C_TEST_FAIL_CORE_VAR(CTRL, name, reason)

#define I3C_TEST_TGT_START(name) I3C_TEST_START_CORE(TGT, name)
#define I3C_TEST_TGT_PASS(name)  I3C_TEST_PASS_CORE(TGT, name)
#define I3C_TEST_TGT_FAIL(name, reason) I3C_TEST_FAIL_CORE(TGT, name, reason)

#define I3C_TEST_TGT_START_VAR(name) I3C_TEST_START_CORE_VAR(TGT, name)
#define I3C_TEST_TGT_PASS_VAR(name)  I3C_TEST_PASS_CORE_VAR(TGT, name)
#define I3C_TEST_TGT_FAIL_VAR(name, reason) I3C_TEST_FAIL_CORE_VAR(TGT, name, reason)

#define I3C_TEST_DONE_VAR(result) printk(I3C_TEST_TOKEN_PREFIX ":DONE:%s\n", result)

/*
 * Summary tokens.
 *
 * 每个核在全部测试结束后打印一张逐项结果表，方便在冗长的调试 log 里
 * 一眼看清哪些测试过了。格式：
 *   I3C_TEST:CTRL:SUMMARY:BEGIN
 *   I3C_TEST:CTRL:SUMMARY:ITEM:<name>:<PASS|FAIL|NOT_RUN>[:reason]
 *   I3C_TEST:CTRL:SUMMARY:END:<passed>/<ran> PASS
 */
#define I3C_TEST_SUMMARY_BEGIN(core) \
	printk("\n" I3C_TEST_TOKEN_PREFIX ":" #core ":SUMMARY:BEGIN\n")
#define I3C_TEST_SUMMARY_ITEM(core, name, result) \
	printk(I3C_TEST_TOKEN_PREFIX ":" #core ":SUMMARY:ITEM:%s:%s\n", name, result)
#define I3C_TEST_SUMMARY_ITEM_REASON(core, name, result, reason) \
	printk(I3C_TEST_TOKEN_PREFIX ":" #core ":SUMMARY:ITEM:%s:%s:%s\n", \
	       name, result, reason)
#define I3C_TEST_SUMMARY_END(core, passed, ran) \
	printk(I3C_TEST_TOKEN_PREFIX ":" #core ":SUMMARY:END:%d/%d PASS\n\n", \
	       passed, ran)

#ifdef __cplusplus
}
#endif

#endif /* I3C_DUALCORE_TEST_PROTOCOL_H */
