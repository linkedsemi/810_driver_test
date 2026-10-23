# emmc_test 测试报告

- 工程: emmc_test
- case: case_block_driver
- 日期: 2026-10-22 20:04:35
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 执行标记: SUCCESSFUL
- 内存兜底: 不适用（本工程无内存兜底）
- 结果: PASS (12/12)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| guard_matches_driver_behavior | ✅ PASS | token | - | - |  |
| host_props_match_devicetree | ✅ PASS | token | - | - |  |
| disk_ioctl_sector_count | ✅ PASS | token | - | - |  |
| disk_ioctl_sector_size | ✅ PASS | token | - | - |  |
| disk_present_and_init | ✅ PASS | token | - | - |  |
| read_aligned_matches_unaligned | ✅ PASS | token | - | - |  |
| read_unknown_drive_rejected | ✅ PASS | token | - | - |  |
| write_read_scratch_multiblock | ✅ PASS | token | - | - |  |
| write_read_scratch_single | ✅ PASS | token | - | - |  |
| request_data_invalid_opcode_notsup | ✅ PASS | token | - | - |  |
| request_mutex_not_leaked | ✅ PASS | token | - | - |  |
| set_io_clock_out_of_range_not_silent | ✅ PASS | token | - | - |  |

## 汇总

OVERALL: PASS
