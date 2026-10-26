# RTC Timer (rtc_timer_ls) 驱动测试报告

- 工程: rtc_timer_ls_test
- case: case_default
- 日期: 2026-10-23 22:21:49
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=30s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (15/15)
- 接线: 无外部接线（片内 BSTIM+PIS+GPTIMA 软件 RTC 链；cpu1 uart1，banner 'CPU1 RTC_TIMER'）

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| a_device_ready | ✅ PASS | token | ready | - |  |
| a_get_uninitialized | ✅ PASS | token | -ENODATA | - |  |
| b_get_normalizes_meta | ✅ PASS | token | isdst=-1,nsec=0 | - |  |
| b_set_get_roundtrip | ✅ PASS | token | 字段一致(±1s) | y=2026, mon=6, d=15 |  |
| b_time_advances | ✅ PASS | token | Δt∈[1,3]s | delta_s=2 |  |
| c_month_bounds | ✅ PASS | token | EINVAL×2 + 0×2 | - |  |
| c_year_bounds | ✅ PASS | token | EINVAL/0/0/EINVAL | - |  |
| d_day_bounds_30day | ✅ PASS | token | 0/EINVAL/EINVAL | - |  |
| d_day_bounds_feb_leap | ✅ PASS | token | 0/EINVAL | - |  |
| d_day_bounds_feb_nonleap | ✅ PASS | token | 0/EINVAL | - |  |
| e_hms_bounds | ✅ PASS | token | 0 + EINVAL×3 | - |  |
| e_isdst_nsec_bounds | ✅ PASS | token | 见断言 | - |  |
| e_wday_bounds | ✅ PASS | token | 0×2 + EINVAL×2 | - |  |
| e_yday_bounds | ✅ PASS | token | 见断言 | - |  |
| f_day_31_and_rollover_sample | ✅ PASS | token | 0/EINVAL + 跨年 | y=2027, mon=1, d=1 |  |

## 汇总

OVERALL: PASS
