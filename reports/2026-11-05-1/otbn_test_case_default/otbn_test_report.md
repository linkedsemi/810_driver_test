# OTBN 测试报告

- 工程: otbn_test
- case: case_default
- 日期: 2026-11-05 21:02:25
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=1500s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (52/52)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| api_aes_kat | ✅ PASS | token | NIST 向量逐字节相等 | - |  |
| api_rsa2048_kat | ✅ PASS | token | OpenSSL 生成的密文/签名 KAT | - |  |
| api_rsa_keygen_2048 | ✅ PASS | token | - | nbits=2048, time_ms=973 |  |
| api_rsa_keygen_3072 | ✅ PASS | token | - | nbits=3072, time_ms=2694 |  |
| api_sha224_kat | ✅ PASS | token | 3 组 KAT 逐字节相等 | - |  |
| api_sha256_kat | ✅ PASS | token | 4 组 KAT | - |  |
| api_sha256_varlen | ✅ PASS | token | 逐长度 KAT | - |  |
| api_sha384_kat | ✅ PASS | token | 3 组 KAT | - |  |
| api_sha512_kat | ✅ PASS | token | 3 组 KAT | - |  |
| api_sm3_kat | ✅ PASS | token | 3 组 KAT | - |  |
| api_sm4_kat | ✅ PASS | token | 逐字节相等 | - |  |
| candidate_residue_and_topbits | ✅ PASS | token | - | - |  |
| direct_apis_ok | ✅ PASS | token | - | - |  |
| mbedtls_gen_key_ok | ✅ PASS | token | - | api=mbedtls_rsa_gen_key, keylen=2048, ret=0 |  |
| ref_vector_kat | ✅ PASS | token | - | key_from_pq_ret=0, n_byte_equal=1, d_byte_equal=1 |  |
| rng_error_propagates | ✅ PASS | token | - | - |  |
| rsa_2048_correctness | ✅ PASS | token | - | - |  |
| rsa_2048_deterministic | ✅ PASS | token | - | - |  |
| rsa_3072_ok | ✅ PASS | token | - | - |  |
| rsa_4096_ok | ✅ PASS | token | - | - |  |
| session_no_leak | ✅ PASS | token | - | iterations=10, ok=10 |  |
| unaligned_rejected | ✅ PASS | token | - | - |  |
| zzz_bench_modexp_128b | ✅ PASS | token | - | num_bytes=128, samples=20, median_us=8880, p90_us=8880, min_us=8880, max_us=8880, mean_us=8880, byte_equal=1, err=0, us_list=8880 |  |
| zzz_bench_modexp_192b | ✅ PASS | token | - | num_bytes=192, samples=20, median_us=27667, p90_us=27668, min_us=27667, max_us=27670, mean_us=27667, byte_equal=1, err=0, us_list=27670 |  |
| zzz_bench_no_internal_drbg | ✅ PASS | token | - | ret=0/0/ctrl=0, edn=0, same_p=1, same_q=1, same_n=1, same_d=1, rnd_calls=1856/1888, urnd_calls=736/736, ctrl_diff=1, edn_diff=0 |  |
| zzz_bench_random_counts | ✅ PASS | token | - | keygen_ret=0, keygen_ms=3665, rnd_cb_calls=9120, urnd_cb_calls=3856, rnd_words=9120, urnd_words=3856, rnd_isr_requests=1140, urnd_isr_requests=482, candidates=1779, sieve_pass=240, post_restore_check_prime_ret=0, post_restore_prime=1, post_rnd_delta=0, post_urnd_delta=0 |  |
| zzz_bench_trial_division | ✅ PASS | token | - | candidates=100, prime_bytes=128, primes=1228, avg_ns=415958, median_ns=6515, p90_ns=2633760, min_ns=2190, max_ns=2638976, avg_us_x1000=415, median_us_x1000=6, ns_per_mod=2146, us_per_mod_x1000=2, total_mods=19377, avg_mods=193, max_mods=1228, survivors=14, survivor_avg_ns=2635075 |  |
| zzz_modexp_kat_n128b_random | ✅ PASS | token | - | ret=0, ms=9, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n192b_random | ✅ PASS | token | - | ret=0, ms=28, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n256b_random | ✅ PASS | token | - | ret=0, ms=63, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n384b_hostkat | ✅ PASS | token | - | ret=0, ms=204, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n4b_base0 | ✅ PASS | token | - | ret=0, ms=9, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n4b_base1 | ✅ PASS | token | - | ret=0, ms=9, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n4b_basenm1 | ✅ PASS | token | - | ret=0, ms=9, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n4b_exp0 | ✅ PASS | token | - | ret=0, ms=9, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n4b_exp1 | ✅ PASS | token | - | ret=0, ms=8, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n512b_f4_hostkat | ✅ PASS | token | - | ret=0, ms=2, byte_equal=1, first_diff=-1, fw=8 |  |
| zzz_modexp_kat_n512b_hostkat | ✅ PASS | token | - | ret=0, ms=475, byte_equal=1, first_diff=-1, fw=8 |  |
| mix_ecdsa_sha256_stream | ✅ PASS | token | genkey/sign/verify==0 且 digest == KAT | - |  |
| mix_keygen_sha256_keygen | ✅ PASS | token | 两次 keygen 均 check_privkey+sign/verify 通过 | keygen1_ms=695, keygen2_ms=1210 |  |
| mix_sha256_keygen_seq | ✅ PASS | token | keygen 后 sha256("abc...") 仍 == KAT | keygen_ms=1261 |  |
| mix_sha256_stream_keygen | ✅ PASS | token | 56B 两段 update 的 digest == KAT | keygen_ms=2560 |  |
| mix_sha512_stream_keygen | ✅ PASS | token | digest == KAT | keygen_ms=483 |  |
| mix_sign_sha256_stream | ✅ PASS | token | sign/verify==0 且流式 digest == KAT | - |  |
| mix_sm3_stream_keygen | ✅ PASS | token | digest == KAT | keygen_ms=942 |  |
| api_ecdsa_p256_kat | ✅ PASS | token | r/s 与参考值逐字节相等 | - |  |
| api_ecdsa_p384 | ✅ PASS | token | verify==0 | - |  |
| api_ecdsa_sm2 | ✅ PASS | token | verify==0 | - |  |
| perf_ecdsa_p256 | ✅ PASS | token | ret==0；INFO: sign_ops/verify_ops | loops=10, sign_ops=416, verify_ops=714 |  |
| perf_rsa2048_sign | ✅ PASS | token | ret==0；INFO: loops/avg_us | loops=5, avg_us=67018 |  |
| perf_sha256_64k | ✅ PASS | token | ret==0；INFO: bytes/us/kbps | bytes=65536, us=14011, kbps=4677 |  |
| api_rsa_keygen_4096 | ✅ PASS | token | - | nbits=4096, time_ms=15747, rng_calls=6972 |  |

## 汇总

OVERALL: PASS
