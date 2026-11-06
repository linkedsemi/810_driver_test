# i3c_dualcore_test_mixed_1m_12m5 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | i3c_dualcore_test |
| case | case_mixed_1m_12m5 |
| 执行标记 | CTRL=FAILED, TGT=None |
| 内存兜底 | 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。 |
| 报告 | [i3c_dualcore_test_report.md](../i3c_dualcore_test_case_mixed_1m_12m5/i3c_dualcore_test_report.md) |
| 结果 | FAIL (34/35；失败 1) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| multi_msg_4 | ❌ FAIL | token | 各段 num_xfer 正确、两段读数据 == pattern | `msg3 num_xfer 0 != 8`（serial_CTRL.log:112-127；`START - multi_msg_4` 后 CTRL_STEP ST/IBI 序列正常，第 3 段 num_xfer 读到 0） | CTRL:WEST_TOPDIR/810_driver_test/projects/i3c_dualcore_test/cpu0_i3c_controller/src/i3c_controller_test.c: 201: ctrl_test_end: (ctrl_failed is true) | serial_CTRL.log（见「实际」列行号） |

## 归因

**B 用例/驱动（本批不深挖）**：`multi_msg_4` 第 3 段 `num_xfer 0 != 8` —— 控制器
在 4 段消息的第 3 段未产生传输（用例期望 8 次 xfer）。TGT 侧同项 PASS。指向控制器侧
多段消息（repeated START）在第 3 段丢失，属 16 B 边界/多段续传已知偶发族
（G-ITEST-MERGED-I3C-16B-INTERMITTENT）。
缺证据：本轮 CTRL 侧 I3C 寄存器快照 / 波形（批量路径不抓）。

## 备注

同批 `case_pure_i3c_12m5` / `case_mixed_1m_12m5` 各有 1 项 FAIL，均为单点、非整组 0/35 形貌，
与既往 i3c 16 B 边界/FIFO 补填类偶发（G-ITEST-MERGED-I3C-16B-INTERMITTENT）同族，本批不做定位。
