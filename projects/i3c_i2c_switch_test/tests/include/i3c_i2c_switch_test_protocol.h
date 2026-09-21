/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I3C_I2C_SWITCH_TEST_PROTOCOL_H
#define I3C_I2C_SWITCH_TEST_PROTOCOL_H

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/printk.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inter-core command protocol for the I3C/I2C module-switch test.
 *
 * 被测对象：同一硬件模块（I3C5/I3C6）在 I3C 与 I2C 两种模式间切换，
 * 模式由引脚复用（pinctrl）区分：
 *   - "default"     状态 = I3C 引脚复用（i3c5_scl_pj10 / i3c5_sda_pj11 等）
 *   - "pinmux_i2c"  状态 = I2C 引脚复用（I2C5_scl_PJ10 / I2C5_sda_PJ11 等）
 * 切换 = pinctrl_apply_state()（CONFIG_PINCTRL_DYNAMIC）。
 *
 * 双核总线拓扑（与 i3c_dualcore_test 相同的物理接线）：
 *   cpu2 (controller 侧): I3C5 控制器 + i2c5 I2C 主机（同一模块两个驱动视图）
 *   cpu1 (target 侧):     I3C6 目标 + i2c6 I2C 从机（同一模块两个驱动视图）
 *   I3C 模式：i3c5 <-> i3c6（DAA + 私有读写，target 地址 0x32）
 *   I2C 模式：i2c5 <-> i2c6（master <-> slave，slave 地址 0x58）
 *
 * 命令下发：I3C 模式下经 i3c_transfer 私有写（target 0x32），
 *           I2C 模式下经 i2c_transfer 写（slave 0x58），首字节均为 opcode。
 * opcode 用可打印 ASCII：逻辑分析仪上能直接认出命令含义。
 *
 * ztest 原生化说明：本工程**不使用 ztest 注册**（固件是"命令通道 + 双端记账"
 * 的顺序流程，不是离散 test 函数），逐项证据只由下面的 token 宏打印。token 格式
 * 的形状是**契约**：`common/ztest_report.py::parse_ztest` 直接消费它（本工程之外
 * 没有第二处产出 `I3C_I2C_TEST:` 前缀的行），`tests/run_test.py::evaluate_case`
 * 的 `src=token` 就来自这里。
 *
 * 为什么 token 宏必须真的打印（而不是 no-op）：批量路径**必须靠 token** —— mem
 * 兜底（GDB 读 test_results[]）只是调试期的应急通道，批量里若一个串口逐项 token
 * 都没捕到，PASS 就只剩"上一轮 RAM 里的数字"作依据（common/judge.py::MEM_ONLY_NOTE
 * 会把这种 case 判 FAIL）。故本头保留所有 token 宏为真实 printk。
 *
 * i3c_i2c_test_names[] = id <-> 测试名对照表：target 侧 dispatcher 需据此把
 * 'B'/'E' 携带的总线 id 解析为 ztest_unit_test 段的链接下标（GDB 内存兜底记账按
 * 链接序，见 cpu1_i3c_target/src/i3c_i2c_target_test.c tgt_link_index）。
 * 脚本侧 TEST_NAMES 各自硬编码并按**枚举序**对齐（§10.27；本工程无 ztest 链接序）。
 */
#define CMD_TGT_BEGIN        'B'	/* 0x42：后随 1 字节 test id，target 开始记账 */
#define CMD_TGT_END          'E'	/* 0x45：后随 1 字节 test id，target 结账记账 */
#define CMD_SWITCH_TO_I2C    'S'	/* 0x53：I3C 帧下发，target 延时切换 I2C pinmux */
#define CMD_SWITCH_TO_I3C    'Y'	/* 0x59：I2C 帧下发，target 延时切回 I3C pinmux */
#define CMD_NACK_WINDOW      'N'	/* 0x4E：I3C 帧下发，target 临时切 I2C（不响应广播），
					 * 延时 NACK_WINDOW_MS 后自动切回 I3C */
#define CMD_WRITE_PATTERN    'W'	/* 0x57：写 pattern（target 校验内容） */
#define CMD_PRINT_SUMMARY    'P'	/* 0x50：target 判定收尾（旧框架：打印 SUMMARY 后结束） */

/* I2C 模式对端地址（i2c6 slave 注册地址） */
#define I2C_SWITCH_SLAVE_ADDR 0x58U
/* i2c6 从机注册地址（切换测试与 master 侧 I2C_SWITCH_SLAVE_ADDR 一致） */
#define I2C_TGT_SLAVE_ADDR I2C_SWITCH_SLAVE_ADDR

/* I3C 模式 target 地址（DAA 分配，与 i3c_dualcore_test 一致） */
#define I3C_SWITCH_TARGET_ADDR 0x32U

/* 切换时序（pinmux 切换 us 级，留足余量避免总线竞态）：
 * target 收到切换命令后延时 TGT_SWITCH_DELAY_MS 再切；
 * controller 发完命令后延时 CTRL_SWITCH_DELAY_MS 再切（必须 > TGT）。 */
