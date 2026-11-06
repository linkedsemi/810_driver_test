# crypto_api_test_default 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | crypto_api_test |
| case | case_default |
| 执行标记 | SUCCESSFUL |
| 内存兜底 | 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。 |
| 报告 | [crypto_api_test_report.md](../crypto_api_test_case_default/crypto_api_test_report.md) |
| 结果 | FAIL (42/44) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| hash_caps_sha256 | ❌ FAIL | token | caps 含 **RAW_KEY**（wrapper 断言要求，见 #158）/ SYNC_OPS / SEPARATE… | 串口先打印 `ASSERTION FAIL [(tmp & ((1UL<<0)\|\|(1UL<<1))) != 0] @ zephyr/include/zephyr/crypto/crypto.h:120 : Driver should support at least one key type: RAW/Opaque`，随后 `mcause: 11, Environment call from M-mode` + `ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0`（serial_TGT.log:81-116），ztest 仍打印 `PASS` | CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0`（归档第 113 行；mcause: 11, Environment call from M-mode）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 | serial_TGT.log:81-116 / :122-151 |
| hash_caps_sha512 | ❌ FAIL | token | caps 含 **RAW_KEY**（wrapper 断言要求，见 #158）/ SYNC_OPS / SEPARATE… | 同 sha256：`ASSERTION FAIL ... crypto.h:120 Driver should support at least one key type: RAW/Opaque` → `mcause: 11` → `ZEPHYR FATAL ERROR 4`（serial_TGT.log:122-151） | CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0`（归档第 151 行；mcause: 11, Environment call from M-mode）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 | serial_TGT.log:81-116 / :122-151 |

## 归因

**B 用例/驱动边界（本批不深挖，批量路径边界）**：失败发生在测试用例对 `crypto.h:120`
caps 宏的断言内（hash 驱动 caps 未同时置 RAW/Opaque key 类型位），断言触发内核
`__ASSERT` → `mcause 11` → `FATAL ERROR 4`；ztest 线程被打断后仍打印 PASS，故按
「任何不确定不产生 PASS」判 FAIL。指向 **驱动 caps 上报 或 用例 caps 断言口径**，
二者之一；本次全量路径不做定位（qsh-test-compare §4.1 批量边界）。
缺证据：该 crypto 驱动 caps 寄存器/返回值的实测值（需 readelf/断点或驱动日志）。

## 备注

与 `KNOWN_ISSUES` #158（crypto caps RAW_KEY wrapper 断言）同族；本次为 2026-11-04 首轮全量复现。
