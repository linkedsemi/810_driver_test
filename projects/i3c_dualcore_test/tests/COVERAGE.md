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
| read_underrun_8_16 | ls_i3c_transfer 读 + target T-bit 提前终止（ISR FC 落账路径） | 异常/提前终止 | 读 16 实际收 8，不挂死，恢复读 OK | 协议文档（T-bit 提前终止合法） | ✅ |
| read_underrun_16_32 | 同上，跨 target TX FIFO(16) 边界 | 异常/提前终止 | 读 32 实际收 16，不挂死，恢复读 OK | 协议文档 | ✅ |
| nack_bad_addr | ls_i3c_transfer 地址 NACK（ISR ERR/SER 检测 + 写完整性校验） | 异常参数/恢复 | 不挂死；读不得伪造数据（actual==0 或报错）；事后正常读 OK | 协议文档 | ✅ |
| back_to_back_8x8 | ls_i3c_transfer 零间隙 8 连发 + target 预填/续发 | 中断/时序 | 8 次全部成功、数据正确 | 压力/时序 | ✅ |
| big_256 | ls_i3c_transfer 256B 读写 + target read_processed_cb 续发 | 边界长度 | 256B 写入+读回 == pattern | 协议文档 | ✅ |
| ccc_rapid_8 | ls_i3c_do_ccc 连发（num_xfer 清零回归） | 压力/时序 | getpid x8 全部成功、PID 非零 | 回归（num_xfer 根因） | ✅ |
| read_master_abort | 主机主动截断读 + target TERM 恢复（STOP 后 FLUSHTB 重填对齐） | 异常/提前终止 | 读 32 取 8 后，下一笔读从 pattern 起点对齐 | 协议文档 | ✅ |
| ibi_neg_paths | ls_i3c_ibi_raise fail-fast 三分支：payload>8/-EINVAL、IBIDIS/-EACCES、DAVALID HJ/-EACCES | 异常参数 | 三个返回值精确匹配 | 驱动契约 | ✅ |
| api_neg_paths | ls_i3c_transfer 入口：num_msgs=0/dynamic_addr=0/hdr_mode=1/msgs=NULL → -EINVAL | 异常参数 | 全部 -EINVAL，事后正常读 OK | 驱动契约 | ✅ |
| multi_msg_4 | ls_i3c_transfer 4 段复合帧（W+R+W+R）+ isr_tx 读消息拦截 | 功能通路/时序 | 各段 num_xfer 正确、两段读数据 == pattern | 协议文档 | ✅ |

## 覆盖说明

- **外部**：本工程所有测试项都依赖 I3C5↔I3C6 跨核外部接线（§14），
  创建/执行前需向用户确认接线环境；接线记录见顶层 README.md「外设接线记录」。
- 预期数据（列 4）由固件写/读同一 pattern + 状态字校验（target 回读比对），
  INFO token 上报关键数值（如 read 实际长度），报告"实际"列取用（§13.4）。
- 已知坑：target 只校验 `last_read_len` 而 controller 实际读 0 → 假 PASS
  （write_read_×4 场景），判定按 §6 双核双 PASS 规则兜底。
