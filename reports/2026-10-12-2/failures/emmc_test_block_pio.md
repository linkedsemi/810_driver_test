# emmc_test_block_pio 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | emmc_test |
| case | case_block_pio |
| 执行标记 | FAILED |
| 内存兜底 | 不适用（本工程无内存兜底） |
| 报告 | [emmc_test_report.md](../emmc_test_case_block_pio/emmc_test_report.md) |
| 结果 | FAIL (8/10) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| request_mutex_not_leaked | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/emmc_test/cpu1_emmc_block_test/src/emmc_block_test.c: 460: zz_emmc_block_edge_request_mutex_not_leaked: (jret is non-zero) | ⚠️ 待补 |
| set_io_clock_out_of_range_not_silent | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/emmc_test/cpu1_emmc_block_test/src/emmc_block_test.c: 496: zz_emmc_block_edge_set_io_clock_out_of_range_not_silent: (ret equal to 0) | ⚠️ 待补 |

## 归因

**A 驱动**（同 block_driver：同样两项、同一缺失的 `df4bc2a46e7` 修复）。

## 备注

同 emmc_test_block_driver。
