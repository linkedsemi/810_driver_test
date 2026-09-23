# mbox_test 测试报告

- 工程: mbox_test
- case: case_mbox_invalid
- 日期: 2026-09-22 22:32:55
- 板级: lsqsh_evb@2os

## case_mbox_invalid: PASS (行为项 7/7)  CTRL_EXECUTION=SUCCESSFUL, TGT_EXECUTION=SUCCESSFUL

- 采集时长: 14.1s（timed_out=False；**不参与判定**，见脚本文件头）

> 结构性守卫（非行为证据）：TGT:tgt_invalid_idle

| 测试项 | 核 | 类别 | 结果 | 明细 |
|---|---|---|---|---|
| ctrl_execution | CTRL | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| tgt_execution | TGT | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| inv_send_null_msg | CTRL | 行为 | ✅ PASS | ret=-134,expect=-134 |
| inv_send_bad_channel | CTRL | 行为 | ✅ PASS | ret=-22,expect=-22 |
| inv_send_oversize_ch4 | CTRL | 行为 | ✅ PASS | ret=-122,expect=-122 |
| inv_send_null_data | CTRL | 行为 | ✅ PASS | ret=-122,expect=-122 |
| inv_send_oversize_ch2 | CTRL | 行为 | ✅ PASS | ret=-122,expect=-122 |
| inv_register_bad_channel | CTRL | 行为 | ✅ PASS | ret=-22,expect=-22 |
| inv_set_enabled_bad_channel | CTRL | 行为 | ✅ PASS | ret=-22,expect=-22 |
| inv_zero_size_accepted | CTRL | 观察 | 👁 OBS | ret=0,recorded=0 |
| inv_mtu_get_unimplemented | CTRL | 观察 | 👁 OBS | ret=-88,recorded=-88 |
| tgt_invalid_idle | TGT | 守卫 | 🛡 GUARD | idle_ms=4000 |

### 失败摘要
- 无

### 观察项（observe=1，不判负）
- OBS [CTRL] inv_zero_size_accepted: ret=0,recorded=0
- OBS [CTRL] inv_mtu_get_unimplemented: ret=-88,recorded=-88

> 观察项把驱动的**当前缺陷**固化成预期值（COVERAGE.md §3.4）。`OBS` = 现状与记录一致；`OBS_DIFF` = 现状已变，**须人工复核并同步改判**；两者都不参与本 case 的 PASS/FAIL。

### 结构性守卫（guard=1，非行为证据）
- [TGT] tgt_invalid_idle: idle_ms=4000

> 守卫项只证明该核已启动并走完入口，**不构成行为验证**；本 case 的行为判据见上表 `类别=行为` 的行。

- `<err>` 日志计数（仅展示，不参与判定）：CTRL=7, TGT=0
