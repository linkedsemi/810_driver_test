# mbedtls_test_hw_all 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | mbedtls_test |
| case | case_hw_all |
| 执行标记 | FAILED |
| 内存兜底 | 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。 |
| 报告 | [mbedtls_test_report.md](../mbedtls_test_case_hw_all/mbedtls_test_report.md) |
| 结果 | FAIL (38/42) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| aes128_ctr_carry | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | algo=aes-128-ctr, carry=word_boundary, single_ok=0, segmented_ok=1, roundtrip_ok=0 | WEST_TOPDIR/810_driver_test/projects/mbedtls_test/cpu1_mbedtls_test/src/cipher_test.c: 1024: cipher_aes128_ctr_carry: (single_ok is false) | ⚠️ 待补 |
| aes_alt_null_params | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | setkey_null_ctx=-33, setkey_null_key=-33 | CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 0: CPU exception on CPU 0`（归档第 86 行；mcause: 5, Load access fault）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 | ⚠️ 待补 |
| aes_cbc_len_contract | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | zero_len_ok=0 | CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0`（归档第 125 行；mcause: 11, Environment call from M-mode）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 | ⚠️ 待补 |
| aes_concurrent_ctx | ❌ FAIL | token | ⚠️ 待补（报告未给，需查 `serial_*.log`） | bytes=8192, interleaved_eq_single_threaded=0 | WEST_TOPDIR/810_driver_test/projects/mbedtls_test/cpu1_mbedtls_test/src/cipher_test.c: 1155: cipher_aes_concurrent_ctx: (same is false) | ⚠️ 待补 |

## 归因

**A 驱动（mbedtls AES ALT，在修中）**。4 项全为 AES ALT 专属用例，**build 已含修复**（`aes_alt.c.obj` mtime 10-12 21:01 > 源 `aes_alt.c` mtime 10-10 19:09 ⇒ 非陈旧构建）：
- `aes128_ctr_carry`：`single_ok=0, segmented_ok=1` ⇒ CTR 单调用字进位路径错（分段对、整体错）；
- `aes_alt_null_params`：`mcause 5 Load access fault` ⇒ **NULL 守卫仍不全**（仍有解引用）；
- `aes_cbc_len_contract`：`mcause 11` → `Kernel panic` ⇒ `length==0` 契约路径 panic；
- `aes_concurrent_ctx`：`interleaved_eq_single_threaded=0` ⇒ 共享外设互斥锁未使交错==单线程。
对应台账 `OPEN_ITEMS` **O-41**（CTR 流式契约）/ **O-42**（CBC 断言代错误码、len==0 越界、共享寄存器无互斥、NULL 加固）。修复在 `modules/crypto/mbedtls/library/aes_alt.c` 但**未提交、且不完整**。

## 备注

与今日单跑会话 `reports/2026-10-12-1/mbedtls_test_case_hw_all`（38/42）**逐项计数相同** ⇒ 稳定复现、非 flaky。★ 两项（aes_alt_null_params / aes_cbc_len_contract）属 **O-43 型「异常后假 PASS」**：串口仍打 PASS、实为 CPU 异常打断，本框架 R9 已正确判 FAIL。
