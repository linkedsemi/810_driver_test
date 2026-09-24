# i3c_dualcore_test 测试报告

- 工程: i3c_dualcore_test
- case: case_pure_i3c_6m25
- 日期: 2026-09-23 23:18:10
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）
- 执行标记: CTRL=SUCCESSFUL, TGT=None
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (35/35)

## case_pure_i3c_6m25 (PASS)

> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| api_neg_paths | ✅ PASS | token | 全部 -EINVAL，事后正常读 OK | - |  |
| back_to_back_8x8 | ✅ PASS | token | 8 次全部成功、数据正确 | - |  |
| big_256 | ✅ PASS | token | 256B 写入+读回 == pattern | - |  |
| ccc_basic | ✅ PASS | token | CCC 完成、响应状态 == 期望值 | - |  |
| ccc_rapid_8 | ✅ PASS | token | getpid x8 全部成功、PID 非零 | - |  |
| daa_discovery | ✅ PASS | token | 发现成功，target 分配到动态地址 | - |  |
| hot_join | ✅ PASS | token | HJ 被识别并处理，无 URUN | - |  |
| ibi | ✅ PASS | token | IBI 事件触发且数据 == pattern | - |  |
| ibi_neg_paths | ✅ PASS | token | 三个返回值精确匹配 | - |  |
| multi_msg_4 | ✅ PASS | token | 各段 num_xfer 正确、两段读数据 == pattern | - |  |
| multi_msg_short_read | ✅ PASS | token | 前三段 num_xfer 正确、rd1 pattern 对；恢复读 8B pattern 对 | - |  |
| nack_bad_addr | ✅ PASS | token | 不挂死；读不得伪造数据（actual==0 或报错）；事后正常读 OK | - |  |
| read_32_direct | ✅ PASS | token | 读回 32B == 预期 pattern | - |  |
| read_master_abort | ✅ PASS | token | 读 32 取 8 后，下一笔读从 pattern 起点对齐 | - |  |
| read_underrun_16_32 | ✅ PASS | token | 读 32 实际收 16，不挂死，恢复读 OK | - |  |
| read_underrun_8_16 | ✅ PASS | token | 读 16 实际收 8，不挂死，恢复读 OK | - |  |
| read_underrun_stress | ✅ PASS | token | 8 轮 actual==8 且 pattern 对；恢复读 ret==0/actual==16 | - |  |
| setaasa | ✅ PASS | token | RSTDAA 后 GETPID@0x32 必须 NACK（旧地址已清）；SETAASA 后 GETPID@0x5A 成功… | - |  |
| setdasa | ✅ PASS | token | RSTDAA 后 GETPID@0x32 必须 NACK；SETDASA@0x5A 分配 0x45：GETPID@0x4… | - |  |
| short_read_then_nack | ✅ PASS | token | 每轮短读 actual==8、恢复读 ret==0/actual==8/pattern 对 | - |  |
| write_100 | ✅ PASS | token | 100B 全部写入无 NACK | - |  |
| write_8 | ✅ PASS | token | 8B 写入无 NACK（SERRWARN 无） | - |  |
| write_delay_read_16 | ✅ PASS | token | 25M 源窗口 6000-7100ns 漂移（±9%，5 轮）；NACK 边界 = 窗口+~0.1µs（stop 122… | CTRL:critical_delay_ns=7600, first_short_step=130, master_stop_step=-1, nack_recover_ok=skip(no-nack) |  |
| write_delay_read_len | ✅ PASS | token | 临界档：len32≈115-124、len64≈107-123（互差 ≤3 档常态，偶发 6-7 档）；len17（1… | CTRL:critical17_step=255, critical32_step=255, critical_ns=8050, first_short_step=148, len=64, stop_step=-1 |  |
| write_indexed_read_16 | ✅ PASS | token | 3 索引（0x00/0x10/0x20）读回 tgt_tx_buf[idx..idx+15] 全部匹配 pattern | - |  |
| write_read_15 | ✅ PASS | token | 读回 15B == 写入值；target 侧校验写段占位字节 0xA5 | - |  |
| write_read_16 | ✅ PASS | token | 读回 16B == 写入值；target 侧校验写段占位字节 0xA5 | - |  |
| write_read_17 | ✅ PASS | token | 读回 17B == 写入值 | - |  |
| write_read_1 | ✅ PASS | token | 读回 1B == 写入值；target 侧另校验写段占位字节 0xA5 原样到达 | - |  |
| write_read_32 | ✅ PASS | token | 读回 32B == 写入值 | - |  |
| write_stop_read_15 | ✅ PASS | token | 停后读回 15B == 写入值；target 侧校验写段占位字节 0xA5 | - |  |
| write_stop_read_16 | ✅ PASS | token | 停后读回 16B == 写入值；target 侧校验写段占位字节 0xA5 | - |  |
| write_stop_read_17 | ✅ PASS | token | 停后读回 17B == 写入值 | - |  |
| write_stop_read_1 | ✅ PASS | token | 停后读回 1B == 写入值；target 侧校验写段占位字节 0xA5 | - |  |
| write_stop_read_32 | ✅ PASS | token | 停后读回 32B == 写入值 | - |  |

## 汇总

OVERALL: PASS
