# mbox_test 测试报告

- 工程: mbox_test
- case: case_mbox_fifo_full
- 日期: 2026-11-04 23:37:31
- 板级: lsqsh_evb@2os

## case_mbox_fifo_full: PASS (行为项 2/2)  CTRL_EXECUTION=SUCCESSFUL, TGT_EXECUTION=SUCCESSFUL

- 采集时长: 11.5s（timed_out=False；**不参与判定**，见脚本文件头）

| 测试项 | 核 | 类别 | 结果 | 明细 |
|---|---|---|---|---|
| ctrl_execution | CTRL | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| tgt_execution | TGT | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| fifo_full_enospc | CTRL | 行为 | ✅ PASS | sent=6,ok=4,enospc=2,other=0,expect_ok=4,expect_enospc=2 |
| fifo_full_drain_on_enable | TGT | 行为 | ✅ PASS | drained=4,drop=0,expect=4 |

### 失败摘要
- 无

### 观察项（observe=1，不判负）
- 无

- `<err>` 日志计数（仅展示，不参与判定）：CTRL=2, TGT=0
