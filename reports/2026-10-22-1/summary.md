# 810_driver_test 汇总（2026-10-22-1）

| 测试 | 结果 | 通过/总数 |
|---|---|---|
| [dma_test_case_default](dma_test_case_default/dma_test_report.md) | PASS | 7/7 |
| [dma_test_case_destructive](dma_test_case_destructive/dma_test_report.md) | PASS | 9/9 |
| [emmc_test_case_block_driver](emmc_test_case_block_driver/emmc_test_report.md) | PASS | 12/12 |
| [emmc_test_case_block_pio](emmc_test_case_block_pio/emmc_test_report.md) | PASS | 12/12 |

**共 4 个测试，通过 4 个，失败 0 个**

> DMA：`case_default` 单项 panic 归因留档于
> [`failures/dma_test_default.md`](failures/dma_test_default.md)（测试侧缺 `burst_length`，已修复并复测 PASS；驱动未改）。
>
> eMMC：最终被测 HEAD `a9aebb55833`（含 `0668549e51f`，`clock == 0` 恢复合法）下两 case 均
> **PASS 12/12**。`disk_access_init=0`，`set_io_clock_out_of_range_not_silent` 实测
> `ret=-22`（`-EINVAL`，断言首次真正执行并转 PASS）。中间轮（`f4fe4746114`，`clock == 0`
> 被拒导致卡初始化失败，3/12）作为回归证据留档于
> [`failures/emmc_test_clock0_init_regression.md`](failures/emmc_test_clock0_init_regression.md)；
> 更早一轮（`b30f5d04223`，11/12，越界时钟静默接受）留档于
> [`failures/emmc_test_set_io_clock.md`](failures/emmc_test_set_io_clock.md)。
