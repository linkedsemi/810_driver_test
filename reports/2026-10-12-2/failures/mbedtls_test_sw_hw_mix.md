# mbedtls_test_sw_hw_mix 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | mbedtls_test |
| case | case_sw_hw_mix |
| 执行标记 | FAILED |
| 内存兜底 | 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。 |
| 报告 | [mbedtls_test_report.md](../mbedtls_test_case_sw_hw_mix/mbedtls_test_report.md) |
| 结果 | FAIL (36/40) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| aes128_ctr_carry | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | algo=aes-128-ctr, carry=word_boundary, single_ok=0, segmented_ok=1, roundtrip_ok=0 | WEST_TOPDIR/810_driver_test/projects/mbedtls_test/cpu1_mbedtls_test/src/cipher_test.c: 1024: cipher_aes128_ctr_carry: (single_ok is false) | ⚠️ 待补 |
| aes_alt_null_params | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | setkey_null_ctx=-33, setkey_null_key=-33 | CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 0: CPU exception on CPU 0`（归档第 86 行；mcause: 5, Load access fault）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 | ⚠️ 待补 |
| aes_cbc_len_contract | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | zero_len_ok=0 | CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0`（归档第 125 行；mcause: 11, Environment call from M-mode）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 | ⚠️ 待补 |
| aes_concurrent_ctx | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | bytes=8192, interleaved_eq_single_threaded=0 | WEST_TOPDIR/810_driver_test/projects/mbedtls_test/cpu1_mbedtls_test/src/cipher_test.c: 1155: cipher_aes_concurrent_ctx: (same is false) | ⚠️ 待补 |

## 归因

**A 驱动（mbedtls AES ALT，在修中）** —— 与 case_hw_all 相同的 4 项、相同判据。

## 备注

见 mbedtls_test_hw_all。与单跑会话 -1（36/40）计数相同。
