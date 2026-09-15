# RTC (rtc_ls) 驱动测试报告

- 工程: rtc_ls_test
- 日期: 2026-09-09 12:59:47
- 板级: lsqsh_evb@2os/lsqsh/cpu1
- 环境: tgt_uart=COM10, timeout=150s

## 测试项结果

| 测试项 | 结果 | 预期(COVERAGE) | 实际(INFO) | 失败原因 | 来源 |
|---|---|---|---|---|---|
| set_get_roundtrip | PASS | 设 2026-08-21 12:34:56 → 回读年月日周相等、秒差∈[0,5] | delta_s=0 |  | src=token |
| time_advances | PASS | sleep 2.1s 秒差∈[1,4] | delta_s=2 |  | src=token |
| invalid_set_args | PASS | sec=99 / mday=32 / 2-29 平年 → -EINVAL |  |  | src=token |
| alarm_set_get | PASS | 设闹钟 → 回读逐字段相等 |  |  | src=token |
| alarm_fires | PASS | 08:00:00 设 RTC → 08:00:10 闹钟 → 20s 内回调 + pending 1→0 | fire1 |  | src=token |
| alarm_invalid_args | PASS | sec=99 → -EINVAL |  |  | src=token |
| calibration_roundtrip | PASS | +1e8 ppb → get==1e8 | ppb=100000000 |  | src=token |
| calibration_negative | PASS | -1e8 → ret==0 |  |  | src=token |
| calibration_null_arg | PASS | → -EINVAL |  |  | src=token |
| calendar_rollover | PASS | 2026 全年月末 23:59:57→次日 00:00:00；2028 闰年 2/28→2/29→3/1 | feb29_to_mar1_leap28 |  | src=token |
