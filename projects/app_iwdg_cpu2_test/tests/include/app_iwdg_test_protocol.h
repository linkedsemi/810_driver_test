/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * app_iwdg_cpu2_test 共享常量（cpu1 / cpu2 两侧固件 + 执行脚本共用）
 *
 * 本工程无跨核命令协议：cpu1 只是 2os 引导核，cpu2 是被测核，
 * 两核之间不传消息（app_iwdg 复位原因由 cpu2 侧本地读取）。
 * 因此本头文件只承载两类共享语义：
 *   1. case id（与 cpu2_controller_app_iwdg_test/Kconfig 的
 *      APP_IWDG_TEST_CASE_ID 一一对应，由 tests/case_xxx/controller.conf 设置）
 *   2. case_reset 状态机在串口上打出的判定标记（run_reset.sh 按此判定）
 */
#ifndef APP_IWDG_TEST_PROTOCOL_H
#define APP_IWDG_TEST_PROTOCOL_H

/* ---- case id（= CONFIG_APP_IWDG_TEST_CASE_ID） ---- */
#define APP_IWDG_TEST_CASE_NON_DESTRUCTIVE 0 /* ztest：API 通路/异常/边界 */
#define APP_IWDG_TEST_CASE_QUICK_ENABLE 1    /* ztest：iwdt_init quick 正常分支 */
#define APP_IWDG_TEST_CASE_QUICK_INVALID 2   /* ztest：quick-timeout-ms=0 -> -EINVAL */
#define APP_IWDG_TEST_CASE_RESET 3           /* 自定义 main：HART 复位 + 原因回读 */

/* ---- case_reset 判定标记（uart5） ---- */
#define APP_IWDG_RESET_PASS_MARK "APP_IWDG_RESET_PASS"
#define APP_IWDG_RESET_FAIL_MARK "APP_IWDG_RESET_FAIL"

/*
 * 预期复位原因 = APP_IWDT_HART_RESET（enum reset_reason，zephyr/soc/linkedsemi/
 * lsqsh/soc_reset.h）。产生位置 = zephyr/soc/linkedsemi/lsqsh/soc_reset_reason.c
 * 的 SYSC_APP_PER 分支（`SYSC_APP_PER_RST_FROM_IWDT2_MASK & reset_src`），
 * 该分支只在 cpu1 节点非 okay 的 cpu2 构建里编译——故 cpu1 侧读不到。
 * 固件内直接用枚举符号断言，本文件不重复其数值（避免与枚举漂移）。
 */

#endif /* APP_IWDG_TEST_PROTOCOL_H */

/* 单出口（SPEC_REFERENCE §6.9.3/§6.9.4）：cpu1 的 token 经 RLOG 交 cpu2 打印，
 * cpu2 转发时加 `<前缀>:<核>:` 核信封 ⇒ 采集侧（acquire.split_relay_lines）按
 * 同一前缀分拣回 serial_TGT.log。须与 common/ztest_report.py 的
 * NATIVE_PREFIXES 登记一致。 */
#define APP_IWDG_TEST_PREFIX "APP_IWDG_TEST"
