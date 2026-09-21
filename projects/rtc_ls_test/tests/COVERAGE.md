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
| calibration_negative | set_calibration 负值 → **直读 CALIB/CTRL 寄存器黄金值** | 边界 | -1e8 → ret==0 **且 CALIB==0x36007FFE（独立手算）、CTRL.CALIB_EN(BIT1)=1** | 驱动源码语义独立推算 + `reg_rtcv2_type.h` 寄存器布局 | ☐ |
| calibration_null_arg | get_calibration(NULL) | 异常参数 | → -EINVAL | 指针检查 | ☐ |
| calendar_rollover | set_time + 硬件进位 | 日历 | 2026 全年月末 23:59:57→次日 00:00:00；2028 闰年 2/28→2/29→3/1 | 公历边界 | ☐ |
| **invalid_set_args_ext** | `is_valid_time()` 剩余拒绝分支（min>59 / hour>23 / mon>11 / wday>6 / mday<1 / 30 天月份取 31） | 异常参数 | 逐项 → `-EINVAL`；同基准合法值反向确认 == 0 | API 契约 | ⏳ |
| **leap_year_dates** | `is_leap_year()` 三分支（4 整除 / 400 整除 / 100 整除非 400）+ `is_valid_time()` 2 月天数分支 | 边界长度 | 2028-02-29 / 2000-02-29 == 0 且 2028 回读 `128/1/29`；2100-02-29 / 2027-02-29 → `-EINVAL` | 公历规则（Gregorian） | ⏳ |
| **alarm_supported_fields** | `rtc_ls_alarm_get_supported_fields()`（**此前零覆盖**）+ `rtc_ls_alarm_set_callback()` 注册/注销 | 功能通路 | 返回 mask == 年月日时分秒 6 位；`set_callback(cb/NULL)` == 0 | 驱动源码 `rtc_ls.c:289-303` | ⏳ |
| **update_callback_notsup** | `rtc_ls_update_set_callback()` 桩（`#if CONFIG_RTC_UPDATE`，**此前该分支未编入**） | 异常参数 | → `-ENOTSUP` | 驱动源码 `rtc_ls.c:307-312` | ⏳ |
| **drift_measure** | `rtc_ls_get_time()` 秒计数 vs `k_uptime_get()` 参考 | 统计/性质 | 10s 内 `drift_ms ∈ ±1000`，实测值 INFO 上报 | 晶振 ppm 级偏差（宽松门限） | ⏳ |

> 状态：`☐` = 既有项（历史记录未回填勾选）；`⏳` = **P1a 新增，仅编译通过，待 P6 首次实测**。

## P1a 新增项说明（2026-09-17，只生成+编译通过，未烧录）

新增 5 项，追加在 `enum rtc_test_id` **尾部**（既有 10 项槽位索引与 token 名不变）。

- **不改 CMakeLists.txt 的既有 `target_compile_definitions`**：新项的 `RTC_EN_*`
  开关在 `rtc_test.c` 内用 `#ifndef` 给默认值 1（仍可从 CMakeLists 传 0 裁掉）。
- **唯一新增的既有配置面**：`prj.conf` 追加 `CONFIG_RTC_UPDATE=y` —— 否则
  `rtc_ls.c` 的 `#if defined(CONFIG_RTC_UPDATE)` 分支与 `rtc_update_set_callback()`
  API 根本不存在（Zephyr `rtc.h` 亦以同一宏门控），`update_callback_notsup` 无法编入。
- 新增项自建前置（各自 `rtc_set_time` / 注册回调后还原为 NULL），互不依赖。
- **跳过项**：
  - `month_end_rollover` / `year_end_rollover` —— **已由既有 `calendar_rollover` 覆盖**
    （`jan31_to_feb1` / `apr30_to_may1` / `dec31_to_jan1` 等 24 个子用例），不重复造。
  - `alarm_multi`（多路 alarm）—— 驱动**只有一路 alarm**：`rtc_ls_alarm_*` 全部
    `ARG_UNUSED(id)`，注释明写 "LS101x only one alarm path"（`rtc_ls.c:278/291`），
    硬件无第二路可比。
- 全项新增耗时约 **+10.2s**（drift_measure 10s + 其余毫秒级）；原 `calendar_rollover`
  占 74s+，回归 timeout=150s 仍有裕量。

## 判定说明

- 无黄金向量；用错误码、往返一致、中断契约、INFO token（§13.4）判定。
- 全项约需 **90–120s**（calendar_rollover 24 子用例 × 3.1s）；回归默认 timeout=150s。
- Harness: `tests/run_test.py`（RTC_TEST token + `dump_results.py` 内存兜底）。

## 修订（2026-09-18，测试有效性审计 A 组 P0-4）

### `calibration_negative` —— 从"只看 ret==0"改为"断言硬件寄存器黄金值"
- **原状**：只断言 `ret==0`，而驱动负值路径
  （`rtc_ls.c:357-368`，唯一出口 `:371 return 0`）**任何分支算错/写 0/写错字段都返 0**
  ⇒ 恒 PASS，无判别力。
