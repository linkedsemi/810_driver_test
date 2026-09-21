/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef I2C_PURE_TEST_PROTOCOL_H
#define I2C_PURE_TEST_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 纯 I2C 波形质量测试（i2c_pure_test）。
 *
 * 目的：观察 400K I2C 总线波形质量（示波器抓 PJ10/PJ11 或 PJ08/PJ09）。
 * 无 I3C 参与：i2c5（cpu2 master）<-> i2c6（cpu1 slave）纯 I2C 直连，
 * 引脚复用由 i2c5/i2c6 节点自身的 pinctrl-0（default = I2C 引脚，
 * board 级 lsqsh_evb_pinctrl.dtsi 宏 DEFINE_PINCTRL_CONSUMER_I2C 生成）
 * 在驱动 init 时自动应用。
 *
 * 双核拓扑（与 i3c_dualcore_test / i3c_i2c_switch_test 相同的物理接线）：
 *   cpu2 (master): i2c5 @400K，I2C5_scl_PJ10 / I2C5_sda_PJ11
 *   cpu1 (slave):  i2c6 @400K，I2C6_scl_PJ08 / I2C6_sda_PJ09，从机地址 0x58
 *
 * i2c_wave：100 轮 × 16B 写+读循环，轮间 100ms。
 * 单轮 ~0.3ms 总线突发（400K 下 33B ≈ 264 bit ≈ 0.66ms，含帧开销），
 * 轮间静默便于示波器单次触发抓拍；总时长 ~10s。
 *
 * 本头只保留跨核 I2C 命令协议常量（opcode/地址/pattern/参数）；
 * 结果判定与上报由 ztest 原生框架 + common/ztest_glue 完成
 * （两核固件各自 ZTEST 套件，token 宏已随 ztest 原生化移除）。
 */
#define I2C_PURE_SLAVE_ADDR 0x58U	/* 从机地址（用户指定，区别于切换测试的 0x50） */

#define CMD_WRITE_PATTERN    'W'	/* 0x57：后随 <len> <data...>，slave 校验递增序列 0,1,2,... */
#define CMD_PRINT_SUMMARY    'P'	/* 0x50：slave 判定后打印并结束套件 */

/* 读返回 pattern 基地址（slave 读到 0xAA + i） */
#define TGT_READ_PATTERN_BASE 0xAAU

/* i2c_wave 参数（master 侧驱动，slave 被动） */
#define I2C_WAVE_ROUNDS       100
#define I2C_WAVE_ROUND_DELAY_MS 100
#define I2C_WAVE_LEN          16U

/* 单出口（SPEC_REFERENCE §6.9.3/§6.9.4）：slave 的 token 经 RLOG 交 master
 * 打印，master 在转发时加 `<前缀>:<核>:` 信封 ⇒ 采集侧（acquire.split_relay_lines）
 * 按同一前缀把它们分拣回 `serial_TGT.log`。
 * 该前缀须与 common/ztest_report.py 的 NATIVE_PREFIXES 登记一致（登记表是
 * "本仓自有前缀"的唯一清单）。 */
#define I2C_PURE_TEST_PREFIX "I2C_PURE_TEST"

#ifdef __cplusplus
}
#endif

#endif /* I2C_PURE_TEST_PROTOCOL_H */
