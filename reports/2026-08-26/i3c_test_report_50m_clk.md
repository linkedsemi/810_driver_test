# i3c_test 测试报告
- 日期: 2026-08-26 23:18  板卡: lsqsh_evb@2os  脚本: run_i3c_tests.py
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，CLAUDE.md §14）
- I3C 时钟源: dpll 50MHz（soc.c CLK_SEL_I3C=0x2 + dts I3C_CLOCK=50M）
- case 数: 7  总测试项: 210  通过: 210  失败: 0

## case_mixed_1m_12m5 (PASS)
| 测试项 | 预期 | 实际(INFO) | 结果 | 来源 | reason |
|--------|------|-----------|------|------|--------|
| daa_discovery | 发现成功，target 分配到动态地址 | - | PASS | token |  |
| write_8 | 8B 写入无 NACK（SERRWARN 无） | - | PASS | token |  |
| write_100 | 100B 全部写入无 NACK | - | PASS | mem |  |
| read_32_direct | 读回 32B == 预期 pattern | - | PASS | token |  |
| write_read_1 | 读回 1B == 写入值 | - | PASS | token |  |
| write_read_15 | - | - | PASS | token |  |
| write_read_16 | 读回 16B == 写入值 | - | PASS | token |  |
| write_read_17 | 读回 17B == 写入值 | - | PASS | token |  |
| write_read_32 | 读回 32B == 写入值 | - | PASS | token |  |
| ibi | IBI 事件触发且数据 == pattern | - | PASS | token |  |
| hot_join | HJ 被识别并处理，无 URUN | - | PASS | token |  |
| ccc_basic | CCC 完成、响应状态 == 期望值 | - | PASS | token |  |
| write_stop_read_1 | 停后读回 1B == 写入值 | - | PASS | token |  |
| write_stop_read_15 | - | - | PASS | token |  |
| write_stop_read_16 | 停后读回 16B == 写入值 | - | PASS | token |  |
| write_stop_read_17 | 停后读回 17B == 写入值 | - | PASS | token |  |
| write_stop_read_32 | 停后读回 32B == 写入值 | - | PASS | token |  |
| read_underrun_8_16 | 读 16 实际收 8，不挂死，恢复读 OK | - | PASS | token |  |
| read_underrun_16_32 | 读 32 实际收 16，不挂死，恢复读 OK | - | PASS | token |  |
| nack_bad_addr | 不挂死；读不得伪造数据（actual==0 或报错）；事后正常读 OK | - | PASS | token |  |
| back_to_back_8x8 | 8 次全部成功、数据正确 | - | PASS | mem |  |
| big_256 | 256B 写入+读回 == pattern | - | PASS | token |  |
| ccc_rapid_8 | getpid x8 全部成功、PID 非零 | - | PASS | token |  |
| read_master_abort | 读 32 取 8 后，下一笔读从 pattern 起点对齐 | - | PASS | token |  |
| ibi_neg_paths | 三个返回值精确匹配 | - | PASS | token |  |
| api_neg_paths | 全部 -EINVAL，事后正常读 OK | - | PASS | token |  |
| multi_msg_4 | 各段 num_xfer 正确、两段读数据 == pattern | - | PASS | token |  |
| write_indexed_read_16 | - | - | PASS | token |  |
| setaasa | RSTDAA 后 GETPID@0x32 必须 NACK（旧地址已清）；SETAASA 后 GETPID@0x5A 成功且 PID 匹配、探针写可达、SDYNADDR DAVALID=1/DADDR=0x5A/DCAUSE=2；dyn=0/static=0x5A 临时 desc 读必须 -EINVAL 且 desc 不被改写（无静默 fallback，记账根因修复见 i3c_common.c backport 373771c062）；SETNEWDA 恢复 0x32 后 GETPID 正常 | - | PASS | token |  |
| setdasa | RSTDAA 后 GETPID@0x32 必须 NACK；SETDASA@0x5A 分配 0x45：GETPID@0x45 成功且 PID 匹配、探针写可达、SDYNADDR DADDR=0x45/DCAUSE=2、SETNEWDA 恢复 0x32 | - | PASS | token |  |

