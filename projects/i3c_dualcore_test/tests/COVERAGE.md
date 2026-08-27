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
| write_indexed_read_16 | ISR 顺序重构（MATCHED 先于 RXPEND）：write_received_cb 改 tx_offset/tx_len 后 restart 读按新参数预填（target_prefill_tx 偏移路径） | 功能通路/中断时序 | 3 索引（0x00/0x10/0x20）读回 tgt_tx_buf[idx..idx+15] 全部匹配 pattern | 驱动契约 | ✅ |
| write_delay_read_16 | 从机"写段 data → 读段欠载"可用准备窗口实测：target 收写段后 spin 延时再 FLUSHTB+prefill（TGT_PARSE_INDEXED_READ_DELAY 分支），controller 扫 0-255 档（50ns 步进）找临界 + NACK 边界（欠载后 master Address NACK err=-5 停止档）+ **nack_recovery**（NACK 后立即恢复写读：master 状态机恢复 + target FIFO 残留清理 + tx_offset 复位，pattern 0x00..0x0f 校验） | 中断/时序 | 25M 源窗口 6000-7100ns 漂移（±9%，5 轮）；NACK 边界 = 窗口+~0.1µs（stop 122-144 档）；**常态化门限**：case controller.conf 配 MIN/MAX_CRITICAL_NS（25M case [5500,8000]）+ NACK 边界硬判定（stop>critical），超限 FAIL（Kconfig: I3C_TEST_WDR_*）；nack_recover_ok=1（实测 PASS） | 实测（2026-08-27，I3C SCL 12.5MHz；CPU 实际 547MHz） | ✅ |
| write_delay_read_len | 写段内容决定读长度的 FIFO 深度临界测试：target 收 len 字节（写段最后字节）后 spin→FLUSHTB→prefill 16B，读段按 len 返回（FIFO 深 16，>16 由 TXSEND read_processed_cb 续发）；controller 对 len∈{17,32,64} 各扫 delay 档找临界。**判定：pattern 前缀完整性 bnd≥len（吸收 master 读段 DCNT 固定导致的 actual 放大假象）；判定② len64↔len32 互比（8 档容差——len64 3 次续发的 ISR 抖动累积使临界档偶发提前 ≤7 档（=350ns），8 档吸收；续发路径真退化会缩小几十档不被掩盖）** | 边界长度/中断时序 | 临界档：len32≈115-124、len64≈107-123（互差 ≤3 档常态，偶发 6-7 档）；len17（1 字节续发）波动 120-146 档（ISR 抖动放大，仅 INFO 上报不判定）；critical 存在 + 数据 pattern 对 | 实测（2026-08-27）+ FIFO 深度 16 契约 | ✅ |
| setaasa | i3c_ccc_do_setaasa_all → ls_i3c_do_ccc 广播分支 + target 硬件 SETAASA 自动处理（SDYNADDR DADDR/DCAUSE=2）+ ls_i3c_transfer dynamic_addr==0 → -EINVAL 入口分支 | 功能通路 | RSTDAA 后 GETPID@0x32 必须 NACK（旧地址已清）；SETAASA 后 GETPID@0x5A 成功且 PID 匹配、探针写可达、SDYNADDR DAVALID=1/DADDR=0x5A/DCAUSE=2；dyn=0/static=0x5A 临时 desc 读必须 -EINVAL 且 desc 不被改写（无静默 fallback，记账根因修复见 i3c_common.c backport 373771c062）；SETNEWDA 恢复 0x32 后 GETPID 正常 | NXP I3C slave programmers model（DYNADDR/DCAUSE 编码）+ I3C spec SETAASA 语义 | ✅ |
| setdasa | ctrl_ccc_direct_write1(I3C_CCC_SETDASA) → ls_i3c_do_ccc direct 写分支 + target 硬件 SETDASA 自动处理 | 功能通路 | RSTDAA 后 GETPID@0x32 必须 NACK；SETDASA@0x5A 分配 0x45：GETPID@0x45 成功且 PID 匹配、探针写可达、SDYNADDR DADDR=0x45/DCAUSE=2、SETNEWDA 恢复 0x32 | 同上（SETDASA 直通到静态地址语义） | ✅ |

## 覆盖说明

- **外部**：本工程所有测试项都依赖 I3C5↔I3C6 跨核外部接线（§14），
  创建/执行前需向用户确认接线环境；接线记录见顶层 README.md「外设接线记录」。
- 预期数据（列 4）由固件写/读同一 pattern + 状态字校验（target 回读比对），
  INFO token 上报关键数值（如 read 实际长度），报告"实际"列取用（§13.4）。
- 已知坑：target 只校验 `last_read_len` 而 controller 实际读 0 → 假 PASS
  （write_read_×4 场景），判定按 §6 双核双 PASS 规则兜底。
