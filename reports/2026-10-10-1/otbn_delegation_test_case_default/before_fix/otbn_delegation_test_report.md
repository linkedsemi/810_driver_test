# otbn_delegation_test 测试报告

- 工程: otbn_delegation_test
- case: case_default
- 日期: 2026-10-10 12:58:34
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）
- 执行标记: CTRL=SUCCESSFUL, TGT=SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (10/10)

## case_default (PASS)

> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| t01_genkey_p256 | ✅ PASS | token | 返回 0；`d` 满足 0<d<n；公钥 Q 通过 `mbedtls_ecp_check_pubkey` | - |  |
| t02_genkey_p384 | ✅ PASS | token | 同上 | - |  |
| t03_genkey_sm2 | ✅ PASS | token | 同上 | - |  |
| t04_sign_p256 | ✅ PASS | token | 返回 0；r、s 均在 (0,n) 区间 | - |  |
| t05_sign_p384 | ✅ PASS | token | 同上 | - |  |
| t06_sign_sm2 | ✅ PASS | token | 同上 | - |  |
| t07_verify_p256 | ✅ PASS | token | 合法签名返回 0；**篡改 r（r+1 mod n）后必须返回非 0** | - |  |
| t08_verify_p384 | ✅ PASS | token | 同上 | - |  |
| t09_verify_sm2 | ✅ PASS | token | 同上 | - |  |
| t10_reject_unsupported_curve | ✅ PASS | token | 白名单外曲线（`MBEDTLS_ECP_DP_NONE`）必须返回非 0 | - |  |

## 汇总

OVERALL: PASS