## case_mixed_1m_6m25 (PASS)
| 测试项 | 预期 | 实际(INFO) | 结果 | 来源 | reason |
|--------|------|-----------|------|------|--------|
| daa_discovery | 发现成功，target 分配到动态地址 | - | PASS | token |  |
| write_8 | 8B 写入无 NACK（SERRWARN 无） | - | PASS | token |  |
| write_100 | 100B 全部写入无 NACK | - | PASS | mem |  |
| read_32_direct | 读回 32B == 预期 pattern | - | PASS | token |  |
| write_read_1 | 读回 1B == 写入值 | - | PASS | token |  |
| write_read_15 | - | - | PASS | token |  |
| write_read_16 | 读回 16B == 写入值 | - | PASS | token |  |
| write_read_17 | 读回 17B == 写入值 | - | PASS | token |  |
| write_read_32 | 读回 32B == 写入值 | - | PASS | token |  |
| ibi | IBI 事件触发且数据 == pattern | - | PASS | token |  |
| hot_join | HJ 被识别并处理，无 URUN | - | PASS | token |  |
| ccc_basic | CCC 完成、响应状态 == 期望值 | - | PASS | token |  |
| write_stop_read_1 | 停后读回 1B == 写入值 | - | PASS | token |  |
| write_stop_read_15 | - | - | PASS | token |  |
| write_stop_read_16 | 停后读回 16B == 写入值 | - | PASS | token |  |
| write_stop_read_17 | 停后读回 17B == 写入值 | - | PASS | token |  |
| write_stop_read_32 | 停后读回 32B == 写入值 | - | PASS | token |  |
| read_underrun_8_16 | 读 16 实际收 8，不挂死，恢复读 OK | - | PASS | token |  |
| read_underrun_16_32 | 读 32 实际收 16，不挂死，恢复读 OK | - | PASS | token |  |
| nack_bad_addr | 不挂死；读不得伪造数据（actual==0 或报错）；事后正常读 OK | - | PASS | token |  |
| back_to_back_8x8 | 8 次全部成功、数据正确 | - | PASS | mem |  |
| big_256 | 256B 写入+读回 == pattern | - | PASS | token |  |
| ccc_rapid_8 | getpid x8 全部成功、PID 非零 | - | PASS | token |  |
| read_master_abort | 读 32 取 8 后，下一笔读从 pattern 起点对齐 | - | PASS | token |  |
| ibi_neg_paths | 三个返回值精确匹配 | - | PASS | token |  |
| api_neg_paths | 全部 -EINVAL，事后正常读 OK | - | PASS | token |  |
| multi_msg_4 | 各段 num_xfer 正确、两段读数据 == pattern | - | PASS | token |  |
| write_indexed_read_16 | - | - | PASS | token |  |
| setaasa | RSTDAA 后 GETPID@0x32 必须 NACK（旧地址已清）；SETAASA 后 GETPID@0x5A 成功且 PID 匹配、探针写可达、SDYNADDR DAVALID=1/DADDR=0x5A/DCAUSE=2；dyn=0/static=0x5A 临时 desc 读必须 -EINVAL 且 desc 不被改写（无静默 fallback，记账根因修复见 i3c_common.c backport 373771c062）；SETNEWDA 恢复 0x32 后 GETPID 正常 | - | PASS | token |  |
| setdasa | RSTDAA 后 GETPID@0x32 必须 NACK；SETDASA@0x5A 分配 0x45：GETPID@0x45 成功且 PID 匹配、探针写可达、SDYNADDR DADDR=0x45/DCAUSE=2、SETNEWDA 恢复 0x32 | - | PASS | token |  |

