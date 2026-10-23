# otbn_delegation_test 测试报告

- 工程: otbn_delegation_test
- case: case_slot
- 日期: 2026-10-22 21:59:04
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）
- 执行标记: CTRL=SUCCESSFUL, TGT=SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: FAIL (1/2；失败 1；SKIP 2)

## case_slot (FAIL)

> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| s01_sign_verify | ⏭️ SKIP | token | rc=0；r/s 用已知公钥 verify=0 | - |  |
| s02_unregistered | ❌ FAIL | token | rc=-ENOENT | - | TGT:CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 3: Kernel oops on CPU 0`（归档第 47 行；mcause: 11, Environment call from M-mode）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 |
| s03_curve_mismatch | ✅ PASS | token | rc=-EINVAL | - |  |
| s04_register_validation | ⏭️ SKIP | token | 零私钥 rc≠0；越界与 NULL rc=-EINVAL；空槽注销 rc=-ENOENT；注册后注销 rc=0，再注销… | - |  |

## 汇总

OVERALL: FAIL
