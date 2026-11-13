# rtc_timer_ls 驱动测试报告

- 工程: rtc_timer_ls_test
- 日期: 2026-11-13 18:03:27
- 板级: lsqsh_evb@2os/lsqsh/cpu1
- libc: PICOLIBC
- 环境: tgt_uart=COM31, timeout=90.0s, host=wsl, libc=PICOLIBC

## 测试项结果

| 测试项 | 结果 | 实际(INFO) | 失败原因 | 来源 |
|---|---|---|---|---|
| a_device_ready | PASS |  |  | src=token |
| a_get_uninitialized | PASS |  |  | src=token |
| b_get_normalizes_meta | PASS |  |  | src=token |
| b_set_get_roundtrip | PASS | y=2026, mon=6, d=15 |  | src=token |
| b_time_advances | PASS | delta_s=2 |  | src=token |
| c_month_bounds | PASS |  |  | src=token |
| c_year_bounds | PASS |  |  | src=token |
| d_day_bounds_30day | PASS |  |  | src=token |
| d_day_bounds_feb_leap | PASS |  |  | src=token |
| d_day_bounds_feb_nonleap | PASS |  |  | src=token |
| e_hms_bounds | PASS |  |  | src=token |
| e_isdst_nsec_bounds | PASS |  |  | src=token |
| e_wday_bounds | PASS |  |  | src=token |
| e_yday_bounds | PASS |  |  | src=token |
| f_day_31_and_rollover_sample | PASS | y=2027, mon=1, d=1 |  | src=token |