## case_mixed_400k_12m5 (PASS)
| 测试项 | 预期 | 实际(INFO) | 结果 | 来源 | reason |
|--------|------|-----------|------|------|--------|
| daa_discovery | 发现成功，target 分配到动态地址 | - | PASS | token |  |
| write_8 | 8B 写入无 NACK（SERRWARN 无） | - | PASS | token |  |
| write_100 | 100B 全部写入无 NACK | - | PASS | mem |  |
| read_32_direct | 读回 32B == 预期 pattern | - | PASS | token |  |
| write_read_1 | 读回 1B == 写入值 | - | PASS | token |  |
| write_read_15 | - | - | PASS | token |  |
| write_read_16 | 读回 16B == 写入值 | - | PASS | token |  |
| write_read_17 | 读回 17B == 写入值 | - | PASS | token |  |
| write_read_32 | 读回 32B == 写入值 | - | PASS | token |  |
| ibi | IBI 事件触发且数据 == pattern | - | PASS | token |  |
| hot_join | HJ 被识别并处理，无 URUN | - | PASS | token |  |
| ccc_basic | CCC 完成、响应状态 == 期望值 | - | PASS | token |  |
| write_stop_read_1 | 停后读回 1B == 写入值 | - | PASS | token |  |
| write_stop_read_15 | - | - | PASS | token |  |
| write_stop_read_16 | 停后读回 16B == 写入值 | - | PASS | token |  |
| write_stop_read_17 | 停后读回 17B == 写入值 | - | PASS | token |  |
| write_stop_read_32 | 停后读回 32B == 写入值 | - | PASS | token |  |
| read_underrun_8_16 | 读 16 实际收 8，不挂死，恢复读 OK | - | PASS | token |  |
| read_underrun_16_32 | 读 32 实际收 16，不挂死，恢复读 OK | - | PASS | token |  |
| nack_bad_addr | 不挂死；读不得伪造数据（actual==0 或报错）；事后正常读 OK | - | PASS | token |  |
| back_to_back_8x8 | 8 次全部成功、数据正确 | - | PASS | mem |  |
| big_256 | 256B 写入+读回 == pattern | - | PASS | token |  |
| ccc_rapid_8 | getpid x8 全部成功、PID 非零 | - | PASS | token |  |
| read_master_abort | 读 32 取 8 后，下一笔读从 pattern 起点对齐 | - | PASS | token |  |
| ibi_neg_paths | 三个返回值精确匹配 | - | PASS | token |  |
| api_neg_paths | 全部 -EINVAL，事后正常读 OK | - | PASS | token |  |
| multi_msg_4 | 各段 num_xfer 正确、两段读数据 == pattern | - | PASS | token |  |
| write_indexed_read_16 | - | - | PASS | token |  |
| setaasa | RSTDAA 后 GETPID@0x32 必须 NACK（旧地址已清）；SETAASA 后 GETPID@0x5A 成功且 PID 匹配、探针写可达、SDYNADDR DAVALID=1/DADDR=0x5A/DCAUSE=2；dyn=0/static=0x5A 临时 desc 读必须 -EINVAL 且 desc 不被改写（无静默 fallback，记账根因修复见 i3c_common.c backport 373771c062）；SETNEWDA 恢复 0x32 后 GETPID 正常 | - | PASS | token |  |
| setdasa | RSTDAA 后 GETPID@0x32 必须 NACK；SETDASA@0x5A 分配 0x45：GETPID@0x45 成功且 PID 匹配、探针写可达、SDYNADDR DADDR=0x45/DCAUSE=2、SETNEWDA 恢复 0x32 | - | PASS | token |  |

