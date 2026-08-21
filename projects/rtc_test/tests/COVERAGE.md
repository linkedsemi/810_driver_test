# RTC 测试覆盖矩阵

- 驱动: `zephyr/drivers/rtc/rtc_ls.c`（`linkedsemi_ls_rtc`，`rtc: rtc@4006fc00`，PMU 域）
- 外部路径: **无**（片内寄存器外设，cpu1 安全域，CLAUDE.md §14 免环境确认）
- 接口: Zephyr RTC API `rtc_set_time/get_time`、`rtc_alarm_*`（CONFIG_RTC_ALARM）、
  `rtc_set/get_calibration`（CONFIG_RTC_CALIBRATION）
- 关键行为（源码行号）:
  - set_time（157-188 行）：is_valid_time 校验（132-148 行）→ 非法 -EINVAL；
    先 rtc_en=0 → 写 TIME/CAL → 翻转 SET_TGGL（181 行）→ rtc_en=1
  - get_time（85-123 行）：CURCAL 两次一致稳定读；硬件 1-60 编码回读 >0 时 -1
  - alarm（190-300 行）：完整匹配年月日时分秒；ISR（66-83 行）清 INTR/关 ALARM_EN/调回调；
    is_pending 读 CTRL.ALARM_EN（273-284 行）
  - calibration（328-389 行）：正值 calib_cyc=ppb*60/1e9（345-352 行）；负值走
    floor_custom_divisor 分支（354-366 行）；get 反推（371-389 行，NULL → -EINVAL）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| set_get_roundtrip | rtc_ls_set_time + rtc_ls_get_time 全路径（157/85 行） | 功能通路 | 设 2026-08-21(五) 12:34:56 → 回读年月日周相等、秒差∈[0,5] | API 契约 + 往返一致 | ☐（驱动层 FAIL，见 RTC_2FAILS_ROOTS.md） |
| time_advances | rtc_ls_get_time 两次采样（CURCAL 稳定读路径） | 统计/性质 | sleep 2.1s 前后秒差∈[1,4] | RTC 计数契约（cyc-1hz=32768） | ✅ |
| invalid_set_args | is_valid_time 校验分支（132-148 行） | 异常参数 | sec=99 / mday=32 / 2026-02-29 → -EINVAL | API 契约（驱动校验） | ✅ |
| alarm_set_get | rtc_ls_alarm_set_time + alarm_get_time（191/239 行） | 功能通路 | 设 2026-07-15 08:20:10 → 回读逐字段相等 | API 契约 + 往返一致 | ✅ |
| alarm_fires | rtc_irq_handler ISR 路径（66-83 行）+ alarm_is_pending（273 行） | 中断/时序 | +2s 闹钟 → 4s 内回调置位 + pending 1→0 | 中断契约 | ☐（驱动层 FAIL，见 RTC_2FAILS_ROOTS.md） |
| alarm_invalid_args | rtc_ls_alarm_set_time is_valid_time 分支（197 行） | 异常参数 | 非法时间 → -EINVAL | API 契约（驱动校验） | ✅ |
| calibration_roundtrip | rtc_ls_set_calibration 正值分支（345-352 行）+ get（371 行） | 功能通路 | set +100000000 ppb（calib_cyc=6）→ get 回读 ==100000000 | 寄存器反推公式（calib_cyc*1e9/60） | ✅ |
| calibration_negative | rtc_ls_set_calibration 负值分支 floor_custom_divisor（354-366 行） | 边界长度 | set -100000000 → ret==0（回读不等价，不断言值） | API 契约 | ✅ |
| calibration_null_arg | rtc_ls_get_calibration NULL 检查（375-378 行） | 异常参数 | get_calibration(NULL) → -EINVAL | 驱动指针检查 | ✅ |

## 预期数据说明

- RTC 是实时时钟，**无黄金向量**（时间随运行前进），确定性判定用：
  - 错误码契约：`ret==0` / `-EINVAL`（非法参数，驱动 is_valid_time / NULL 检查）
  - 往返一致：set → get 逐字段相等（年月日周精确、时分秒容差 [0,5]s）
  - 中断契约：+2s 闹钟 4s 内触发（ISR 回调置位 + pending 1→0）
  - 校准反推公式：set +1e8 ppb → calib_cyc=6 → get = 6*1e9/60 = 1e8 精确往返
- 数值实测（往返秒差 delta_s、校准回读 ppb）经 INFO token 上报（§13.4），报告"实际"列对比。
- 状态: ✅ = 回归 PASS（2026-08-21）；☐ = 驱动层 FAIL（详见 RTC_2FAILS_ROOTS.md，非测试代码问题）。
