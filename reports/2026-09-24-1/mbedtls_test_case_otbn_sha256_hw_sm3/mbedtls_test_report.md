# mbedtls 测试报告

- 工程: mbedtls_test
- case: case_otbn_sha256_hw_sm3
- 日期: 2026-09-23 23:44:43
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=600s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (34/34)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| aes128_cbc | ✅ PASS | token | 4 块加密向量 + 解密回环 | algo=aes-128-cbc, bytes=64, us=53 |  |
| aes128_ctr | ✅ PASS | token | 4 块 CTR 向量 + 解密回环 | algo=aes-128-ctr, bytes=64, us=38 |  |
| aes128_gcm | ✅ PASS | token | 2 块 ciphertext + 16B tag + 解密回环 | algo=aes-128-gcm, bytes=32, us=156 |  |
| p256_kat | ✅ PASS | token | 黄金 r/s + verify=0 | curve=p256, mode=fixed_k_kat, us=3918 |  |
| p256_keygen | ✅ PASS | token | verify=0 | curve=p256, mode=keygen_sign_verify, keygen_us=17883, sign_us=2378, verify_us=1397 |  |
| p384_keygen | ✅ PASS | token | verify=0 | curve=p384, mode=keygen_sign_verify, keygen_us=28092, sign_us=5616, verify_us=3829 |  |
| sm2_keygen | ✅ PASS | token | verify=0 | curve=sm2, mode=keygen_sign_verify, keygen_us=317812, sign_us=3025, verify_us=5566 |  |
| sha224_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sha224, vecs=5, long_bytes=1000000, us=215203 |  |
| sha256_kat | ✅ PASS | token | 4 短向量 + 百万'a'=cdc76e5c... | algo=sha256, vecs=5, long_bytes=1000000, us=215285 |  |
| sha384_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sha384, vecs=5, long_bytes=1000000, us=152743 |  |
| sha512_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sha512, vecs=5, long_bytes=1000000, us=152626 |  |
| sm3_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sm3, vecs=5, long_bytes=1000000, us=66383 |  |
| concurrent_hash_ecdsa | ✅ PASS | token | - | loops=5, sha_ret=0, ecdsa_ret=0 |  |
| base_mult | ✅ PASS | token | (rx,ry) 与 NIST 向量一致 | - |  |
| field_bytes | ✅ PASS | token | 返回 32/48/66 | - |  |
| point_add | ✅ PASS | token | (rx,ry) 与 NIST 向量一致 | - |  |
| scalar_mult | ✅ PASS | token | (rx,ry) 与 NIST 向量一致 | - |  |
| stress_reentry | ✅ PASS | token | 全部 ret=0 | - |  |
| aes_bad_params | ✅ PASS | token | NULL ctx/input、错误 keybits 返回非 0 | - |  |
| ecdsa_tamper_verify | ✅ PASS | token | ret != 0 | - |  |
| heap_stress_keygen | ✅ PASS | token | - | loops=16, pre_used=336, base_used=336, first_after=336, max_after=336, post_used=336, peak_used=2812, peak_blocks=49, heap=65536 |  |
| pointops_base_mult_bad | ✅ PASS | token | ret != 0 | - |  |
| pointops_field_bytes_bad | ✅ PASS | token | ret != 0（-EINVAL） | - |  |
| pointops_point_add_bad | ✅ PASS | token | ret != 0 | - |  |
| pointops_scalar_mult_bad | ✅ PASS | token | ret != 0 | - |  |
| rsa_key_mismatch | ✅ PASS | token | ret != 0 | - |  |
| rsa_keygen_non_f4_fallback | ✅ PASS | token | ret=0 + check_privkey=0 | - |  |
| sha256_bad_params | ✅ PASS | token | NULL ctx/input 返回非 0；len=0 合法；reset 后再 update 正常 | - |  |
| sha384_bad_params | ✅ PASS | token | NULL 返回非 0 | - |  |
| rsa2048_keygen | ✅ PASS | token | check=0 + verify=0 | algo=rsa2048, keylen=2048, exponent=65537, time_ms=1923 |  |
| rsa2048_modexp | ✅ PASS | token | decrypt(黄金密文)==黄金明文；sign==黄金签名；verify=0 | algo=rsa2048, stage=signverify, us=67838 |  |
| rsa2048_otbn_hit | ✅ PASS | token | - | hook_ret=0, imem_firmware=8 |  |
| rsa3072_modexp | ✅ PASS | token | decrypt 黄金明文；sign/verify 回环 | algo=rsa3072, stage=signverify, us=212978 |  |
| rsa4096_modexp | ✅ PASS | token | decrypt 黄金明文；sign/verify 回环 | algo=rsa4096, stage=signverify, us=490681 |  |

## 汇总

OVERALL: PASS
