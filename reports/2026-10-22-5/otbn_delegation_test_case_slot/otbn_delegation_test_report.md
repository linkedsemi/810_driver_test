# otbn_delegation_test 测试报告

- 工程: otbn_delegation_test
- case: case_slot
- 日期: 2026-10-22 22:43:20
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）
- 执行标记: CTRL=SUCCESSFUL, TGT=SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (6/6；SKIP 6)

## case_slot (PASS)

> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| s01_sign_verify | ✅ PASS | token | rc=0；r/s 用已知公钥 verify=0 | - |  |
| s02_unregistered | ✅ PASS | token | rc=-ENOENT | - |  |
| s03_curve_mismatch | ✅ PASS | token | rc=-EINVAL | - |  |
| s04_register_validation | ⏭️ SKIP | token | d=0、d=n → `MBEDTLS_ERR_ECP_INVALID_KEY`；d=n-1 → rc=0；非白名单 →… | - |  |
| s05_curves | ✅ PASS | token | 两个 rc=0；分别用 P384/SM2 公钥 verify=0 | - |  |
| s06_multi_slot | ✅ PASS | token | 两个 rc=0；各自 verify=0；交叉 verify=`MBEDTLS_ERR_ECP_VERIFY_FAILED… | - |  |
| s07_hash_lengths | ✅ PASS | token | 31B、64B verify=0（64B 按前 32B 验）；0B rc=0 且 r/s∈(0,n) | - |  |
| s08_client_args | ⏭️ SKIP | token | -EINVAL / -EIO / -ENOENT | - |  |
| s09_server_args | ⏭️ SKIP | token | 前 7 项 -EINVAL；合法请求 rc=0 | - |  |
| s10_soak | ⏭️ SKIP | token | 100 次 rc=0；末次 verify=0 | - |  |
| s11_concurrent | ⏭️ SKIP | token | 两线程 rc 均=0，无死锁 | - |  |
| s12_server_race | ⏭️ SKIP | token | 注册/注销与签名 rc 均=0，无 oops | - |  |

## 汇总

OVERALL: PASS
