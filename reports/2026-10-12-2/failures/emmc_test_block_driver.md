# emmc_test_block_driver 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | emmc_test |
| case | case_block_driver |
| 执行标记 | FAILED |
| 内存兜底 | 不适用（本工程无内存兜底） |
| 报告 | [emmc_test_report.md](../emmc_test_case_block_driver/emmc_test_report.md) |
| 结果 | FAIL (8/10) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| request_mutex_not_leaked | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/emmc_test/cpu1_emmc_block_test/src/emmc_block_test.c: 460: zz_emmc_block_edge_request_mutex_not_leaked: (jret is non-zero) | ⚠️ 待补 |
| set_io_clock_out_of_range_not_silent | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/emmc_test/cpu1_emmc_block_test/src/emmc_block_test.c: 496: zz_emmc_block_edge_set_io_clock_out_of_range_not_silent: (ret equal to 0) | ⚠️ 待补 |

## 归因

**A 驱动 · `linkedsemi` 上的真实未修缺陷**（2026-10-13 用户裁定：目标分支 = `linkedsemi` 唯一，`driver_test` 内容舍弃 ⇒ **不再按「分支差异」豁免**）。两项 FAIL 恰是 09-23 emmc 修复针对的两项：
- `request_mutex_not_leaked`（`:460 jret is non-zero`）= sdhci 非法 opcode 在锁内早退致 **mutex 泄漏**；
- `set_io_clock_out_of_range_not_silent`（`:496 ret equal to 0`）= **越界时钟静默忽略**。
该修复 = 提交 `df4bc2a46e7`（`sdhci-of-linkedsemi.c`），在 **`driver_test`** 分支，`merge-base --is-ancestor driver_test的该commit linkedsemi` = NO。

## 备注

与 09-23 对比 = PASS→FAIL，性质 = 分支差异。建议：把 emmc 修复合入 linkedsemi。
