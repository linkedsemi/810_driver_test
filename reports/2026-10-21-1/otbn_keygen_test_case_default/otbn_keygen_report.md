# OTBN RSA keygen 测试报告

- 工程: otbn_keygen_test
- case: case_default
- 日期: 2026-10-21 16:17:43
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=auto, timeout=900s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (27/27)

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
| zzz_bench_modexp_128b | ✅ PASS | token | - | num_bytes=128, samples=20, median_us=8912, p90_us=8912, min_us=8912, max_us=8912, mean_us=8912, byte_equal=1, err=0, us_list=8912 |  |
| zzz_bench_modexp_192b | ✅ PASS | token | - | num_bytes=192, samples=20, median_us=27698, p90_us=27698, min_us=27698, max_us=27700, mean_us=27698, byte_equal=1, err=0, us_list=27700 |  |
| zzz_bench_no_internal_drbg | ✅ PASS | token | - | ret=0/0/ctrl=0, edn=0, same_p=1, same_q=1, same_n=1, same_d=1, rnd_calls=2176/1952, urnd_calls=736/736, ctrl_diff=1, edn_diff=0 |  |
| zzz_bench_random_counts | ✅ PASS | token | - | keygen_ret=0, keygen_ms=3664, rnd_cb_calls=9024, urnd_cb_calls=3856, rnd_words=9024, urnd_words=3856, rnd_isr_requests=1128, urnd_isr_requests=482, candidates=1779, sieve_pass=240, post_restore_check_prime_ret=0, post_restore_prime=1, post_rnd_delta=0, post_urnd_delta=0 |  |
| zzz_bench_trial_division | ✅ PASS | token | - | candidates=1000, prime_bytes=128, primes=1228, avg_ns=382978, median_ns=6580, p90_ns=2633700, min_ns=2180, max_ns=2650520, avg_us_x1000=382, median_us_x1000=6, ns_per_mod=2147, us_per_mod_x1000=2, total_mods=178353, avg_mods=178, max_mods=1228, survivors=122, survivor_avg_ns=2635588 |  |
| zzz_modexp_kat_n128b_random | ✅ PASS | token | ret==0（非 -ENOTSUP）；输出与 KAT 逐字节相同；fw==RSA_MODEXP | ret=0, ms=9, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n192b_random | ✅ PASS | token | 同上 | ret=0, ms=28, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n256b_random | ✅ PASS | token | 同上 | ret=0, ms=63, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n384b_hostkat | ✅ PASS | token | 同上 | ret=0, ms=204, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n4b_base0 | ✅ PASS | token | 输出 == 0 | ret=0, ms=8, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n4b_base1 | ✅ PASS | token | 输出 == 1 | ret=0, ms=9, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n4b_basenm1 | ✅ PASS | token | 输出 == n-1 | ret=0, ms=9, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n4b_exp0 | ✅ PASS | token | 输出 == 1（x^0 mod n） | ret=0, ms=9, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n4b_exp1 | ✅ PASS | token | 输出 == base | ret=0, ms=8, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n512b_f4_hostkat | ✅ PASS | token | 同上 | ret=0, ms=2, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n512b_hostkat | ✅ PASS | token | 同上 | ret=0, ms=475, byte_equal=1, first_diff=-1, fw=8 |  |

## 汇总

OVERALL: PASS
