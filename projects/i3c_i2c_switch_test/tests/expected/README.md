# expected/ — 预期数据（黄金向量）

本工程为通信类测试，确定性预期数据收敛在共享协议头
`tests/include/i3c_i2c_switch_test_protocol.h`（单一事实源），固件双核直接
引用，无需独立向量头。此处登记可复现公式：

| 数据 | 生成公式 | 位置 |
|---|---|---|
| 写 pattern（第 i 字节） | `(uint8_t)i`，长度由命令字节决定（1/16/17/100） | 协议头 CMD_WRITE_PATTERN 约定 |
| 读 pattern（第 i 字节） | `(uint8_t)(TGT_READ_PATTERN_BASE + i)`，TGT_READ_PATTERN_BASE=0xAA | 协议头 TGT_READ_PATTERN_BASE |
| I3C target 动态地址 | DAA 分配固定 0x32 | 协议头 I3C_SWITCH_TARGET_ADDR |
| I2C slave 地址 | 固定 0x58（i2c6 注册地址） | 协议头 I2C_SWITCH_SLAVE_ADDR |
| 切换时序 | target 延时 100ms，controller 延时 250ms（controller 必须 > target） | 协议头 TGT/CTRL_SWITCH_DELAY_MS |