## case_mixed_400k_1m (PASS)
| 测试项 | 预期 | 实际(INFO) | 结果 | 来源 | reason |
|--------|------|-----------|------|------|--------|
| daa_discovery | 发现成功，target 分配到动态地址 | - | PASS | token |  |
| write_8 | 8B 写入无 NACK（SERRWARN 无） | - | PASS | token |  |
| write_100 | 100B 全部写入无 NACK | - | PASS | mem |  |
| read_32_direct | 读回 32B == 预期 pattern | - | PASS | token |  |
| write_read_1 | 读回 1B == 写入值 | - | PASS | token |  |
| write_read_15 | - | - | PASS | token |  |
| write_read_16 | 读回 16B == 写入值 | - | PASS | token |  |
| write_read_17 | 读回 17B == 写入值 | - | PASS | token |  |
| write_read_32 | 读回 32B == 写入值 | - | PASS | token |  |
| ibi | IBI 事件触发且数据 == pattern | - | PASS | token |  |
| hot_join | HJ 被识别并处理，无 URUN | - | PASS | token |  |
| ccc_basic | CCC 完成、响应状态 == 期望值 | - | PASS | token |  |
| write_stop_read_1 | 停后读回 1B == 写入值 | - | PASS | token |  |
| write_stop_read_15 | - | - | PASS | token |  |
| write_stop_read_16 | 停后读回 16B == 写入值 | - | PASS | token |  |
| write_stop_read_17 | 停后读回 17B == 写入值 | - | PASS | token |  |
| write_stop_read_32 | 停后读回 32B == 写入值 | - | PASS | token |  |
| read_underrun_8_16 | 读 16 实际收 8，不挂死，恢复读 OK | - | PASS | token |  |
| read_underrun_16_32 | 读 32 实际收 16，不挂死，恢复读 OK | - | PASS | token |  |
| nack_bad_addr | 不挂死；读不得伪造数据（actual==0 或报错）；事后正常读 OK | - | PASS | token |  |
| back_to_back_8x8 | 8 次全部成功、数据正确 | - | PASS | mem |  |
| big_256 | 256B 写入+读回 == pattern | - | PASS | token |  |
| ccc_rapid_8 | getpid x8 全部成功、PID 非零 | - | PASS | token |  |
| read_master_abort | 读 32 取 8 后，下一笔读从 pattern 起点对齐 | - | PASS | token |  |
| ibi_neg_paths | 三个返回值精确匹配 | - | PASS | token |  |
| api_neg_paths | 全部 -EINVAL，事后正常读 OK | - | PASS | token |  |
| multi_msg_4 | 各段 num_xfer 正确、两段读数据 == pattern | - | PASS | token |  |
| write_indexed_read_16 | - | - | PASS | token |  |
| setaasa | RSTDAA 后 GETPID@0x32 必须 NACK（旧地址已清）；SETAASA 后 GETPID@0x5A 成功且 PID 匹配、探针写可达、SDYNADDR DAVALID=1/DADDR=0x5A/DCAUSE=2；dyn=0/static=0x5A 临时 desc 读必须 -EINVAL 且 desc 不被改写（无静默 fallback，记账根因修复见 i3c_common.c backport 373771c062）；SETNEWDA 恢复 0x32 后 GETPID 正常 | - | PASS | token |  |
| setdasa | RSTDAA 后 GETPID@0x32 必须 NACK；SETDASA@0x5A 分配 0x45：GETPID@0x45 成功且 PID 匹配、探针写可达、SDYNADDR DADDR=0x45/DCAUSE=2、SETNEWDA 恢复 0x32 | - | PASS | token |  |

## case_mixed_400k_6m25 (PASS)
| 测试项 | 预期 | 实际(INFO) | 结果 | 来源 | reason |
|--------|------|-----------|------|------|--------|
| daa_discovery | 发现成功，target 分配到动态地址 | - | PASS | token |  |
| write_8 | 8B 写入无 NACK（SERRWARN 无） | - | PASS | token |  |
| write_100 | 100B 全部写入无 NACK | - | PASS | mem |  |
| read_32_direct | 读回 32B == 预期 pattern | - | PASS | token |  |
| write_read_1 | 读回 1B == 写入值 | - | PASS | token |  |
| write_read_15 | - | - | PASS | token |  |
| write_read_16 | 读回 16B == 写入值 | - | PASS | token |  |
| write_read_17 | 读回 17B == 写入值 | - | PASS | token |  |
| write_read_32 | 读回 32B == 写入值 | - | PASS | token |  |
| ibi | IBI 事件触发且数据 == pattern | - | PASS | token |  |
| hot_join | HJ 被识别并处理，无 URUN | - | PASS | token |  |
| ccc_basic | CCC 完成、响应状态 == 期望值 | - | PASS | token |  |
| write_stop_read_1 | 停后读回 1B == 写入值 | - | PASS | token |  |
| write_stop_read_15 | - | - | PASS | token |  |
| write_stop_read_16 | 停后读回 16B == 写入值 | - | PASS | token |  |
| write_stop_read_17 | 停后读回 17B == 写入值 | - | PASS | token |  |
| write_stop_read_32 | 停后读回 32B == 写入值 | - | PASS | token |  |
| read_underrun_8_16 | 读 16 实际收 8，不挂死，恢复读 OK | - | PASS | token |  |
| read_underrun_16_32 | 读 32 实际收 16，不挂死，恢复读 OK | - | PASS | token |  |
| nack_bad_addr | 不挂死；读不得伪造数据（actual==0 或报错）；事后正常读 OK | - | PASS | token |  |
| back_to_back_8x8 | 8 次全部成功、数据正确 | - | PASS | mem |  |
| big_256 | 256B 写入+读回 == pattern | - | PASS | token |  |
| ccc_rapid_8 | getpid x8 全部成功、PID 非零 | - | PASS | token |  |
| read_master_abort | 读 32 取 8 后，下一笔读从 pattern 起点对齐 | - | PASS | token |  |
| ibi_neg_paths | 三个返回值精确匹配 | - | PASS | token |  |
| api_neg_paths | 全部 -EINVAL，事后正常读 OK | - | PASS | token |  |
| multi_msg_4 | 各段 num_xfer 正确、两段读数据 == pattern | - | PASS | token |  |
| write_indexed_read_16 | - | - | PASS | token |  |
| setaasa | RSTDAA 后 GETPID@0x32 必须 NACK（旧地址已清）；SETAASA 后 GETPID@0x5A 成功且 PID 匹配、探针写可达、SDYNADDR DAVALID=1/DADDR=0x5A/DCAUSE=2；dyn=0/static=0x5A 临时 desc 读必须 -EINVAL 且 desc 不被改写（无静默 fallback，记账根因修复见 i3c_common.c backport 373771c062）；SETNEWDA 恢复 0x32 后 GETPID 正常 | - | PASS | token |  |
| setdasa | RSTDAA 后 GETPID@0x32 必须 NACK；SETDASA@0x5A 分配 0x45：GETPID@0x45 成功且 PID 匹配、探针写可达、SDYNADDR DADDR=0x45/DCAUSE=2、SETNEWDA 恢复 0x32 | - | PASS | token |  |

