# OTBN RSA keygen 测试报告

- 工程: otbn_keygen_test
- case: case_default
- 日期: 2026-10-12 12:12:35
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=auto, timeout=900s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (11/11)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| candidate_residue_and_topbits | ✅ PASS | token | 256/384/512、128/192/256、无效=0；低字节 `&3==3`、MSB `&0xC0==0xC0`；同… | - |  |
| direct_apis_ok | ✅ PASS | token | check_prime(p)=true、check_prime(2^1024-1)=false；key_from_pq… | - |  |
| mbedtls_gen_key_ok | ✅ PASS | token | 两者 ret==0 | api=mbedtls_rsa_gen_key, keylen=2048, ret=0 |  |
| ref_vector_kat | ✅ PASS | token | ① 返回 0；② N 与参考逐字节相同；③ d 与参考逐字节相同；④ `check_prime(P)`==true（P≡… | key_from_pq_ret=0, n_byte_equal=1, d_byte_equal=1 |  |
| rng_error_propagates | ✅ PASS | token | rng 返回 -EIO/-EPERM ⇒ keygen 原样返回对应码 | - |  |
| rsa_2048_correctness | ✅ PASS | token | `n==p*q`；p、q 素数；p、q ≡3 mod 4；`e*d≡1 mod lcm(p-1,q-1)` | - |  |
| rsa_2048_deterministic | ✅ PASS | token | p/q/n/d 两次逐位相同 | - |  |
| rsa_3072_ok | ✅ PASS | token | ret==0；`n==p*q`；p、q ≡3 mod 4 | - |  |
| rsa_4096_ok | ✅ PASS | token | ret==0；`n==p*q`；p、q ≡3 mod 4 | - |  |
| session_no_leak | ✅ PASS | token | 10 次全部 ret==0（泄漏则第 2 次 -EBUSY） | iterations=10, ok=10 |  |
| unaligned_rejected | ✅ PASS | token | 非 4 字节对齐缓冲一律 `-EINVAL` | - |  |

## 汇总

OVERALL: PASS