#define TGT_SWITCH_DELAY_MS  5
#define CTRL_SWITCH_DELAY_MS 8
/* NACK 窗口时长（target 收到 'N' 后切 I2C 保持的时长，controller 须在此
 * 窗口内完成一次广播 NACK，窗口结束后 target 自动切回 I3C 应答） */
#define NACK_WINDOW_MS       2

/* pinctrl "pinmux_i2c" 状态的 ID：私有状态从 PINCTRL_STATE_PRIV_START 起
 * （pinctrl-names 第 2 个状态），固件据此验证 IO 区分机制的配置。 */
#define PINCTRL_STATE_PINMUX_I2C PINCTRL_STATE_PRIV_START

/* 读预填 pattern 基地址（target 读到 0xAA + i） */
#define TGT_READ_PATTERN_BASE 0xAAU

/* 循环切换轮数（switch_stress） */
#define SWITCH_STRESS_ROUNDS 5

/*
 * Test IDs shared by controller and target（'B'/'E' 命令的总线载荷）。
 * The order must match i3c_i2c_test_names[] below.
 */
enum i3c_i2c_test_id {
	TEST_PINCTRL_STATES = 0,	/* pinctrl 状态 ID 与初始模式验证（IO 区分机制静态验证） */
	TEST_I3C_BASELINE,		/* I3C 模式：DAA + 16B write_read */
	TEST_SWITCH_I3C_TO_I2C,		/* 双端切换 I2C + 状态字探测 */
	TEST_I2C_RW_1,			/* I2C 模式 1B 写读 */
	TEST_I2C_RW_16,			/* I2C 模式 16B 写读 */
	TEST_I2C_RW_17,			/* I2C 模式 17B 写读（跨 FIFO 深度边界） */
	TEST_I2C_RW_100,		/* I2C 模式 100B 写读 */
	TEST_SWITCH_I2C_TO_I3C,		/* 双端切回 I3C + 重新 DAA + 复验 */
	TEST_I2C_NACK_BAD_ADDR,		/* I2C 负路径：访问不存在的地址 */
	TEST_SWITCH_STRESS,		/* I3C<->I2C 循环切换 N 轮，每轮双模式基本读写 */
	TEST_BROADCAST_NACK_RECOVERY,	/* 广播 RSTACT NACK 后 CCC 恢复（驱动修复验证） */
	I3C_I2C_TEST_COUNT,
};

/* id <-> 测试名对照表：'B'/'E' 总线 id 到 ztest 测试名/链接下标的运行时
 * 解析依据（非 token 机制，见文件头说明）。名字必须与 ZTEST(suite, name)
 * 注册名一致。 */
static const char * const i3c_i2c_test_names[I3C_I2C_TEST_COUNT] = {
	[TEST_PINCTRL_STATES] = "pinctrl_states",
	[TEST_I3C_BASELINE] = "i3c_baseline",
	[TEST_SWITCH_I3C_TO_I2C] = "switch_i3c_to_i2c",
	[TEST_I2C_RW_1] = "i2c_rw_1",
	[TEST_I2C_RW_16] = "i2c_rw_16",
	[TEST_I2C_RW_17] = "i2c_rw_17",
	[TEST_I2C_RW_100] = "i2c_rw_100",
	[TEST_SWITCH_I2C_TO_I3C] = "switch_i2c_to_i3c",
	[TEST_I2C_NACK_BAD_ADDR] = "i2c_nack_bad_addr",
	[TEST_SWITCH_STRESS] = "switch_stress",
	[TEST_BROADCAST_NACK_RECOVERY] = "broadcast_nack_recovery",
};

/* ============ UART 逐项 token（批量判定的**唯一**逐项证据源） ============
 *
 * 形状（`<core>` = CTRL | TGT，`<name>` = i3c_i2c_test_names[] 里的名字）：
 *   I3C_I2C_TEST:<core>:START:<name>
 *   I3C_I2C_TEST:<core>:PASS:<name>
 *   I3C_I2C_TEST:<core>:FAIL:<name>:<reason>
 *   I3C_I2C_TEST:<core>:INFO:<name>:k=v,k=v,...
 *   I3C_I2C_TEST:<core>:BOOT  /  :DONE:<result>
 *   I3C_I2C_TEST:<core>:SUMMARY:ITEM:<name>:<PASS|FAIL|NOT_RUN>[:<reason>]
 *
 * 消费者 = `common/ztest_report.py::parse_ztest`（只取 `PASS/FAIL/SKIP` 与 `INFO`
 * 两类；SUMMARY 块是给人看的收尾账，同时供 run_test.py 的 prime 轮"任何固件
 * 输出"判据）。打印点全部在**线程上下文**（CTRL = test_main；TGT = dispatcher），
 * ISR 只记账塞队列（CLAUDE.md §4.2 ISR 禁打日志）。 */
#define I3C_I2C_TOKEN_PREFIX "I3C_I2C_TEST"