## case_pure_i3c_12m5 (PASS)
| 测试项 | 预期 | 实际(INFO) | 结果 | 来源 | reason |
|--------|------|-----------|------|------|--------|
| daa_discovery | 发现成功，target 分配到动态地址 | - | PASS | token |  |
| write_8 | 8B 写入无 NACK（SERRWARN 无） | - | PASS | token |  |
| write_100 | 100B 全部写入无 NACK | - | PASS | mem |  |
| read_32_direct | 读回 32B == 预期 pattern | - | PASS | token |  |
| write_read_1 | 读回 1B == 写入值 | - | PASS | token |  |
| write_read_15 | - | - | PASS | token |  |
| write_read_16 | 读回 16B == 写入值 | - | PASS | token |  |
| write_read_17 | 读回 17B == 写入值 | - | PASS | token |  |
| write_read_32 | 读回 32B == 写入值 | - | PASS | token |  |
| ibi | IBI 事件触发且数据 == pattern | - | PASS | token |  |
| hot_join | HJ 被识别并处理，无 URUN | - | PASS | token |  |
| ccc_basic | CCC 完成、响应状态 == 期望值 | - | PASS | token |  |
| write_stop_read_1 | 停后读回 1B == 写入值 | - | PASS | token |  |
| write_stop_read_15 | - | - | PASS | token |  |
| write_stop_read_16 | 停后读回 16B == 写入值 | - | PASS | token |  |
| write_stop_read_17 | 停后读回 17B == 写入值 | - | PASS | token |  |
| write_stop_read_32 | 停后读回 32B == 写入值 | - | PASS | token |  |
| read_underrun_8_16 | 读 16 实际收 8，不挂死，恢复读 OK | - | PASS | token |  |
| read_underrun_16_32 | 读 32 实际收 16，不挂死，恢复读 OK | - | PASS | token |  |
| nack_bad_addr | 不挂死；读不得伪造数据（actual==0 或报错）；事后正常读 OK | - | PASS | token |  |
| back_to_back_8x8 | 8 次全部成功、数据正确 | - | PASS | mem |  |
| big_256 | 256B 写入+读回 == pattern | - | PASS | token |  |
| ccc_rapid_8 | getpid x8 全部成功、PID 非零 | - | PASS | token |  |
| read_master_abort | 读 32 取 8 后，下一笔读从 pattern 起点对齐 | - | PASS | token |  |
| ibi_neg_paths | 三个返回值精确匹配 | - | PASS | token |  |
| api_neg_paths | 全部 -EINVAL，事后正常读 OK | - | PASS | token |  |
| multi_msg_4 | 各段 num_xfer 正确、两段读数据 == pattern | - | PASS | token |  |
| write_indexed_read_16 | - | - | PASS | token |  |
| setaasa | RSTDAA 后 GETPID@0x32 必须 NACK（旧地址已清）；SETAASA 后 GETPID@0x5A 成功且 PID 匹配、探针写可达、SDYNADDR DAVALID=1/DADDR=0x5A/DCAUSE=2；dyn=0/static=0x5A 临时 desc 读必须 -EINVAL 且 desc 不被改写（无静默 fallback，记账根因修复见 i3c_common.c backport 373771c062）；SETNEWDA 恢复 0x32 后 GETPID 正常 | - | PASS | token |  |
| setdasa | RSTDAA 后 GETPID@0x32 必须 NACK；SETDASA@0x5A 分配 0x45：GETPID@0x45 成功且 PID 匹配、探针写可达、SDYNADDR DADDR=0x45/DCAUSE=2、SETNEWDA 恢复 0x32 | - | PASS | token |  |

