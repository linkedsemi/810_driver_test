# RTC 测试预期数据说明

RTC 为实时时钟外设，时间随运行前进，**无黄金向量**。确定性判定方式：

1. **错误码契约**：`rtc_set_time` / `rtc_alarm_set_time` 对非法时间返回 `-EINVAL`
   （驱动 `is_valid_time` 校验：秒/分/时/日/月/年/周逐项检查，含闰年 2 月）；
   `rtc_get_calibration(NULL)` 返回 `-EINVAL`（驱动指针检查）。
2. **往返一致**：set 固定时间 → get 回读逐字段相等（年月日周精确相等，
   时分秒因 RTC 在走，断言秒差 delta ∈ [0,5]）。
3. **中断契约**：设当前时间 +2s 闹钟（带进位不跨天）→ 4s 内 ISR 触发回调
   置位 + `alarm_is_pending` 1→0（ISR 触发后驱动关 ALARM_EN）。
4. **校准反推公式**：`set_calibration(+1e8 ppb)` → 驱动 `calib_cyc = 1e8*60/1e9 = 6`
   → `get_calibration` 反推 `6*1e9/60 = 1e8` 精确往返。负值路径（floor_custom_divisor）
   反推不等价（驱动设计），只断言 API 接受（ret==0）。

统计门限：时间前进 sleep 2.1s 前后秒差 ∈ [1,4]（硬件 RTC 计数，cyc-1hz=32768）。

数值实测（delta_s / ppb）经 INFO token 上报（§13.4），报告"实际"列对比。
