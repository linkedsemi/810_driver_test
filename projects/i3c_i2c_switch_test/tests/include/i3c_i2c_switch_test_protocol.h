/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I3C_I2C_SWITCH_TEST_PROTOCOL_H
#define I3C_I2C_SWITCH_TEST_PROTOCOL_H

#include <zephyr/drivers/pinctrl.h>

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
 * ztest 原生化说明：token 打印宏已随旧框架删除，本头只保留总线协议事实
 * （opcode/地址/时序/常量/测试 id）。i3c_i2c_test_names[] 保留为
 * id <-> 测试名对照表——target 侧 dispatcher 需据此把 'B'/'E' 携带的
 * 总线 id 解析为 ztest_unit_test 段的链接下标（GDB 内存兜底记账按链接序，
 * 见 cpu1_i3c_target/src/i3c_i2c_target_test.c tgt_link_index），
 * 不是 token 机制。脚本侧 TEST_NAMES 各自硬编码并按 nm 链接序对齐（§10.27），
 * 不再解析本文件。
 */
#define CMD_TGT_BEGIN        'B'	/* 0x42：后随 1 字节 test id，target 开始记账 */
#define CMD_TGT_END          'E'	/* 0x45：后随 1 字节 test id，target 结账记账 */
#define CMD_SWITCH_TO_I2C    'S'	/* 0x53：I3C 帧下发，target 延时切换 I2C pinmux */
#define CMD_SWITCH_TO_I3C    'Y'	/* 0x59：I2C 帧下发，target 延时切回 I3C pinmux */
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
};

/* 兼容旧 token 宏（ztest 迁移后不再输出 token，保留空宏避免编译错误） */
#define I3C_I2C_TOKEN_PREFIX "I3C_I2C_TEST"
#define I3C_I2C_START_CORE_VAR(core, name)       do {} while (0)
#define I3C_I2C_PASS_CORE_VAR(core, name)        do {} while (0)
#define I3C_I2C_FAIL_CORE_VAR(core, name, reason) do {} while (0)
#define I3C_I2C_INFO_CORE(core, name, kv)        do {} while (0)
#define I3C_I2C_BOOT_CORE(core)                  do {} while (0)
#define I3C_I2C_DONE_CORE_VAR(core, result)      do {} while (0)
#define I3C_I2C_SUMMARY_BEGIN(core)              do {} while (0)
#define I3C_I2C_SUMMARY_ITEM(core, name, result) do {} while (0)
#define I3C_I2C_SUMMARY_ITEM_REASON(core, name, result, reason) do {} while (0)
#define I3C_I2C_SUMMARY_END(core, passed, ran)   do {} while (0)
#define I3C_I2C_CTRL_BOOT()                      do {} while (0)
#define I3C_I2C_CTRL_DONE_VAR(result)            do {} while (0)
#define I3C_I2C_CTRL_START_VAR(name)             do {} while (0)
#define I3C_I2C_CTRL_PASS_VAR(name)              do {} while (0)
#define I3C_I2C_CTRL_FAIL_VAR(name, reason)      do {} while (0)
#define I3C_I2C_TGT_BOOT()                       do {} while (0)
#define I3C_I2C_TGT_DONE_VAR(result)             do {} while (0)
#define I3C_I2C_TGT_START_VAR(name)              do {} while (0)
#define I3C_I2C_TGT_PASS_VAR(name)               do {} while (0)
#define I3C_I2C_TGT_FAIL_VAR(name, reason)       do {} while (0)
#define I3C_I2C_TGT_START(name)                  do {} while (0)
#define I3C_I2C_TGT_PASS(name)                   do {} while (0)
#define I3C_I2C_TGT_FAIL(name, reason)           do {} while (0)

#ifdef __cplusplus
}
#endif

#endif /* I3C_I2C_SWITCH_TEST_PROTOCOL_H */
