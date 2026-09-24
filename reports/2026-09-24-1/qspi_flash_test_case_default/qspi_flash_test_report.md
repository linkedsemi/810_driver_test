# qspi_flash_test 测试报告

- 工程: qspi_flash_test
- case: case_default
- 日期: 2026-09-24 00:47:01
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 执行标记: SUCCESSFUL
- 内存兜底: 不适用（本工程无内存兜底）
- 结果: PASS (26/26)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| calib_status_expected | ✅ PASS | token | - | - |  |
| calib_status_null | ✅ PASS | token | - | - |  |
| set_freq_above_max_rejected | ✅ PASS | token | - | - |  |
| set_freq_below_min_rejected | ✅ PASS | token | - | - |  |
| set_freq_boundary_6m25_accepted | ✅ PASS | token | - | - |  |
| set_freq_zero_silent | ✅ PASS | token | - | - |  |
| erase_non_sector_multiple_rejected | ✅ PASS | token | - | - |  |
| erase_sector_all_ff | ✅ PASS | token | - | - |  |
| erase_unaligned_addr_rejected | ✅ PASS | token | - | - |  |
| read_past_end_rejected | ✅ PASS | token | - | - |  |
| write_past_end_rejected | ✅ PASS | token | - | - |  |
| write_read_len1 | ✅ PASS | token | - | - |  |
| write_read_len255 | ✅ PASS | token | - | - |  |
| write_read_len256 | ✅ PASS | token | - | - |  |
| write_read_len4096 | ✅ PASS | token | - | - |  |
| write_read_len8192 | ✅ PASS | token | - | - |  |
| mode_222_rejected | ✅ PASS | token | - | - |  |
| raw_read_1_1_1_ok | ✅ PASS | token | - | - |  |
| raw_read_1_1_4_ok | ✅ PASS | token | - | - |  |
| raw_read_1_4_4_ok | ✅ PASS | token | - | - |  |
| spi_transceive_notsup | ✅ PASS | token | - | - |  |
| stg_cmd_wren_ok | ✅ PASS | token | - | - |  |
| stg_read_chunked_over_4096 | ✅ PASS | token | - | - |  |
| stg_reg_read_len_4097_rejected | ✅ PASS | token | - | - |  |
| stg_write_len_4096_accepted | ✅ PASS | token | - | - |  |
| stg_write_len_4097_rejected | ✅ PASS | token | - | - |  |

## 汇总

OVERALL: PASS
