# dma_test 测试报告

- 工程: dma_test
- case: case_default
- 日期: 2026-09-22 17:23:31
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 执行标记: FAILED
- 内存兜底: 不适用（本工程无内存兜底）
- 结果: FAIL (1/7)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| test_dma_channel_config_stop | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_test.c: 210: dma_test_dma_channel_config_stop: (dma_config(dma_dev, DMA_CHANNEL, &cfg) is non-zero) |
| test_dma_dmac2_transfer | ✅ PASS | token | - | - |  |
| test_dma_invalid_channel | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_test.c: 246: dma_test_dma_invalid_channel: (ret not equal to 0) |
| test_dma_lli_chain | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_test.c: 337: dma_test_dma_lli_chain: (ret not equal to 0) |
| test_dma_loop_transfer | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_test.c: 81: dma_test_dma_loop_transfer: (ret not equal to 0) |
| test_dma_multi_channel_concurrent | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_test.c: 395: dma_test_dma_multi_channel_concurrent: (dma_config(dma_dev, 0, &cfg0) is non-zero) |
| test_dma_transfer_ones_zeros | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_test.c: 134: dma_test_dma_transfer_ones_zeros: dma_config(dma_dev, DMA_CHANNEL, &cfg) is non-zero |

## 汇总

OVERALL: FAIL
