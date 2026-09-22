# emmc_test 测试报告

- 工程: emmc_test
- case: case_block_driver
- 日期: 2026-09-22 18:01:01
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 执行标记: FAILED
- 内存兜底: 不适用（本工程无内存兜底）
- 结果: FAIL (8/10)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| disk_ioctl_sector_count | ✅ PASS | token | - | - |  |
| disk_ioctl_sector_size | ✅ PASS | token | - | - |  |
| disk_present_and_init | ✅ PASS | token | - | - |  |
| read_aligned_matches_unaligned | ✅ PASS | token | - | - |  |
| read_unknown_drive_rejected | ✅ PASS | token | - | - |  |
| write_read_scratch_multiblock | ✅ PASS | token | - | - |  |
| write_read_scratch_single | ✅ PASS | token | - | - |  |
| request_data_invalid_opcode_notsup | ✅ PASS | token | - | - |  |
| request_mutex_not_leaked | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/emmc_test/cpu1_emmc_block_test/src/emmc_block_test.c: 460: zz_emmc_block_edge_request_mutex_not_leaked: (jret is non-zero) |
| set_io_clock_out_of_range_not_silent | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/emmc_test/cpu1_emmc_block_test/src/emmc_block_test.c: 496: zz_emmc_block_edge_set_io_clock_out_of_range_not_silent: (ret equal to 0) |

## 汇总

OVERALL: FAIL
