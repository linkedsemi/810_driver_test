# rtc_timer_ls 覆盖矩阵（片内，无外部接线）

被测：`zephyr/drivers/rtc/rtc_timer_ls.c`（`linkedsemi,ls-rtc-timer`）
能力：仅 `set_time` / `get_time`（无 alarm / calibration）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| a_device_ready | DEVICE_DT_GET ready | 冒烟 | ready | 设备节点 | ☐ |
| a_get_uninitialized | get 在 SFT_CTRL=0xFFFFFFFF | 异常 | -ENODATA | API | ☐ |
| b_set_get_roundtrip | set→get 民用时 | 正常 | 字段一致(±1s) | 功能 | ☐ |
| b_time_advances | sleep 2s | 正常 | Δt∈[1,3]s | 计时链 | ☐ |
| b_get_normalizes_meta | set isdst/nsec → get | 正常 | isdst=-1,nsec=0 | 归一化 | ☐ |
| c_year_bounds | year 69/70/199/200 | 边界 | EINVAL/0/0/EINVAL | calendar_valid | ☐ |
| c_month_bounds | mon -1/12/0/11 | 边界 | EINVAL×2 + 0×2 | calendar_valid | ☐ |
| d_day_bounds_30day | Apr 30/31/0 | 边界 | 0/EINVAL/EINVAL | 月长 | ☐ |
| d_day_bounds_feb_nonleap | 2023-02-28/29 | 边界 | 0/EINVAL | 非闰 | ☐ |
| d_day_bounds_feb_leap | 2024-02-29/30 | 边界 | 0/EINVAL | 闰年 | ☐ |
| e_hms_bounds | 23:59:59 / 24 / 60 / 60 | 边界 | 0 + EINVAL×3 | HMS | ☐ |
| e_wday_bounds | -1/6/-2/7 | 边界 | 0×2 + EINVAL×2 | wday | ☐ |
| e_yday_bounds | -1/365/366(闰)/364/365(平)/-2 | 边界 | 见断言 | yday | ☐ |
| e_isdst_nsec_bounds | isdst -1..2；nsec 0/max/-1/1e9 | 边界 | 见断言 | meta | ☐ |
| f_day_31_and_rollover_sample | Jan31/32 + 年尾+3s | 边界+抽样 | 0/EINVAL + 跨年 | 31天/滚动 | ☐ |

外部: 无（片内 BSTIM+PIS+GPTIMA）
