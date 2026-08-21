# i3c_dualcore_test 测试覆盖矩阵（CLAUDE.md §13）

被测驱动：`zephyr/drivers/i3c/i3c_linkedsemi.c`（controller，cpu2）+ `i3c_target_linkedsemi.c`（target，cpu1）
- **外部连接路径**（CLAUDE.md §14）：I3C5_SCL(PJ10)↔I3C6_SCL(PJ08)，
  I3C5_SDA(PJ11)↔I3C6_SDA(PJ09)，共地；两侧均配置上拉。
  跑测试前需按 §14.2 向用户报告本接线状态。
- 状态机：`ls_i3c_curr_msg_*`（808-971 行）+ `ls_i3c_event_isr_*`（TX/RX/CF，831-992 行）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| daa_discovery | ls_i3c_do_daa (1052)：GETACCCR/ENTDAA/地址分配 | 功能通路 | 发现成功，target 分配到动态地址 | 协议文档 | ✅ |
| write_8 | ls_i3c_transfer (1514) 写方向 + curr_msg 状态机 | 功能通路 | 8B 写入无 NACK（SERRWARN 无） | 协议文档 | ✅ |
| write_100 | ls_i3c_transfer 长写（>FIFO 缓冲） | 边界长度 | 100B 全部写入无 NACK | 协议文档 | ✅ |
| read_32_direct | ls_i3c_transfer 读方向 + event_isr_rx (861) | 功能通路 | 读回 32B == 预期 pattern | 协议文档 | ✅ |
| write_read_1 | ls_i3c_transfer 写+读组合（1B 最小粒度） | 功能通路 | 读回 1B == 写入值 | 协议文档 | ✅ |
| write_read_16 | 写+读组合（16B 对齐） | 边界长度 | 读回 16B == 写入值 | 协议文档 | ✅ |
| write_read_17 | 写+读组合（17B 非字对齐） | 边界长度 | 读回 17B == 写入值 | 协议文档 | ✅ |
| write_read_32 | 写+读组合（32B = FIFO 阈值附近） | 边界长度 | 读回 32B == 写入值 | 协议文档 | ✅ |
| ibi | ls_i3c_ibi_enable (2155) + target ls_i3c_target_ibi_raise (2256) + event_isr | 中断/时序 | IBI 事件触发且数据 == pattern | 协议文档 | ✅ |
| hot_join | ls_i3c_do_daa 中 HJ 分支 + event_isr_cf (992) | 中断/时序 | HJ 被识别并处理，无 URUN | 协议文档 | ✅ |
| ccc_basic | ls_i3c_do_ccc (1207)：GETSTATUS/ID 等 CCC | 功能通路 | CCC 完成、响应状态 == 期望值 | 协议文档 | ✅ |
| write_stop_read_1 | ls_i3c_transfer 写+stop+读（stop 条件分支） | 边界长度 | 停后读回 1B == 写入值 | 协议文档 | ✅ |
| write_stop_read_16 | 写+stop+读（16B） | 边界长度 | 停后读回 16B == 写入值 | 协议文档 | ✅ |
| write_stop_read_17 | 写+stop+读（17B 非对齐） | 边界长度 | 停后读回 17B == 写入值 | 协议文档 | ✅ |
| write_stop_read_32 | 写+stop+读（32B） | 边界长度 | 停后读回 32B == 写入值 | 协议文档 | ✅ |

## 覆盖说明

- **外部**：本工程所有测试项都依赖 I3C5↔I3C6 跨核外部接线（§14），
  创建/执行前需向用户确认接线环境；接线记录见顶层 README.md「外设接线记录」。
- 预期数据（列 4）由固件写/读同一 pattern + 状态字校验（target 回读比对），
  INFO token 上报关键数值（如 read 实际长度），报告"实际"列取用（§13.4）。
- 已知坑：target 只校验 `last_read_len` 而 controller 实际读 0 → 假 PASS
  （write_read_×4 场景），判定按 §6 双核双 PASS 规则兜底。
