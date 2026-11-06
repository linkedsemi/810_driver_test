# i3c_dualcore_test_pure_i3c_12m5 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | i3c_dualcore_test |
| case | case_pure_i3c_12m5 |
| 执行标记 | CTRL=FAILED, TGT=None |
| 内存兜底 | 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。 |
| 报告 | [i3c_dualcore_test_report.md](../i3c_dualcore_test_case_pure_i3c_12m5/i3c_dualcore_test_report.md) |
| 结果 | FAIL (34/35；失败 1) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| write_delay_read_16 | ❌ FAIL | token | 25M 源窗口 6000-7100ns 漂移（±9%，5 轮）；NACK 边界 = 窗口+~0.1µs（stop 122… | CTRL:critical_delay_ns=1600, first_short_step=33, master_stop_step=56, nack_recover_ok=1 | CTRL:WEST_TOPDIR/810_driver_test/projects/i3c_dualcore_test/cpu0_i3c_controller/src/i3c_controller_test.c: 201: ctrl_test_end: (ctrl_failed is true) | serial_CTRL.log（见「实际」列行号） |

## 归因

**B 用例/驱动（本批不深挖）**：`write_delay_read_16` 实测 `critical_delay_ns=1600`，
远低于用例配置窗口 `CONFIG_I3C_TEST_WDR_MIN_CRITICAL_NS=5500 / MAX=8000`。窗口中间值
是 25 MHz 时钟源下 5 轮实测 6000-7100 ns 漂移 ±9% 定的；1600 ns 约为其 1/4，说明该
轮 master 间隙/等待期显著变短（或测量点提前），未触及 NACK 边界（first_short_step=33,
master_stop_step=56, nack_recover_ok=1）。指向**用例时序测量窗口 vs 本轮时序**，
不能据此判驱动回归。
缺证据：示波器/逻辑分析仪波形、本轮 I3C 时钟源实际频率。

## 备注

同批 `case_pure_i3c_12m5` / `case_mixed_1m_12m5` 各有 1 项 FAIL，均为单点、非整组 0/35 形貌，
与既往 i3c 16 B 边界/FIFO 补填类偶发（G-ITEST-MERGED-I3C-16B-INTERMITTENT）同族，本批不做定位。
