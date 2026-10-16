# dma_test 测试报告

- 工程: dma_test
- case: case_destructive
- 日期: 2026-10-12 21:31:41
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 执行标记: FAILED
- 内存兜底: 不适用（本工程无内存兜底）
- 结果: FAIL (4/9)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| test_dma_abort_mid_transfer | ✅ PASS | token | - | - |  |
| test_dma_block_too_large | ✅ PASS | token | - | - |  |
| test_dma_data_size_zero | ✅ PASS | token | - | - |  |
| test_dma_invalid_data_size | ✅ PASS | token | - | - |  |
| test_dma_large_block | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_destructive_test.c: 271: dma_destructive_test_dma_large_block: (ret not equal to 0) |
| test_dma_lli_pool_exhaust | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_destructive_test.c: 332: dma_destructive_test_dma_lli_pool_exhaust: (ret not equal to -EINVAL) |
| test_dma_one_byte | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_destructive_test.c: 230: dma_destructive_test_dma_one_byte: (ret not equal to 0) |
| test_dma_self_copy_rejected | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_destructive_test.c: 193: dma_destructive_test_dma_self_copy_rejected: (ret not equal to -EINVAL) |
| test_dma_suspend_resume_guards | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/dma_test/cpu1_dma_test/src/dma_destructive_test.c: 444: dma_destructive_test_dma_suspend_resume_guards: (ret not equal to -EINVAL) |

## 汇总

OVERALL: FAIL
