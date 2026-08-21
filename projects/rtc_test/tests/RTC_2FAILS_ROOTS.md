# RTC 驱动 2 项 FAIL 根因总结（2026-08-21 回归）

回归：rtc_test 7/9 PASS，2 FAIL。**均为驱动层问题，非测试代码问题**（测试断言符合
Zephyr RTC API 契约，未改动）。

## 1. set_get_roundtrip FAIL — set_time 载入延迟（ms 级）

- 现象：set 2026-08-21 12:34:56 后立即 get_time 读回复位值 1/1/1 01:01:05
  （reason: `mday 1 != 21; ...; set->get delta -45292s`）。
- 证据链（GDB 断点追踪 + 手动寄存器实验）：
  - GDB halt 手动序列（EN=0 → 写 TIME/CAL → TGGL=1 → EN=1）**立即载入**；
  - 固件运行态 set 后 us 级 get 读旧值；断点停顿（ms 级）后 get 读到新值
    （CUR=0x150d2339）；time_advances（set 后 sleep 2.1s）PASS → **载入延迟在
    us~ms 区间，非秒 tick**；
  - 寄存器写入有效（TIME/CAL 值正确），仅 CURTIME 未立即更新。
- 根因：驱动 `rtc_ls_set_time`（zephyr/drivers/rtc/rtc_ls.c:181-184）TGGL 翻转后
  仅 `k_busy_wait(10)`（10us），**不足以覆盖硬件载入同步**（ms 级），set 返回后
  立即 get 读到旧值。
- 修复方向（驱动层，未实施）：延长 busy_wait 或 set 后轮询 CURTIME 确认载入。
- 通用教训：**RTC set_time 后不可立即 get_time**，需等待载入完成（轮询或 ≥ms 延时）。

## 2. alarm_fires FAIL — CLIC 中断 irq16 未使能

- 现象：+2s 闹钟 4s 内未触发回调，alarm_is_pending 恒 1
  （reason: `alarm callback not fired in 4s; alarm still pending after fire`）。
- 证据链（GDB 手动实验 + CLIC 寄存器读）：
  - 手动写 alarm（TIME=CUR+2s, CAL, ALARM_EN=1）detach 3s 后 **INTR=1**
    （硬件比较器匹配 ✓），但 ALARM_EN 未被 ISR 清 → **ISR 从未执行**；
  - **CLIC IE 寄存器 0xE0801400 = 0x00000000**：irq16（PMU_RTC_IRQN）使能位
    **未置位**（对比 irq112 区 0xE080101C=0x1fc00101 已配置）；
  - INTR 置位但 CLIC 屏蔽 → 中断永远进不了 CPU。
- 根因：RTC 中断使能链路未生效（驱动 init `cfg->irq_config_func` → IRQ_CONNECT +
  irq_enable(16) → `csi_vic_enable_irq`），CLIC bit16 为 0。待查：irq_enable 路径
  是否被调用/写失败（E906 CLIC 经 mireg2 间接访问，drivers 侧 `linkedsemi,riscv,e906-intc`
  无对应 C 驱动文件，走 arch RISC-V 通用 CLIC 路径）。
- 修复方向（驱动/SoC 层，未实施）：确认并修复 irq16 的 CLIC 使能写入。
- 通用教训：**INTR 寄存器置位 ≠ 中断会进 ISR**，先验证 CLIC IE 位；
  测试中断路径前用 GDB 读 CLIC 使能寄存器确认。

## 状态

- 测试代码：**确认无问题**（9 项断言符合 API 契约；回归脚本 trng 样板残留
  `throughput_64k`→`calibration_null_arg` 已修复）。
- 驱动修复：待后续（用户当前不要求深挖）。
