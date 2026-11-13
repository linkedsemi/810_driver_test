# otbn_delegation_test 测试报告

- 工程: otbn_delegation_test
- case: case_throughput_iso_local_hwrng
- 日期: 2026-11-13 02:31:29
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）
- 执行标记: CTRL=SUCCESSFUL, TGT=SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (1/1)

## case_throughput_iso_local_hwrng (PASS)

> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| b01_throughput | ✅ PASS | token | - | CTRL:bench=0, role=hold |  |

## 汇总

OVERALL: PASS
