## case_pure_i3c_12m5: FAIL
  (mem-unavailable: 内存兜底未生效，MISSING 项按 FAIL 判定)
  内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
| Test | Controller | Target | Result | Source | INFO(actual) | Reason |
|------|------------|--------|--------|--------|--------------|--------|
| api_neg_paths | PASS | PASS | PASS | token |  |  |
| back_to_back_8x8 | FAIL | FAIL | FAIL | token |  | CTRL:WEST_TOPDIR/810_driver_test/projects/i3c_dualcore_test/cpu0_i3c_controller/src/i3c_controller_test.c: 201: ctrl_test_end: (ctrl_failed is true) |
| big_256 | PASS | PASS | PASS | token |  |  |
| ccc_basic | PASS | PASS | PASS | token |  |  |
| ccc_rapid_8 | PASS | PASS | PASS | token |  |  |
| daa_discovery | PASS | PASS | PASS | token |  |  |
| hot_join | PASS | PASS | PASS | token |  |  |
| ibi | PASS | PASS | PASS | token |  |  |
| ibi_neg_paths | PASS | PASS | PASS | token |  |  |
| multi_msg_short_read | PASS | PASS | PASS | token |  |  |
| nack_bad_addr | PASS | PASS | PASS | token |  |  |
| read_32_direct | PASS | PASS | PASS | token |  |  |
| read_master_abort | PASS | PASS | PASS | token |  |  |
| read_underrun_16_32 | PASS | PASS | PASS | token |  |  |
| read_underrun_8_16 | PASS | PASS | PASS | token |  |  |
| read_underrun_stress | FAIL | FAIL | FAIL | token |  | CTRL:WEST_TOPDIR/810_driver_test/projects/i3c_dualcore_test/cpu0_i3c_controller/src/i3c_controller_test.c: 201: ctrl_test_end: (ctrl_failed is true) |
| setaasa | PASS | PASS | PASS | token |  |  |
| setdasa | PASS | PASS | PASS | token |  |  |
| short_read_then_nack | PASS | PASS | PASS | token |  |  |
| write_100 | PASS | PASS | PASS | token |  |  |
| write_8 | PASS | PASS | PASS | token |  |  |
| write_dummy_read_16 | PASS | PASS | PASS | token | CTRL:dummy_max_ok=128 |  |
| write_dummy_read_len | PASS | PASS | PASS | token | CTRL:max_len_ok=64 |  |
| write_indexed_read_16 | PASS | PASS | PASS | token |  |  |
| write_read_15 | PASS | PASS | PASS | token |  |  |
| write_read_16 | PASS | PASS | PASS | token |  |  |
| write_read_17 | PASS | PASS | PASS | token |  |  |
| write_read_1 | PASS | PASS | PASS | token |  |  |
| write_read_32 | PASS | PASS | PASS | token |  |  |
| write_stop_read_15 | PASS | PASS | PASS | token |  |  |
| write_stop_read_16 | PASS | PASS | PASS | token |  |  |
| write_stop_read_17 | PASS | PASS | PASS | token |  |  |
| write_stop_read_1 | PASS | PASS | PASS | token |  |  |
| write_stop_read_32 | PASS | PASS | PASS | token |  |  |
EXECUTION: CTRL=FAILED, TGT=None

================================================================================
OVERALL: FAIL
================================================================================

