# dma_test_destructive 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | dma_test |
| case | case_destructive |
| 执行标记 | FAILED |
| 内存兜底 | 不适用（本工程无内存兜底） |
| 报告 | [dma_test_report.md](../dma_test_case_destructive/dma_test_report.md) |
| 结果 | FAIL (4/9) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| test_dma_large_block | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_destructive_test.c: 271: dma_destructive_test_dma_large_block: (ret not equal to 0) | ⚠️ 待补 |
| test_dma_lli_pool_exhaust | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_destructive_test.c: 332: dma_destructive_test_dma_lli_pool_exhaust: (ret not equal to -EINVAL) | ⚠️ 待补 |
| test_dma_one_byte | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_destructive_test.c: 230: dma_destructive_test_dma_one_byte: (ret not equal to 0) | ⚠️ 待补 |
| test_dma_self_copy_rejected | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_destructive_test.c: 193: dma_destructive_test_dma_self_copy_rejected: (ret not equal to -EINVAL) | ⚠️ 待补 |
| test_dma_suspend_resume_guards | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_destructive_test.c: 444: dma_destructive_test_dma_suspend_resume_guards: (ret not equal to -EINVAL) | ⚠️ 待补 |

## 归因

**A 驱动 · `linkedsemi` 上的真实未修缺陷**（2026-10-13 用户裁定：`driver_test` 分支内容舍弃 ⇒ 不按「分支差异」豁免；见 `dma_test_default.md` 同款改写）。`large_block`/`one_byte` 失败 = `dma_config`/传输返非 0（同状态机卡死）；`self_copy_rejected`/`lli_pool_exhaust`/`suspend_resume_guards` 期望 `-EINVAL` 而返回非 `-EINVAL` = 缺 09-23 的「source==dest 显式拒绝」修复（`dma_dw_common.c`，`driver_test` 分支）。

## 备注

同 dma_test_default（**2026-10-13 裁定后：真实未修缺陷，须在 `linkedsemi` 上重新修复**）。证据 `dma_test_case_destructive/serial_TGT.log`（`:193/:230/:271/:332/:444`）。
