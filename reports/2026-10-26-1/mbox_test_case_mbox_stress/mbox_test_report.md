# mbox_test 测试报告

- 工程: mbox_test
- case: case_mbox_stress
- 日期: 2026-10-26 11:21:22
- 板级: lsqsh_evb@2os

## case_mbox_stress: PASS (行为项 3/3)  CTRL_EXECUTION=SUCCESSFUL, TGT_EXECUTION=SUCCESSFUL

- 采集时长: 11.5s（timed_out=False；**不参与判定**，见脚本文件头）

| 测试项 | 核 | 类别 | 结果 | 明细 |
|---|---|---|---|---|
| ctrl_execution | CTRL | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| tgt_execution | TGT | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| stress_n_messages | CTRL | 行为 | ✅ PASS | sent=1000,ack_ok=1000,ack_timeout=0,seq_err=0,send_fail=0,tgt_recv=1000,tgt_seq_err=0,tgt_payload_err=0,tgt_isr=1001,tgt_ |
| stress_rtt_stats | CTRL | 行为 | ✅ PASS | samples=1000,rtt_us_min=34,rtt_us_avg=37,rtt_us_max=128 |
| stress_n_messages | TGT | 行为 | ✅ PASS | recv=1000,expect=1000,seq_err=0,payload_err=0,isr_total=1001,isr_drop=0,crc=0xbf4ff62e |

### 失败摘要
- 无

### 观察项（observe=1，不判负）
- 无

- `<err>` 日志计数（仅展示，不参与判定）：CTRL=0, TGT=0
