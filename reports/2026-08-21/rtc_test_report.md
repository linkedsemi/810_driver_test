# RTC 驱动测试报告

- 工程: rtc_test
- 日期: 2026-08-21 21:18:07
- 板级: lsqsh_evb@2os/lsqsh/cpu1
- 环境: tgt_uart=auto, timeout=30s

## 测试项结果

| 测试项 | 结果 | 预期(COVERAGE) | 实际(INFO) | 失败原因 | 来源 |
|---|---|---|---|---|---|
| set_get_roundtrip | ❌ FAIL | 设 2026-08-21(五) 12:34:56 → 回读年月日周相等、秒差∈[0,5] | delta_s=-45292 | mday 1 != 21; mon 1 != 7; year 1 != 126; wday 1 != 5; set->get delta -45292s out of [0,5]; | src=token |
| time_advances | ✅ PASS | sleep 2.1s 前后秒差∈[1,4] | delta_s=2 |  | src=token |
| invalid_set_args | ✅ PASS | sec=99 / mday=32 / 2026-02-29 → -EINVAL |  |  | src=token |
| alarm_set_get | ✅ PASS | 设 2026-07-15 08:20:10 → 回读逐字段相等 |  |  | src=token |
| alarm_fires | ❌ FAIL | +2s 闹钟 → 4s 内回调置位 + pending 1→0 |  | alarm callback not fired in 4s; alarm still pending after fire; | src=token |
| alarm_invalid_args | ✅ PASS | 非法时间 → -EINVAL |  |  | src=token |
| calibration_roundtrip | ✅ PASS | set +100000000 ppb（calib_cyc=6）→ get 回读 ==100000000 | ppb=100000000 |  | src=token |
| calibration_negative | ✅ PASS | set -100000000 → ret==0（回读不等价，不断言值） |  |  | src=token |
| calibration_null_arg | ✅ PASS | get_calibration(NULL) → -EINVAL |  |  | src=token |
