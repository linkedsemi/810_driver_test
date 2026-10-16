# dma_test_default 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | dma_test |
| case | case_default |
| 执行标记 | FAILED |
| 内存兜底 | 不适用（本工程无内存兜底） |
| 报告 | [dma_test_report.md](../dma_test_case_default/dma_test_report.md) |
| 结果 | FAIL (1/7) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| test_dma_channel_config_stop | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_test.c: 210: dma_test_dma_channel_config_stop: (dma_config(dma_dev, DMA_CHANNEL, &cfg) is non-zero) | ⚠️ 待补 |
| test_dma_invalid_channel | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_test.c: 246: dma_test_dma_invalid_channel: (ret not equal to 0) | ⚠️ 待补 |
| test_dma_lli_chain | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_test.c: 337: dma_test_dma_lli_chain: (ret not equal to 0) | ⚠️ 待补 |
| test_dma_loop_transfer | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_test.c: 81: dma_test_dma_loop_transfer: (ret not equal to 0) | ⚠️ 待补 |
| test_dma_multi_channel_concurrent | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_test.c: 395: dma_test_dma_multi_channel_concurrent: (dma_config(dma_dev, 0, &cfg0) is non-zero) | ⚠️ 待补 |
| test_dma_transfer_ones_zeros | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | ⚠️ 待补（报告未给，需查 `serial_*.log`） | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_test.c: 134: dma_test_dma_transfer_ones_zeros: dma_config(dma_dev, DMA_CHANNEL, &cfg) is non-zero | ⚠️ 待补 |

## 归因

**A 驱动 · `linkedsemi` 上的真实未修缺陷**（2026-10-13 用户裁定：目标分支 = `linkedsemi` 唯一，`driver_test` 分支内容**舍弃** ⇒ **不再按「分支差异」豁免**）。5/6 项 FAIL 同因：`dma_config()` 返回非零。串口证据 `reports/2026-10-12-2/dma_test_case_default/serial_TGT.log`:4 `<err> dma_dw_common: dmac@40041000: channel 0 must be inactive to reconfigure, currently 3` ⇒ `state=3=DW_DMA_ACTIVE` 卡死。

**根因**：当前分支 `linkedsemi`（HEAD `680604e7e9c`）的 `zephyr/drivers/dma/dma_dw_common.c` **缺 09-23 的 TFR-unmask 修复**（`DW_MASK_TFR` unmask 仍在 `else` 分支内，blame=2022 上游原貌）；该修复在 **`driver_test`** 分支（`84e1a82c26f`/`6af8087e3ba`），`git merge-base --is-ancestor` = NO。

## 备注

与 09-23（`reports/2026-09-23-1` 记 7/7 PASS，`driver_test` 分支）对比 = PASS→FAIL。**2026-10-13 用户裁定**：目标分支 = `linkedsemi` 唯一，`driver_test` 内容舍弃 ⇒ 本项是 **`linkedsemi` 上的真实未修缺陷**（原分支的修复已不可用），须**在 `linkedsemi` 上重新修复**。