/* ============ 单出口（SPEC_REFERENCE §6.9.3）：从核 token 交主核打印 ============
 *
 * 本工程两核都是"命令通道型"（不注册 ztest），符合 §6.9.3 的适用面 ⇒ **从核不往
 * 自己的串口打 token**，改把整行经 mbox 交给主核，由主核**原样**打印（单出口）。
 * 主核侧不做重定向，token 照旧本地打印 —— 于是两核 token 汇成一路串口流。
 *
 * 从核的 LOG / panic / assert 文本**不走这里**（它们不是 rlog_emit 的调用点）
 * ⇒ 从核崩溃现场仍留在本地串口上，不因单出口丢失。
 *
 * `I3C_I2C_SINGLE_EGRESS` 只由**从核**的 CMakeLists 定义（也只有从核编入 rlog.c）；
 * 主核不定义 ⇒ 同一份头在两核各自落到正确的输出通道。 */
#if defined(I3C_I2C_SINGLE_EGRESS)
#include "rlog.h"
#define I3C_I2C_OUT(...) rlog_emitf(__VA_ARGS__)
#else
#define I3C_I2C_OUT(...) printk(__VA_ARGS__)
#endif

#define I3C_I2C_START_CORE_VAR(core, name) \
	I3C_I2C_OUT(I3C_I2C_TOKEN_PREFIX ":" #core ":START:%s\n", name)
#define I3C_I2C_PASS_CORE_VAR(core, name) \
	I3C_I2C_OUT(I3C_I2C_TOKEN_PREFIX ":" #core ":PASS:%s\n", name)
#define I3C_I2C_FAIL_CORE_VAR(core, name, reason) \
	I3C_I2C_OUT(I3C_I2C_TOKEN_PREFIX ":" #core ":FAIL:%s:%s\n", name, reason)
#define I3C_I2C_INFO_CORE(core, name, kv) \
	I3C_I2C_OUT(I3C_I2C_TOKEN_PREFIX ":" #core ":INFO:%s:" kv "\n", name)
#define I3C_I2C_BOOT_CORE(core) \
	I3C_I2C_OUT(I3C_I2C_TOKEN_PREFIX ":" #core ":BOOT\n")
#define I3C_I2C_DONE_CORE_VAR(core, result) \
	I3C_I2C_OUT(I3C_I2C_TOKEN_PREFIX ":" #core ":DONE:%s\n", result)
#define I3C_I2C_SUMMARY_BEGIN(core) \
	I3C_I2C_OUT("\n" I3C_I2C_TOKEN_PREFIX ":" #core ":SUMMARY:BEGIN\n")
#define I3C_I2C_SUMMARY_ITEM(core, name, result) \
	I3C_I2C_OUT(I3C_I2C_TOKEN_PREFIX ":" #core ":SUMMARY:ITEM:%s:%s\n", name, result)
#define I3C_I2C_SUMMARY_ITEM_REASON(core, name, result, reason) \
	I3C_I2C_OUT(I3C_I2C_TOKEN_PREFIX ":" #core ":SUMMARY:ITEM:%s:%s:%s\n", \
		    name, result, reason)
#define I3C_I2C_SUMMARY_END(core, passed, ran) \
	I3C_I2C_OUT(I3C_I2C_TOKEN_PREFIX ":" #core ":SUMMARY:END:%d/%d PASS\n\n", \
		    passed, ran)

/* core 绑定别名 —— 调用点只使用这些 */
#define I3C_I2C_CTRL_BOOT()                 I3C_I2C_BOOT_CORE(CTRL)
#define I3C_I2C_TGT_BOOT()                  I3C_I2C_BOOT_CORE(TGT)
#define I3C_I2C_CTRL_DONE_VAR(result)       I3C_I2C_DONE_CORE_VAR(CTRL, result)
#define I3C_I2C_TGT_DONE_VAR(result)        I3C_I2C_DONE_CORE_VAR(TGT, result)
#define I3C_I2C_CTRL_START_VAR(name)        I3C_I2C_START_CORE_VAR(CTRL, name)
#define I3C_I2C_CTRL_PASS_VAR(name)         I3C_I2C_PASS_CORE_VAR(CTRL, name)
#define I3C_I2C_CTRL_FAIL_VAR(name, reason) I3C_I2C_FAIL_CORE_VAR(CTRL, name, reason)
#define I3C_I2C_TGT_START_VAR(name)         I3C_I2C_START_CORE_VAR(TGT, name)
#define I3C_I2C_TGT_PASS_VAR(name)          I3C_I2C_PASS_CORE_VAR(TGT, name)
#define I3C_I2C_TGT_FAIL_VAR(name, reason)  I3C_I2C_FAIL_CORE_VAR(TGT, name, reason)
#define I3C_I2C_TGT_START(name)             I3C_I2C_START_CORE_VAR(TGT, name)
#define I3C_I2C_TGT_PASS(name)              I3C_I2C_PASS_CORE_VAR(TGT, name)
#define I3C_I2C_TGT_FAIL(name, reason)      I3C_I2C_FAIL_CORE_VAR(TGT, name, reason)

#ifdef __cplusplus
}
#endif

#endif /* I3C_I2C_SWITCH_TEST_PROTOCOL_H */
