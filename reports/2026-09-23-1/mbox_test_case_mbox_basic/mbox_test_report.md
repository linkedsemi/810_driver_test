# mbox_test 测试报告

- 工程: mbox_test
- case: case_mbox_basic
- 日期: 2026-09-22 22:23:33
- 板级: lsqsh_evb@2os

## case_mbox_basic: PASS (行为项 5/5)  CTRL_EXECUTION=SUCCESSFUL, TGT_EXECUTION=SUCCESSFUL

- 采集时长: 13.2s（timed_out=False；**不参与判定**，见脚本文件头）

| 测试项 | 核 | 类别 | 结果 | 明细 |
|---|---|---|---|---|
| ctrl_execution | CTRL | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| tgt_execution | TGT | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| basic_ch4_frames | CTRL | 行为 | ✅ PASS | sent=5,failed=0,expect=5 |
| basic_ch5_frames | CTRL | 行为 | ✅ PASS | sent=3,failed=0,expect=3 |
| basic_mtu_full | CTRL | 行为 | ✅ PASS | ret=0,size=64,width=64 |
| basic_ch4_frames | TGT | 行为 | ✅ PASS | recv=5,expect=5,verify_err=0,seq_err=0 |
| basic_ch5_frames | TGT | 行为 | ✅ PASS | recv=3,expect=3,verify_err=0,seq_err=0 |

### 失败摘要
- 无

### 观察项（observe=1，不判负）
- 无

- `<err>` 日志计数（仅展示，不参与判定）：CTRL=0, TGT=0
