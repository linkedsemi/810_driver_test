# otbn_delegation_test 测试报告

- 工程: otbn_delegation_test
- case: case_explicit_api
- 日期: 2026-10-14 12:30:51
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）
- 执行标记: CTRL=SUCCESSFUL, TGT=SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (24/24；SKIP 1)

## case_explicit_api (PASS)

> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| t01_genkey_p256 | ✅ PASS | token | rc=0；`0<d<n`；`check_privkey`=0；`check_pubkey`=0；`write_binar… | - |  |
| t02_genkey_p384 | ✅ PASS | token | 同上 | - |  |
| t03_genkey_sm2 | ✅ PASS | token | 同上 | - |  |
| t04_sign_p256 | ✅ PASS | token | rc=0；r,s∈(0,n) | - |  |
| t05_sign_p384 | ✅ PASS | token | 同上 | - |  |
| t06_sign_sm2 | ✅ PASS | token | 同上 | - |  |
| t07_verify_p256 | ✅ PASS | token | 合法=0；篡改 r(`r+1 mod n`)≠0 | - |  |
| t08_verify_p384 | ✅ PASS | token | - | - |  |
| t09_verify_sm2 | ✅ PASS | token | - | - |  |
| t10_reject_unsupported_curve | ✅ PASS | token | 白名单外曲线 rc≠0 | - |  |
| t11_sign_blen_lt_n | ✅ PASS | token | rc=0；r,s∈(0,n) | - |  |
| t12_sign_blen_gt_n | ✅ PASS | token | 同上 | - |  |
| t13_verify_r_out_of_range | ✅ PASS | token | rc≠0 | - |  |
| t14_verify_s_zero | ✅ PASS | token | rc≠0 | - |  |
| t15_concurrent_genkey | ✅ PASS | token | 两线程均在超时内完成且 rc=0（`client_sem` 串行） | - |  |
| t16a_sign_blen_zero | ✅ PASS | token | rc=0；r,s∈(0,n) | - |  |
| t16b_sign_blen_eq_n | ✅ PASS | token | rc=0 | - |  |
| t16c_repeat_channel_init | ✅ PASS | token | rc=0 | - |  |
| t16d_mbedtls_and_explicit_agree | ⏭️ SKIP | token | 两者 rc=0；各自 `check_pubkey`=0 | - |  |
| t17_sign_bad_d_zero | ✅ PASS | token | - | - |  |
| t19_verify_bad_q_off_curve | ✅ PASS | token | rc≠0 | - |  |
| t21_verify_wrong_keypair | ✅ PASS | token | rc≠0 | - |  |
| t80_proxy_responds_within_timeout | ✅ PASS | token | 3s 内返回（case_default）；否则 FAIL | - |  |
| t88_sign_bad_d_ge_n | ✅ PASS | token | rc≠0 | - |  |
| t89_verify_bad_q_infinity | ✅ PASS | token | rc≠0 | - |  |

## 汇总

OVERALL: PASS
