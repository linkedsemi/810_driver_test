# i2c_pure_test 测试覆盖矩阵

**被测对象**：纯 I2C 链路（无 I3C 参与）400K 波形质量与基本写读通路。
i2c5（cpu2 master，PJ10/PJ11）↔ i2c6（cpu1 slave，PJ08/PJ09，地址 0x58），
引脚复用由 i2c5/i2c6 节点 pinctrl-0（board 宏默认生成）在 init 时自动应用。
驱动路径参照 `zephyr/drivers/i2c/i2c_ls.c`（i2c_ls_transfer 主机写/读主循环、
i2c 从机逐字节回调路径）。

**预期数据约定**（协议头 `tests/include/i2c_pure_test_protocol.h` 为单一事实源）：
- 写 pattern：`W <len> <0x00,0x01,...,0x0F>`（递增序列，slave 逐字节校验）
- 读 pattern：`TGT_READ_PATTERN_BASE(0xAA) + i`（slave 预填 `tgt_tx_buf[256]`）
- 收尾：master 发 `'P'`，slave 打印 SUMMARY + DONE

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| i2c_wave | i2c_ls_transfer 主机写（i2c_write 18B：'W'+len+16B）+ 主机读（i2c_read 16B）主循环；slave 侧 write_requested/write_received/read_requested/read_processed/stop 逐字节回调 | 功能通路 + 波形观察 | 100 轮 ×（写 16B 0x00..0x0F + 读 16B 0xAA..0xB9）全部匹配；轮间 100ms 静默（示波器单次触发抓拍）；master INFO 上报 rounds=100,delay_ms=100,rate_khz=400；slave INFO 上报 writes/bytes/reads 计数 | 协议约定（pattern 定义，协议头） | 待验证 |

> 维度对照 §13.1：功能通路（主机写读主循环 + 从机逐字节回调全覆盖）。
> 外部路径（§14）：I2C5↔I2C6 跨核对连，接线见顶层 README「外设接线记录」。
> 波形质量（占空比/上升沿/振铃）由示波器人工判定，固件只保证总线持续有
> 400K 突发波形可供抓拍。
