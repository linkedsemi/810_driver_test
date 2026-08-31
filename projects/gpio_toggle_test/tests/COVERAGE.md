# gpio_toggle_test 测试覆盖矩阵

**被测对象**：GPIO 基础输出功能 + 系统时钟链准确性。PJ10/PJ11（I3C5 的
SCL/SDA 引脚）以 1s 周期翻转（k_msleep(500) 半周期），示波器实测周期与
软件设定对比，用于排查 I2C 400K 实测 333K 的时钟源疑点（PBUS4=75MHz 配置
是否与实际相符）。

**判定方式**：示波器人工判定（固件不自验，结果不入 test_results；
按设计无限翻转，无 DONE token）。

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| io_toggle_1s | HAL io_cfg_output/io_toggle_pin（ls_soc_gpio.c）+ k_msleep 系统节拍 | 功能通路 + 时序 | 方波周期 1s（500ms 高 + 500ms 低），示波器实测周期 = 1s ±1% | API 契约（k_msleep）+ 时钟配置（INFO 上报 ticks_per_sec） | 待示波器验证 |

> 外部路径（§14）：PJ10/PJ11 即 I3C5/I2C5 总线焊点，示波器探头直接挂总线处。
> INFO token 上报 `pins=PJ10_PJ11,period_ms=1000,ticks_per_sec=<N>`。
> 若实测周期偏离 1s：实际时钟 ≈ 配置时钟 × (1s / 实测周期)。
