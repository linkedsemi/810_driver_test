/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 测试结果记账模块（810_driver_test 公共规范，所有驱动测试工程必须使用）。
 *
 * 用途
 * ----
 * 每个固件（controller / target）把每项测试的结果记到公共全局数组：
 *     int8_t  test_results[TR_MAX_COUNT]      -1=未跑  0=PASS  1=FAIL
 *     char    test_reasons[TR_MAX_COUNT][TR_REASON_LEN]  失败原因
 * 双通道取结果：
 *   1. 正常路径：结尾 SUMMARY token 走串口（I3C_TEST:CTRL:SUMMARY:ITEM:...）
 *   2. 兜底路径：串口不可用/乱码时，用 GDB 直接读内存（符号固定可查）：
 *        riscv64-unknown-elf-gdb -q -batch \
 *          -ex "target remote 127.0.0.1:1025" -ex "interrupt" \
 *          -ex "x/<count>db &test_results" \
 *          -ex "x/s &test_reasons[<i>]" -ex "detach" -ex "quit"
 *      或直接跑 tests/scripts/dump_results.py（自动解析双核并打印对照表）。
 *   ★ 兜底路径必须先读 test_results_valid 哨兵：未置位（或旧 ELF 无此符号）
 *     = 固件没跑到 tr_init → 槽位是 .bss 零值（零值语义是 TR_PASS）→
 *     必须报"结果无效"，禁止输出全 PASS。
 *
 * 为什么定长数组（TR_MAX_COUNT=64）而不是按枚举定长：
 *   定长让公共 .c 文件与具体工程零耦合——新工程只需 include 本头文件 +
 *   链接本 .c，不用给数组定尺寸；符号名 test_results 全工程统一，dump 脚本
 *   无需改。槽位只用到 tr_init(count) 传入的 count 个。
 *
 * 接入方法（新工程照抄）：
 *   1. CMakeLists.txt（find_package 之后）：
 *        target_sources(app PRIVATE
 *            ${CMAKE_CURRENT_SOURCE_DIR}/../../common/test_results.c)
 *        target_include_directories(app PRIVATE
 *            ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
 *   2. 固件源码 include "test_results.h"，测试前 tr_init(I3C_TEST_COUNT)，
 *      每项测试结束 tr_record(id, PASS/FAIL, "reason fmt", ...)。
 *   3. 结尾汇总打印读 test_results[] / test_reasons[]（槽位内容），
 *      打印逻辑保持工程自己的 SUMMARY token 格式。
 */
#ifndef TEST_RESULTS_H
#define TEST_RESULTS_H

#include <stdarg.h>
#include <stdint.h>

#define TR_MAX_COUNT   64   /* 单固件最多测试项数（公共定长，见文件头注释） */
#define TR_REASON_LEN  96   /* 每个失败原因槽位字节数 */

/* 结果值约定（-1/0/1 与 SUMMARY 的 NOT_RUN/PASS/FAIL 一一对应） */
#define TR_NOT_RUN (-1)
#define TR_PASS     0
#define TR_FAIL     1

/* 有效性哨兵（2026-09-18 加，堵"内存兜底假 PASS"）：
 * test_results[] 在 .bss，C 运行时会把它清零 —— 而零值是 TR_PASS。
 * 若固件**没跑到 tr_init()**（挂在内核初始化 / POST_KERNEL / 早期崩溃），
 * 槽位就停在零值，dump_results.py 只按 <0/=0/>0 判读就会把"固件根本没跑"
 * 读成"全 PASS" —— 恰好在最需要看到真相（固件静默）时报告最像好消息。
 * 故：tr_init() 在**填完槽位之后**把 test_results_valid 置 TR_VALID_MAGIC，
 * dump 脚本先查哨兵 —— 符号不存在或值不等 ⇒ 明确报"结果无效"。
 * 用魔数（非 0 非 1）而非布尔量，避免与 .bss 零值和相邻槽位混淆。 */
#define TR_VALID_MAGIC 0x5A   /* 哨兵期望值；tr_init() 未跑到则保持 .bss 的 0 */
extern int8_t test_results_valid;

/* 全局符号（.bss，链接地址=加载地址，GDB 直接读）——双核各自链接，
 * 各自 elf 里都叫这个名字，dump_results.py 按 elf 区分核。 */
extern int8_t test_results[TR_MAX_COUNT];
extern char test_reasons[TR_MAX_COUNT][TR_REASON_LEN];

/**
 * @brief 初始化 count 个测试槽位为 TR_NOT_RUN / 空原因。
 *        必须在第一个测试开始前调用一次（每个核自己调自己的）。
 */
void tr_init(uint8_t count);

/**
 * @brief 记账一项测试结果：test_results[idx] = result，
 *        失败时把原因写入 test_reasons[idx]（变参 fmt，自动截断）。
 *        可在任意上下文调用（ISR 里也可以，只写内存不拿锁）。
 */
void tr_record(int idx, int result, const char *fmt, ...);

#endif /* TEST_RESULTS_H */
