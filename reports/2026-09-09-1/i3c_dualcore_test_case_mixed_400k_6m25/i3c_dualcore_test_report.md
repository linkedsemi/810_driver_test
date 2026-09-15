# i3c_dualcore_test 测试报告
- 日期: 2026-09-09 13:10  板卡: lsqsh_evb@2os  脚本: run_test.py
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，CLAUDE.md §14）
- case 数: 1  总测试项: 35  通过: 35  失败: 0

## case_mixed_400k_6m25 (PASS)
| 测试项 | 预期 | 实际(INFO) | 结果 | 来源 | reason |
|--------|------|-----------|------|------|--------|
| api_neg_paths | 全部 -EINVAL，事后正常读 OK | - | PASS | token |  |
| back_to_back_8x8 | 8 次全部成功、数据正确 | - | PASS | token |  |
| big_256 | 256B 写入+读回 == pattern | - | PASS | token |  |
| ccc_basic | CCC 完成、响应状态 == 期望值 | - | PASS | token |  |
| ccc_rapid_8 | getpid x8 全部成功、PID 非零 | - | PASS | token |  |
| daa_discovery | 发现成功，target 分配到动态地址 | - | PASS | token |  |
| hot_join | HJ 被识别并处理，无 URUN | - | PASS | token |  |
| ibi | IBI 事件触发且数据 == pattern | - | PASS | token |  |
| ibi_neg_paths | 三个返回值精确匹配 | - | PASS | token |  |
| multi_msg_4 | 各段 num_xfer 正确、两段读数据 == pattern | - | PASS | token |  |
| multi_msg_short_read | - | - | PASS | token |  |
| nack_bad_addr | 不挂死；读不得伪造数据（actual==0 或报错）；事后正常读 OK | - | PASS | token |  |
| read_32_direct | 读回 32B == 预期 pattern | - | PASS | token |  |
| read_master_abort | 读 32 取 8 后，下一笔读从 pattern 起点对齐 | - | PASS | token |  |
| read_underrun_16_32 | 读 32 实际收 16，不挂死，恢复读 OK | - | PASS | token |  |
| read_underrun_8_16 | 读 16 实际收 8，不挂死，恢复读 OK | - | PASS | token |  |
| read_underrun_stress | - | - | PASS | token |  |
| setaasa | RSTDAA 后 GETPID@0x32 必须 NACK（旧地址已清）；SETAASA 后 GETPID@0x5A 成功且 PID 匹配、探针写可达、SDYNADDR DAVALID=1/DADDR=0x5A/DCAUSE=2；dyn=0/static=0x5A 临时 desc 读必须 -EINVAL 且 desc 不被改写（无静默 fallback，记账根因修复见 i3c_common.c backport 373771c062）；SETNEWDA 恢复 0x32 后 GETPID 正常 | - | PASS | token |  |
| setdasa | RSTDAA 后 GETPID@0x32 必须 NACK；SETDASA@0x5A 分配 0x45：GETPID@0x45 成功且 PID 匹配、探针写可达、SDYNADDR DADDR=0x45/DCAUSE=2、SETNEWDA 恢复 0x32 | - | PASS | token |  |
| short_read_then_nack | - | - | PASS | token |  |
| write_100 | 100B 全部写入无 NACK | - | PASS | token |  |
| write_8 | 8B 写入无 NACK（SERRWARN 无） | - | PASS | token |  |
| write_delay_read_16 | 25M 源窗口 6000-7100ns 漂移（±9%，5 轮）；NACK 边界 = 窗口+~0.1µs（stop 122-144 档）；**常态化门限**：case controller.conf 配 MIN/MAX_CRITICAL_NS（25M case [5500,8000]）+ NACK 边界硬判定（stop>critical），超限 FAIL（Kconfig: I3C_TEST_WDR_*）；nack_recover_ok=1（实测 PASS） | CTRL:critical_delay_ns=7650, first_short_step=154, master_stop_step=158, nack_recover_ok=1 | PASS | token |  |
| write_delay_read_len | 临界档：len32≈115-124、len64≈107-123（互差 ≤3 档常态，偶发 6-7 档）；len17（1 字节续发）波动 120-146 档（ISR 抖动放大，仅 INFO 上报不判定）；critical 存在 + 数据 pattern 对 | CTRL:critical_ns=7650, first_short_step=154, len=64, stop_step=164 | PASS | token |  |
| write_indexed_read_16 | 3 索引（0x00/0x10/0x20）读回 tgt_tx_buf[idx..idx+15] 全部匹配 pattern | - | PASS | token |  |
| write_read_15 | - | - | PASS | token |  |
| write_read_16 | 读回 16B == 写入值 | - | PASS | token |  |
| write_read_17 | 读回 17B == 写入值 | - | PASS | token |  |
| write_read_1 | 读回 1B == 写入值 | - | PASS | token |  |
| write_read_32 | 读回 32B == 写入值 | - | PASS | token |  |
| write_stop_read_15 | - | - | PASS | token |  |
| write_stop_read_16 | 停后读回 16B == 写入值 | - | PASS | token |  |
| write_stop_read_17 | 停后读回 17B == 写入值 | - | PASS | token |  |
| write_stop_read_1 | 停后读回 1B == 写入值 | - | PASS | token |  |
| write_stop_read_32 | 停后读回 32B == 写入值 | - | PASS | token |  |

## 汇总

OVERALL: PASS
