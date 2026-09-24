# spi_test_default 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | spi_test |
| case | case_default |
| 执行标记 | FAILED |
| 内存兜底 | 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。 |
| 报告 | [spi_test_report.md](../spi_test_case_default/spi_test_report.md) |
| 结果 | FAIL (18/19) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| link_probe | ❌ FAIL | token | 两设备 `device_is_ready` 真；8 B 双向逐字节相等；`master_ret==0`；`slave_r…` | master_ret=0, slave_ret=-119, state=0（timed_out=1, mrx0=ff, stx0=c0, srx0=00, mtx0=30 → BAD） | WEST_TOPDIR/810_driver_test/projects/spi_test/cpu1_spi_test/src/spi_loopback.c: 454: spi_loopback_link_probe: (lb_link_state not equal to LB_LINK_OK) | `serial_TGT.log:91`（INFO 实测值）、`serial_TGT.log:95`（`link probe: … -> BAD`）、`serial_TGT.log:96`（FAIL 行） |

## 归因

**环境（物理接线）** —— `slave_ret=-119`（`-ENOTCONN` 类）+ `timed_out=1`、target 侧收帧全 `ff`/`00`：**外部回环跳线未接**，属物理问题，非软件/驱动缺陷。

- **预期内已知基线**：任务书裁定 `spi_test_case_default` = `FAIL 18/19`（`link_probe`）。
- 与 `reports/2026-09-23-1` 逐项一致（同 FAIL 项、同计数）。

## 备注

- 与 `reports/2026-09-23-1` 同名 case 结果一致（FAIL 18/19，`link_probe`）。
- 恢复需**接上外部回环跳线**后复测（硬件动作，非软件可修）。
