# otbn_delegation_test 测试报告

- 工程: otbn_delegation_test
- case: case_slot
- 日期: 2026-10-22 22:05:45
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）
- 执行标记: CTRL=SUCCESSFUL, TGT=SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (2/2；SKIP 2)

## case_slot (PASS)

> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| s01_sign_verify | ⏭️ SKIP | token | rc=0；r/s 用已知公钥 verify=0 | - |  |
| s02_unregistered | ✅ PASS | token | rc=-ENOENT | - |  |
| s03_curve_mismatch | ✅ PASS | token | rc=-EINVAL | - |  |
| s04_register_validation | ⏭️ SKIP | token | 零私钥 rc≠0；越界与 NULL rc=-EINVAL；空槽注销 rc=-ENOENT；注册后注销 rc=0，再注销… | - |  |

## 汇总

OVERALL: PASS
