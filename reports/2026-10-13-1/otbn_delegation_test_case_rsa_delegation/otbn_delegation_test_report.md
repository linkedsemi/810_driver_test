# otbn_delegation_test 测试报告

- 工程: otbn_delegation_test
- case: case_rsa_delegation
- 日期: 2026-10-13 22:59:22
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）
- 执行标记: CTRL=SUCCESSFUL, TGT=SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (3/3；SKIP 1)

## case_rsa_delegation (PASS)

> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| r01_rsa_modexp_roundtrip_2048 | ✅ PASS | token | `c=m^e mod n` rc=0；`m'=c^d mod n` rc=0；**`m'==m` 逐字节** | - |  |
| r02_rsa_modexp_roundtrip_4096 | ✅ PASS | token | 同上 | - |  |
| r03_reject_unsupported_modulus | ✅ PASS | token | 1024/1536 位 rc≠0（客户端 `-EINVAL` / 服务端 `-ENOTSUP`） | - |  |
| r90_rsa_mailbox_bound | ⏭️ SKIP | token | 返回且 rc≠0，且耗时 ≈ 10s 总线超时（`OTBN_DELEG_TIMEOUT_RSA_MS`）⇒ 证明 RSA… | - |  |

## 汇总

OVERALL: PASS
