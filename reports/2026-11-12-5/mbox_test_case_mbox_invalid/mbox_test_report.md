# mbox_test (WSL) 测试报告

- 工程: mbox_test
- case: case_mbox_invalid
- 日期: 2026-11-13 03:14:07
- 板级: lsqsh_evb@2os
- 环境: tgt_uart=COM31, ctrl_uart=COM14, timeout=120s, host=wsl, gdb=172.24.48.1:1025

## case_mbox_invalid: PASS (行为项 7/7) CTRL=SUCCESSFUL, TGT=SUCCESSFUL

- 采集时长: 6.1s（timed_out=False；**不参与判定**）

> 结构性守卫（非行为证据）：TGT:tgt_invalid_idle

| 测试项 | 核 | 类别 | 结果 | 明细 |
|---|---|---|---|---|
| ctrl_execution | CTRL | 核判定 | PASS | PROJECT EXECUTION SUCCESSFUL |
| tgt_execution | TGT | 核判定 | PASS | PROJECT EXECUTION SUCCESSFUL |
| inv_send_null_msg | CTRL | 行为 | PASS | ret=-22,expect=-22 |
| inv_send_bad_channel | CTRL | 行为 | PASS | ret=-22,expect=-22 |
| inv_send_oversize_ch4 | CTRL | 行为 | PASS | ret=-122,expect=-122 |
| inv_send_null_data | CTRL | 行为 | PASS | ret=-122,expect=-122 |
| inv_send_oversize_ch2 | CTRL | 行为 | PASS | ret=-122,expect=-122 |
| inv_register_bad_channel | CTRL | 行为 | PASS | ret=-22,expect=-22 |
| inv_set_enabled_bad_channel | CTRL | 行为 | PASS | ret=-22,expect=-22 |
| inv_zero_size_accepted | CTRL | 观察 | OBS | ret=0,recorded=0 |
| inv_mtu_get_unimplemented | CTRL | 观察 | OBS | ret=-88,recorded=-88 |
| tgt_invalid_idle | TGT | 守卫 | GUARD | idle_ms=4000 |

### 失败摘要
- 无

### 观察项（observe=1，不判负）
- OBS [CTRL] inv_zero_size_accepted: ret=0,recorded=0
- OBS [CTRL] inv_mtu_get_unimplemented: ret=-88,recorded=-88

- `<err>` 日志计数（仅展示）：CTRL=6, TGT=0