## case_pure_i3c_6m25 (PASS)
| 测试项 | 预期 | 实际(INFO) | 结果 | 来源 | reason |
|--------|------|-----------|------|------|--------|
| daa_discovery | 发现成功，target 分配到动态地址 | - | PASS | token |  |
| write_8 | 8B 写入无 NACK（SERRWARN 无） | - | PASS | token |  |
| write_100 | 100B 全部写入无 NACK | - | PASS | mem |  |
| read_32_direct | 读回 32B == 预期 pattern | - | PASS | token |  |
| write_read_1 | 读回 1B == 写入值 | - | PASS | token |  |
| write_read_15 | - | - | PASS | token |  |
| write_read_16 | 读回 16B == 写入值 | - | PASS | token |  |
| write_read_17 | 读回 17B == 写入值 | - | PASS | token |  |
| write_read_32 | 读回 32B == 写入值 | - | PASS | token |  |
| ibi | IBI 事件触发且数据 == pattern | - | PASS | token |  |
| hot_join | HJ 被识别并处理，无 URUN | - | PASS | token |  |
| ccc_basic | CCC 完成、响应状态 == 期望值 | - | PASS | token |  |
| write_stop_read_1 | 停后读回 1B == 写入值 | - | PASS | token |  |
| write_stop_read_15 | - | - | PASS | token |  |
| write_stop_read_16 | 停后读回 16B == 写入值 | - | PASS | token |  |
| write_stop_read_17 | 停后读回 17B == 写入值 | - | PASS | token |  |
| write_stop_read_32 | 停后读回 32B == 写入值 | - | PASS | token |  |
| read_underrun_8_16 | 读 16 实际收 8，不挂死，恢复读 OK | - | PASS | token |  |
| read_underrun_16_32 | 读 32 实际收 16，不挂死，恢复读 OK | - | PASS | token |  |
| nack_bad_addr | 不挂死；读不得伪造数据（actual==0 或报错）；事后正常读 OK | - | PASS | token |  |
| back_to_back_8x8 | 8 次全部成功、数据正确 | - | PASS | mem |  |
| big_256 | 256B 写入+读回 == pattern | - | PASS | token |  |
| ccc_rapid_8 | getpid x8 全部成功、PID 非零 | - | PASS | token |  |
| read_master_abort | 读 32 取 8 后，下一笔读从 pattern 起点对齐 | - | PASS | token |  |
| ibi_neg_paths | 三个返回值精确匹配 | - | PASS | token |  |
| api_neg_paths | 全部 -EINVAL，事后正常读 OK | - | PASS | token |  |
| multi_msg_4 | 各段 num_xfer 正确、两段读数据 == pattern | - | PASS | token |  |
| write_indexed_read_16 | - | - | PASS | token |  |
| setaasa | RSTDAA 后 GETPID@0x32 必须 NACK（旧地址已清）；SETAASA 后 GETPID@0x5A 成功且 PID 匹配、探针写可达、SDYNADDR DAVALID=1/DADDR=0x5A/DCAUSE=2；dyn=0/static=0x5A 临时 desc 读必须 -EINVAL 且 desc 不被改写（无静默 fallback，记账根因修复见 i3c_common.c backport 373771c062）；SETNEWDA 恢复 0x32 后 GETPID 正常 | - | PASS | token |  |
| setdasa | RSTDAA 后 GETPID@0x32 必须 NACK；SETDASA@0x5A 分配 0x45：GETPID@0x45 成功且 PID 匹配、探针写可达、SDYNADDR DADDR=0x45/DCAUSE=2、SETNEWDA 恢复 0x32 | - | PASS | token |  |

## 汇总

OVERALL: PASS
