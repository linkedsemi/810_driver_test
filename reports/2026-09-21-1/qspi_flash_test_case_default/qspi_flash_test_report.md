# qspi_flash_test 测试报告

- 工程: qspi_flash_test
- case: case_default
- 日期: 2026-09-21 13:25:51
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 执行标记: FAILED
- 内存兜底: 不适用（本工程无内存兜底）
- 结果: FAIL (14/26)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| calib_status_expected | ✅ PASS | token | - | - |  |
| calib_status_null | ✅ PASS | token | - | - |  |
| set_freq_above_max_rejected | ✅ PASS | token | - | - |  |
| set_freq_below_min_rejected | ✅ PASS | token | - | - |  |
| set_freq_boundary_6m25_accepted | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c: 682: qspi_flash_calib_set_freq_boundary_6m25_accepted: (ret_init is non-zero) |
| set_freq_zero_silent | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c: 698: qspi_flash_calib_set_freq_zero_silent: (ret_init is non-zero) |
| erase_non_sector_multiple_rejected | ✅ PASS | token | - | - |  |
| erase_sector_all_ff | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c: 271: qspi_flash_rw_erase_sector_all_ff: (offender not equal to -1) |
| erase_unaligned_addr_rejected | ✅ PASS | token | - | - |  |
| read_past_end_rejected | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c: 397: qspi_flash_rw_read_past_end_rejected: (ret not equal to -EINVAL) |
| write_past_end_rejected | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c: 405: qspi_flash_rw_write_past_end_rejected: (ret not equal to -EINVAL) |
| write_read_len1 | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c: 289: qspi_flash_rw_write_read_len1: (rbuf not equal to wbuf) |
| write_read_len255 | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c: 306: qspi_flash_rw_write_read_len255: (rbuf not equal to wbuf) |
| write_read_len256 | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c: 324: qspi_flash_rw_write_read_len256: (rbuf not equal to wbuf) |
| write_read_len4096 | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c: 343: qspi_flash_rw_write_read_len4096: (rbuf not equal to wbuf) |
| write_read_len8192 | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c: 370: qspi_flash_rw_write_read_len8192: (rbuf not equal to wbuf) |
| mode_222_rejected | ✅ PASS | token | - | - |  |
| raw_read_1_1_1_ok | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c: 455: qspi_flash_stg_raw_read_1_1_1_ok: (rbuf not equal to wbuf) |
| raw_read_1_1_4_ok | ✅ PASS | token | - | - |  |
| raw_read_1_4_4_ok | ✅ PASS | token | - | - |  |
| spi_transceive_notsup | ✅ PASS | token | - | - |  |
| stg_cmd_wren_ok | ✅ PASS | token | - | - |  |
| stg_read_chunked_over_4096 | ❌ FAIL | token | - | - | WEST_TOPDIR/810_driver_test/projects/qspi_flash_test/cpu1_qspi_flash_test/src/qspi_flash_test.c: 574: qspi_flash_stg_stg_read_chunked_over_4096: (rbuf not equal to wbuf) |
| stg_reg_read_len_4097_rejected | ✅ PASS | token | - | - |  |
| stg_write_len_4096_accepted | ✅ PASS | token | - | - |  |
| stg_write_len_4097_rejected | ✅ PASS | token | - | - |  |

## 汇总

OVERALL: FAIL
