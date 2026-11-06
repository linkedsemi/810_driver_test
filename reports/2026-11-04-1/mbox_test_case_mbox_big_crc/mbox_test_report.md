# mbox_test 测试报告

- 工程: mbox_test
- case: case_mbox_big_crc
- 日期: 2026-11-04 23:21:11
- 板级: lsqsh_evb@2os

## case_mbox_big_crc: PASS (行为项 4/4)  CTRL_EXECUTION=SUCCESSFUL, TGT_EXECUTION=SUCCESSFUL

- 采集时长: 11.0s（timed_out=False；**不参与判定**，见脚本文件头）

| 测试项 | 核 | 类别 | 结果 | 明细 |
|---|---|---|---|---|
| ctrl_execution | CTRL | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| tgt_execution | TGT | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| big_crc_chunk_ack | CTRL | 行为 | ✅ PASS | chunks=79,chunk_err=0,ack_timeout=0,send_fail=0 |
| big_payload_crc | CTRL | 行为 | ✅ PASS | bytes=4096,tgt_recv=4096,crc=0xa2912082,tgt_crc=0xa2912082,tgt_st=0,us=2460 |
| big_payload_crc | TGT | 行为 | ✅ PASS | bytes=4096,recv=4096,pattern_err=0,seq_err=0,crc=0xa2912082,peer_crc=0xa2912082 |
| big_crc_chunk_ack | TGT | 行为 | ✅ PASS | chunks=79,ack_err=0 |

### 失败摘要
- 无

### 观察项（observe=1，不判负）
- 无

- `<err>` 日志计数（仅展示，不参与判定）：CTRL=0, TGT=0
