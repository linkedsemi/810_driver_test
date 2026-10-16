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
| link_probe | ❌ FAIL | token | 两设备 `device_is_ready` 真；8 B 双向逐字节相等；`master_ret==0`；`slave_r… | master_ret=0, slave_ret=-119, state=0 | WEST_TOPDIR/810_driver_test/projects/spi_test/cpu1_spi_test/src/spi_loopback.c: 454: spi_loopback_link_probe: (lb_link_state not equal to LB_LINK_OK) | ⚠️ 待补 |

## 归因

**C/B 物理基线**（非驱动缺陷）。`link_probe`：`master_ret=0, slave_ret=-119, state=0`（LB_LINK_NOT_OK）。已知基线 = **外部回环（loopback）跳线未接**（09-23 A3/D5 结论；`OPEN_ITEMS` O-18 已登记，陈年）。master 侧发出但 slave 侧收不到 ⇒ 物理回环断路。

## 备注

长期停在 18/19，属**已知基线**（`spi_test/tests/COVERAGE.md`）。非本轮新增，不因历史基线收兵 —— 已核最近改动窗口（spi_ls.c 本轮无改动）。
