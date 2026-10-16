# mbox_test 测试报告

- 工程: mbox_test
- case: case_mbox_consumers
- 日期: 2026-10-12 21:42:31
- 板级: lsqsh_evb@2os

## case_mbox_consumers: PASS (行为项 12/12)  CTRL_EXECUTION=SUCCESSFUL, TGT_EXECUTION=SUCCESSFUL

- 采集时长: 11.2s（timed_out=False；**不参与判定**，见脚本文件头）

| 测试项 | 核 | 类别 | 结果 | 明细 |
|---|---|---|---|---|
| ctrl_execution | CTRL | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| tgt_execution | TGT | 核判定 | ✅ PASS | PROJECT EXECUTION SUCCESSFUL |
| consumer_ch1_roundtrip | CTRL | 行为 | ✅ PASS | ch=1,size=32 |
| consumer_ch2_roundtrip | CTRL | 行为 | ✅ PASS | ch=2,size=8 |
| consumer_ch3_roundtrip | CTRL | 行为 | ✅ PASS | ch=3,size=8 |
| consumer_ch4_roundtrip | CTRL | 行为 | ✅ PASS | ch=4,size=64 |
| consumer_same_channel_bidir | CTRL | 行为 | ✅ PASS | bidir_ok=4/4 |
| consumer_ch5_short_frame | CTRL | 行为 | ✅ PASS | ack_len=64,expect=64,ack_st=0 |
| consumer_ch1_roundtrip | TGT | 行为 | ✅ PASS | ch=1,size=32 |
| consumer_ch2_roundtrip | TGT | 行为 | ✅ PASS | ch=2,size=8 |
| consumer_ch3_roundtrip | TGT | 行为 | ✅ PASS | ch=3,size=8 |
| consumer_ch4_roundtrip | TGT | 行为 | ✅ PASS | ch=4,size=64 |
| consumer_same_channel_bidir | TGT | 行为 | ✅ PASS | bidir_ok=4/4 |
| consumer_ch5_short_frame | TGT | 行为 | ✅ PASS | rx_len=64,expect=64 |

### 失败摘要
- 无

### 观察项（observe=1，不判负）
- 无

- `<err>` 日志计数（仅展示，不参与判定）：CTRL=0, TGT=0