- **改**：写入后**直读 RTC 硬件寄存器**并断言黄金值（地址取 DT `rtc` 节点，
  偏移按 `reg_rtcv2_type.h` 的 `reg_rtc_t`：CTRL@0x00 / CALIB@0x04）：
  - `CALIB == 0x36007FFE`
  - `CTRL.CALIB_EN(BIT1) == 1`
- **黄金值独立推导**（不复用驱动公式回读）：
  - `RTC_CALIB_MAX_PPB = 1e9`（`rtc_ls.c:34`）；`cfg->cyc_1hz = DT cyc-1hz = 32768`
    （`zephyr/dts/riscv/linkedsemi/lsqsh.dtsi:967`）
  - `calibration = -1e8` 走负值分支：
    - `floor_custom_divisor(-1e8, 1e9) = -1` ⇒ 传入 `cyc_1hz` 参数 = `32768 + (-1) = 32767`
    - `calib_cyc = 60 + (-1e8 % 1e9)*60/1e9 = 60 + (-6e9/1e9) = 60 - 6 = 54`
  - `rtc_cycle_config()`（`rtc_ls.c:57-58`）写
    `CALIB = (calib_cyc<<24) | ((cyc_1hz-1) & 0xFFFFF) = (54<<24) | 32766 = 0x36007FFE`，
    并置 `CTRL.CALIB_EN`。
- **驱动正确时必过 / 典型失效时必不过**：
  - 正确：按上式写寄存器 ⇒ `CALIB==0x36007FFE`、`CTRL.BIT1==1` ⇒ 必过。
  - 典型失效：`floor_custom_divisor` 少减 1（得 32767 而非 32766）、`calib_cyc` 漏补
    60/符号算错、写 0、只写高位不写低位、忘记置 `CALIB_EN` ⇒ 任一都会使寄存器值偏离
    黄金值 ⇒ 必不过。
- 备注：`RTC_TEST_TGT_INFO_VAR` 同拍上报 `calib_reg`/`ctrl_reg` 实测值（§13.4），
  报告"实际"列可与黄金值对照。

---

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
> 只登记**不由被测代码算出**的预期；没有确定预期的项显式写 `!none`。
>
> ★ 本工程归档 INFO 有两处**已知的信息损失**，直接压缩了可独立校验的面：
> 1. `tests/run_test.py` 的 `INFO_RE` 值字符类是 `[\w.=,\-]+`（**不含 `:`**）⇒
>    `fire1:triggered=1,pending=0,now=...` 这类结构化值被截断成 `fire1`。
> 2. 于是 `alarm_fires` / `calendar_rollover` 的真实证据（回调/pending、set/expect/get）
>    **进不了报告**，只能记 `!none`。修 `INFO_RE` 后这两项即可升级为可校验。

```expect
# 测试项 | 断言 | 来源标签:说明
set_get_roundtrip | delta_s in [0,1] | handcalc:set 后仅 k_sleep(5ms) 再 get，1Hz 计数 ⇒ 秒差必∈{0,1}（★紧于固件的 [0,5]）
time_advances | delta_s in [2,3] | handcalc:sleep 2.1s ⇒ 1Hz 计数器必前进 2~3（★紧于固件的 [1,4]）
calibration_negative | calib_reg == 0x36007FFE; ctrl.b1 == 1 | handcalc:寄存器布局 reg_rtcv2_type.h + (54<<24) 按位或 32766 手工代入（不复用驱动公式回读）
leap_year_dates | y2028_get == 2028-02-29 | handcalc:公历——2028 被 4 整除且不被 100 整除 ⇒ 闰年
alarm_supported_fields | mask == 0x3f; expect == 0x3f | spec:Zephyr rtc.h RTC_ALARM_TIME_MASK_{SECOND..YEAR} 共 6 位（expect 一并校验，防期望被放宽）
update_callback_notsup | ret == -134 | spec:Zephyr ENOTSUP==134（硬件无 update 事件，驱动桩固定返回）
drift_measure | rtc_ms in [9000,11000]; drift_ms in [-1100,1100] | handcalc:rtc_ms 由 1Hz 秒差 ×1000 得来 ⇒ 量化 ±1000ms，叠加 ≤100ms 唤醒抖动（★固件自身的 ±1000 在 up_ms>10000 时反而会边界误判）
invalid_set_args | ret == -EINVAL | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
alarm_set_get | ret == 0 | contract:API 契约（★无 INFO 通道；"回读逐字段相等"未上报 ⇒ 恒为 NO_ACTUAL）
alarm_invalid_args | ret == -EINVAL | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
calibration_null_arg | ret == -EINVAL | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
invalid_set_args_ext | ret == -EINVAL | contract:API 契约（★无 INFO 通道 ⇒ 恒为 NO_ACTUAL）
calibration_roundtrip | ppb == 100000000 | samecode:set→get 同驱动往返，值经驱动自身存储返回（原「预期来源」列自述"公式反推"）⇒ **非独立**，只记录不计入覆盖
alarm_fires | !none | !none:INFO 值被 run_test 正则截断成阶段标记 `fire1`，回调/pending 无证据
calendar_rollover | !none | !none:INFO 值被截断成末段 tag，set/expect/get 无证据
```
