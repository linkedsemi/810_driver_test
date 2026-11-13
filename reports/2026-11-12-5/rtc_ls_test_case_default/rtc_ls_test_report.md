# RTC (rtc_ls) 驱动测试报告

- 工程: rtc_ls_test
- 日期: 2026-11-13 12:52:43
- 板级: lsqsh_evb@2os/lsqsh/cpu1
- 环境: tgt_uart=COM31, timeout=180.0s, host=wsl

## 测试项结果

| 测试项 | 结果 | 实际(INFO) | 失败原因 | 来源 |
|---|---|---|---|---|
| set_get_roundtrip | PASS |  |  | src=mem |
| time_advances | PASS |  |  | src=mem |
| invalid_set_args | PASS |  |  | src=mem |
| alarm_set_get | PASS |  |  | src=mem |
| alarm_fires | PASS |  |  | src=mem |
| alarm_invalid_args | PASS |  |  | src=mem |
| calibration_roundtrip | PASS |  |  | src=mem |
| calibration_negative | PASS |  |  | src=mem |
| calibration_null_arg | PASS |  |  | src=mem |
| calendar_rollover | PASS |  |  | src=mem |
