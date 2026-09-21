/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * i2cf_test 公共定义（单核 cpu1）。
 * 覆盖矩阵见 tests/COVERAGE.md。
 */

#ifndef I2CF_TEST_H_
#define I2CF_TEST_H_

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <stdint.h>

/*
 * 下游侧从设备**地址扫描**范围。
 *
 * 用户裁定（2026-09-17）：外接 EEPROM 优先级降为最低（"两个 I2C 回环已经够了，
 * EEPROM 不重要"），不再纠缠具体地址；改为**全地址扫描**（本身就是有价值的用例），
 * 且**不因无器件而阻塞任何用例**。
 *
 * 范围取标准 7-bit 地址空间的可分配段 —— 跳过 0x00..0x07（保留/广播）与
 * 0x78..0x7F（10-bit 前缀/保留），避免探测保留地址干扰总线。
 */
#define I2CF_SCAN_ADDR_MIN 0x08
#define I2CF_SCAN_ADDR_MAX 0x77

/* 扫描结果上限（标准段内可能应答者的上限） */
#define I2CF_SCAN_MAX 32

/* 被测实例数（i2cf1..i2cf4，overlay 全使能） */
#define I2CF_INST_NUM 4

/* 每个 i2cf 的通道数（LINKEDSEMI_I2C_F_ADDR_NUM） */
#define I2CF_CHANNEL_NUM 16

/* idx 0..3 → i2cf1..i2cf4；越界返回 NULL */
const struct device *i2cf_dev(int idx);

/* idx 0..3 → i2cfN 寄存器基址；越界返回 0 */
uintptr_t i2cf_reg_base(int idx);

#endif /* I2CF_TEST_H_ */
