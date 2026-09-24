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
| cmd_idx_accessors | ❌ FAIL | token | CMD 与 DUMMY_CYCLE 按 idx 读写往返一致；DUMMY_CYCLE 为 5 bit → `0xff`… | slot5_raw=0x77, cmd=0x77, raw_dummy=0, get_dummy=0（CMD 往返一致但 DUMMY 读回 0，断言期望 9） | WEST_TOPDIR/810_driver_test/projects/spif_test/cpu1_spif_test/src/spif_test.c: 341: spif_cmd_idx_accessors: (dummy not equal to 9) | `serial_TGT.log:38`（INFO 实测值）、`serial_TGT.log:40`（Assertion）、`serial_TGT.log:42`（FAIL 行） |

## 归因

**已知基线（已裁定为设计，非缺陷）** —— 任务书裁定 `spif_test_case_default` = `FAIL 17/18`，`cmd_idx_accessors` **保持 FAIL 是设计**。

- 与 `reports/2026-09-23-1` 逐项一致（同 FAIL 项、同计数 17/18）。
- CMD idx 往返正常（`slot5_raw=cmd=0x77`），仅 DUMMY_CYCLE 字段读回 0 与用例期望值 9 不符 —— 判据与实现的分歧已由任务书定为基线，本批不重新裁定。

## 备注

- 与 `reports/2026-09-23-1` 同名 case 结果一致（FAIL 17/18，`cmd_idx_accessors`）。
- 属「已裁定的已知基线」，不得据本结果判为回归。
