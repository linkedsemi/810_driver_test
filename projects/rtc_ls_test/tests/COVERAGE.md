# RTC (rtc_ls) 测试覆盖矩阵

- 驱动: `zephyr/drivers/rtc/rtc_ls.c`（`linkedsemi_ls_rtc`，`rtc@4006fc00`，PMU 域）
- 工程: `810_driver_test/projects/rtc_ls_test`
- 外部路径: **无**（片内寄存器外设，cpu1 安全域）
- 接口: Zephyr RTC API `rtc_set_time/get_time`、`rtc_alarm_*`、`rtc_set/get_calibration`

| 测试项 | 驱动路径 | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|----------|------|----------|----------|------|
| set_get_roundtrip | rtc_ls_set_time + rtc_ls_get_time | 功能通路 | 设 2026-08-21 12:34:56 → 回读年月日周相等、秒差∈[0,5] | API 往返 | ☐ |
| time_advances | rtc_ls_get_time 两次采样 | 时序 | sleep 2.1s 秒差∈[1,4] | RTC 计数 | ☐ |
| invalid_set_args | is_valid_time | 异常参数 | sec=99 / mday=32 / 2-29 平年 → -EINVAL | API 契约 | ☐ |
| alarm_set_get | alarm_set/get_time | 功能通路 | 设闹钟 → 回读逐字段相等 | API 往返 | ☐ |
| alarm_fires | rtc_irq_handler + alarm_is_pending | 中断 | 08:00:00 设 RTC → 08:00:10 闹钟 → 20s 内回调 + pending 1→0 | 中断契约 | ☐ |
| alarm_invalid_args | alarm is_valid_time | 异常参数 | sec=99 → -EINVAL | API 契约 | ☐ |
| calibration_roundtrip | set/get_calibration 正值 | 功能通路 | +1e8 ppb → get==1e8 | 公式反推 | ☐ |
| calibration_negative | set_calibration 负值 | 边界 | -1e8 → ret==0 | API 契约 | ☐ |
| calibration_null_arg | get_calibration(NULL) | 异常参数 | → -EINVAL | 指针检查 | ☐ |
| calendar_rollover | set_time + 硬件进位 | 日历 | 2026 全年月末 23:59:57→次日 00:00:00；2028 闰年 2/28→2/29→3/1 | 公历边界 | ☐ |

## 判定说明

- 无黄金向量；用错误码、往返一致、中断契约、INFO token（§13.4）判定。
- 全项约需 **90–120s**（calendar_rollover 24 子用例 × 3.1s）；回归默认 timeout=150s。
- Harness: `tests/run_test.py`（RTC_TEST token + `dump_results.py` 内存兜底）。
