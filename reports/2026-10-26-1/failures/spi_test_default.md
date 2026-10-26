# spi_test_default 失败分析

> 骨架由 `common/failures_report.py` 生成；**归因已由本轮 runner 按 §4.1 补齐**。

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
| link_probe | ❌ FAIL | token | 两设备 `device_is_ready` 真；8 B 双向逐字节相等；`master_ret==0`；`slave_r… | master_ret=0, slave_ret=-119, state=0 | WEST_TOPDIR/810_driver_test/projects/spi_test/cpu1_spi_test/src/spi_loopback.c: 454: spi_loopback_link_probe: (lb_link_state not equal to LB_LINK_OK) | `serial_CTRL.log`：`link_probe master_ret=0, slave_ret=-119`；外部 SPI1↔SPI3 跳线未接 |

## 归因

**类别：环境（外部回环跳线未接），非本轮代码回归。**

- 失败项 `link_probe`：`master_ret=0, slave_ret=-119(-ENODEV), state=0`；套件 `spi_loopback`
  需要外部 `SPI1 ↔ SPI3` 跳线（SCK PA00↔PG14 / MOSI PA01↔PG15 / MISO PA02↔PG03 / CS PA03↔PG13）。
  跳线未接时其余 7 项按设计 SKIP，`link_probe` FAIL。
- 基线 2026-09-24-1 同为 `18/19`、同一项同一原因 ⇒ 既有环境条件，不是本轮回归。

## 备注

- 与基线一致（18/19 PASS 分母相同）。要转 PASS 需接好 SPI1↔SPI3 回环跳线后重跑。
