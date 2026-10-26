# spif_test_default 失败分析

> 骨架由 `common/failures_report.py` 生成；**归因已由本轮 runner 按 §4.1 补齐**。

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
| cmd_idx_accessors | ❌ FAIL | token | CMD 与 DUMMY_CYCLE 按 idx 读写往返一致；DUMMY_CYCLE 为 5 bit → `0xff`… | slot5_raw=0x77, cmd=0x77, raw_dummy=0, get_dummy=0 | WEST_TOPDIR/810_driver_test/projects/spif_test/cpu1_spif_test/src/spif_test.c: 341: spif_cmd_idx_accessors: (dummy not equal to 9) | `serial_*.log`:38 `INFO cmd_idx_accessors: ... raw_dummy=0`；:40 `Assertion failed spif_test.c:341` |

## 归因

**类别：驱动（既有已知失败），非本轮回归。**

- `cmd_idx_accessors`：`spif_cmd_idx_accessors` 断言 `dummy != 9`，实测
  `slot5_raw=0x77, cmd=0x77, raw_dummy=0, get_dummy=0`（DUMMY_CYCLE 未按 idx 生效）。
- 与基线 2026-09-24-1（17/18）同项同因 ⇒ 已知驱动问题（任务书亦标注为已知 FAIL）。

## 备注

- 与基线一致；未在本轮修复代码。
