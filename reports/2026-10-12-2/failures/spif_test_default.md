# spif_test_default 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | spif_test |
| case | case_default |
| 执行标记 | FAILED |
| 内存兜底 | 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。 |
| 报告 | [spif_test_report.md](../spif_test_case_default/spif_test_report.md) |
| 结果 | FAIL (17/18) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| cmd_idx_accessors | ❌ FAIL | token | CMD 与 DUMMY_CYCLE 按 idx 读写往返一致；DUMMY_CYCLE 为 5 bit → `0xff`… | slot5_raw=0x77, cmd=0x77, raw_dummy=0, get_dummy=0 | WEST_TOPDIR/810_driver_test/projects/spif_test/cpu1_spif_test/src/spif_test.c: 341: spif_cmd_idx_accessors: (dummy not equal to 9) | ⚠️ 待补 |

## 归因

**B 硬件字段基线**（非驱动缺陷）。`cmd_idx_accessors`：`slot5_raw=0x77, cmd=0x77, raw_dummy=0, get_dummy=0` ⇒ 索引 5 槽位的 **DUMMY_CYCLE 字段实测不可写**（同行 CMD/EN 均正常）⇒ 该断言在此槽位**构造上不可满足**。见 `OPEN_ITEMS` **O-31**（已结：登记为**已知基线**，与 `spi_test::link_probe` 并列，保持 FAIL 不修）。

## 备注

长期停在 17/18 = 已知基线。已核改动窗口（`spi_filter.c` 本轮无改动）。
