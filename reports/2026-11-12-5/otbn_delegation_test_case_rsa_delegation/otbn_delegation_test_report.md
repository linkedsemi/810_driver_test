# otbn_delegation_test 测试报告

- 工程: otbn_delegation_test
- case: case_rsa_delegation
- 日期: 2026-11-13 01:53:35
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）
- 执行标记: CTRL=SUCCESSFUL, TGT=SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (3/3；SKIP 2)

## case_rsa_delegation (PASS)

> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| r01_rsa_modexp_roundtrip_2048 | ✅ PASS | token | `c=m^e mod n` rc=0；`m'=c^d mod n` rc=0；**`m'==m` 逐字节** | - |  |
| r02_rsa_modexp_roundtrip_4096 | ✅ PASS | token | 同上 | - |  |
| r03_reject_unsupported_modulus | ✅ PASS | token | 768/1792 位 rc≠0（客户端 `MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE`，即… | - |  |
| r04_rsa_size_boundary | ⏭️ SKIP | token | 全部 rc≠0（1024/1536 被 delegation 白名单拒绝；非整字节尺寸不得截断成合法值） | CTRL:rejected=10 |  |
| r90_rsa_mailbox_bound | ⏭️ SKIP | token | 返回且 rc≠0，且耗时 ≈ 10s 总线超时（`OTBN_DELEG_TIMEOUT_RSA_MS`）⇒ 证明 RSA… | - |  |

## 汇总

OVERALL: PASS
