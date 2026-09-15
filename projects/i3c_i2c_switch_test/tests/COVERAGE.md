# i3c_i2c_switch_test 测试覆盖矩阵

**被测对象**：I3C5/I3C6 硬件模块在 I3C 与 I2C 两种模式间的切换，模式由**引脚复用
（pinctrl）**区分（外部接线复用 i3c_dualcore_test 的 I3C5↔I3C6 两根线）。
驱动路径参照 `zephyr/drivers/i3c/i3c_linkedsemi.c`（I3C 控制器）+ `i3c_target_linkedsemi.c`（I3C 目标）、
`zephyr/drivers/i2c/i2c_ls.c`（I2C 主机/从机）与 pinctrl 动态切换
（`pinctrl_apply_state()`，CONFIG_PINCTRL_DYNAMIC）。

**预期数据约定**（协议头 `tests/include/i3c_i2c_switch_test_protocol.h` 为单一事实源）：
- 写 pattern：`W <len> <0x00,0x01,...,0xlen-1>`（递增序列，target 逐字节校验）
- 读 pattern：`TGT_READ_PATTERN_BASE(0xAA) + i`（target 预填 `tgt_tx_buf[256]`）
- 命令通道：I3C 模式经 i3c5 私有写（target 0x32）；I2C 模式经 i2c5 写（slave 0x58）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| pinctrl_states | pinctrl_apply_state 配置表（i3c5/i3c6 节点 states[0]=default、states[1]=pinmux_i2c） | 功能通路 | state_count==2 且 states[1].id==PINCTRL_STATE_PRIV_START | 例程 i3c_i2c_exchange 配置约定（app.overlay pinctrl-names） | ✅ |
| i3c_baseline | i3c_do_daa（DAA 分配 0x32）+ i3c_transfer 私有 WRITE/READ（I3C_MSG_STOP） | 功能通路 | 写 0x00..0x0F 16B；读 0xAA..0xB9 16B | 协议约定（pattern 定义） | ✅ |
| switch_i3c_to_i2c | pinctrl_apply_state(PINMUX_I2C)（i3c5 节点 pinctrl-1 配置 I2C5_scl/sda_PJ10/11） | 中断/时序 | 切换后 i2c5↔i2c6 回连 16B 写读（target 侧 100ms 延时先行） | 例程验证 + 协议时序（CTRL_SWITCH_DELAY>TGT） | ✅ |
| i2c_rw_1 | i2c_ls_transfer / i2c_ls_write_fifo / RXNE 读（单字节路径） | 边界长度 | 写 1B 读 1B（0xAA） | API 契约 | ✅ |
| i2c_rw_16 | i2c_ls 写读 FIFO 主路径（16=FIFO 深度上限） | 边界长度 | 写 0x00..0x0F，读 0xAA..0xB9 | API 契约 | ✅ |
| i2c_rw_17 | i2c_ls 写读跨 FIFO 深度边界（16→17） | 边界长度 | 写 0x00..0x10，读 0xAA..0xBA | API 契约 | ✅ |
| i2c_rw_100 | i2c_ls 大块传输（多段 FIFO 装填循环） | 边界长度 | 写 0x00..0x63，读 0xAA..0x109 模 256 | API 契约 | ✅ |
| i2c_nack_bad_addr | i2c_ls_transfer NACK 分支（MASTER_NACK_RECEIVED → 返回错误） | 异常参数 | 访问 0x70 返回非 0（预期 NACK） | API 契约 | ✅ |
| switch_i2c_to_i3c | pinctrl_apply_state(DEFAULT) + 重新 i3c_do_daa + i3c_transfer 复验 | 中断/时序 | 切回后 DAA 重新发现 target，16B 写读一致 | 例程验证 | ✅ |
| switch_stress | pinctrl_apply_state 双状态循环 ×5 轮（每轮 I3C→I2C→I3C 各一次 16B 写读） | 并发/时序 | 5 轮全部通过（同 id 多轮失败累计） | 协议约定（SWITCH_STRESS_ROUNDS=5） | ✅ |
| broadcast_nack_recovery | i3c_linkedsemi.c ls_i3c_do_ccc 广播帧（RSTACT whole/peripheral、RSTDAA）+ ls_i3c_xfer_reset 残留清理 + EVR BUSY(bit12) 判空 | 异常/恢复 | 'N' 窗口内首帧 RSTACT whole 应 NACK；target 自动切回后 RSTACT peripheral / RSTDAA 应正常；帧末 EVR BUSY=0 | 驱动修复回归（2026-09-09 新增广播 NACK 恢复路径） | ✅ |

> 维度对照 §13.1：功能通路（pinctrl_states/i3c_baseline）、边界长度（i2c_rw_1/16/17/100）、
> 异常参数（i2c_nack_bad_addr）、异常/恢复（broadcast_nack_recovery）、中断/时序（switch 两向 + stress）。
> 外部路径（§14）：I3C5/I3C6 跨核对连，接线见顶层 README「外设接线记录」，
> I2C 模式复用同一组线。切换时序依赖（TGT 100ms < CTRL 250ms）已在协议头固化。
