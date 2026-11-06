# mbox_test 测试报告

- 工程: mbox_test
- case: case_mbox_rtc_sync
- 日期: 2026-11-05 22:36:52
- 板级: lsqsh_evb@2os

## case_mbox_rtc_sync: PASS (行为项 4/4)  CTRL_EXECUTION=SUCCESSFUL, TGT_EXECUTION=SUCCESSFUL

- 采集时长: 11.6s（timed_out=False；**不参与判定**，见脚本文件头）

| 测试项 | 核 | 类别 | 结果 | 明细 |
|---|---|---|---|---|
| ctrl_execution | CTRL | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| tgt_execution | TGT | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| rtc_sync_delta | CTRL | 行为 | ✅ PASS | delta1_s=1,delta2_s=0,frame_bad=0,tgt_bad=0 |
| rtc_sync_monotonic | CTRL | 行为 | ✅ PASS | e_tgt1=2117514496,e_tgt2=2117514497 |
| rtc_read_advances | CTRL | 行为 | ✅ PASS | tgt_advance_s=1,source=payload |
| rtc_read_advances | TGT | 行为 | ✅ PASS | e1=2117514496,e2=2117514497,advance_s=1,source=local |

### 失败摘要
- 无

### 观察项（observe=1，不判负）
- 无

- `<err>` 日志计数（仅展示，不参与判定）：CTRL=0, TGT=0
