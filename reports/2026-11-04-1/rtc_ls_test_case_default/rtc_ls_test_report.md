# RTC (rtc_ls) 驱动测试报告

- 工程: rtc_ls_test
- case: case_default
- 日期: 2026-11-04 21:49:07
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=150s
- 执行标记: PASS
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (15/15)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| set_get_roundtrip | ✅ PASS | token | 设 2026-08-21 12:34:56 → 回读年月日周相等、秒差∈[0,5] | delta_s=0 |  |
| time_advances | ✅ PASS | token | sleep 2.1s 秒差∈[1,4] | delta_s=2 |  |
| invalid_set_args | ✅ PASS | token | sec=99 / mday=32 / 2-29 平年 → -EINVAL | - |  |
| alarm_set_get | ✅ PASS | token | 设闹钟 → 回读逐字段相等 | - |  |
| alarm_fires | ✅ PASS | token | 08:00:00 设 RTC → 08:00:10 闹钟 → 20s 内回调 + pending 1→0 | fire1:triggered=1,pending=0,now=2026-09-01 08:00:07 |  |
| alarm_invalid_args | ✅ PASS | token | sec=99 → -EINVAL | - |  |
| calibration_roundtrip | ✅ PASS | token | +1e8 ppb → get==1e8 | ppb=100000000 |  |
| calibration_negative | ✅ PASS | token | -1e8 → ret==0 **且 CALIB==0x36007FFE（独立手算）、CTRL.CALIB_EN(BIT1… | calib_reg=0x36007ffe,ctrl=0x0000000f |  |
| calibration_null_arg | ✅ PASS | token | → -EINVAL | - |  |
| calendar_rollover | ✅ PASS | token | 2026 全年月末 23:59:57→次日 00:00:00；2028 闰年 2/28→2/29→3/1 | feb29_to_mar1_leap28:set=2028年02月29日 周2 23:59:57 expect=2028年03月01日 周3 00:00:00 get=2028年03月01日 周3 00:00:00 |  |
| invalid_set_args_ext | ✅ PASS | token | - | - |  |
| leap_year_dates | ✅ PASS | token | - | y2028_get=2028-02-29 |  |
| alarm_supported_fields | ✅ PASS | token | - | mask=0x003f,expect=0x003f |  |
| update_callback_notsup | ✅ PASS | token | - | ret=-134 |  |
| drift_measure | ✅ PASS | token | - | rtc_ms=10000,uptime_ms=10000,drift_ms=0 |  |

## 汇总

OVERALL: PASS
