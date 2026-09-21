# mbedtls 测试报告

- 工程: mbedtls_test
- case: case_all_on_max
- 日期: 2026-09-21 12:42:18
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=600s
- 执行标记: SUCCESSFUL
- 内存兜底: 有效
- 结果: PASS (39/39)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| aes128_cbc | ✅ PASS | token | 4 块加密向量 + 解密回环 | algo=aes-128-cbc, bytes=64, us=21 |  |
| aes128_ctr | ✅ PASS | token | 4 块 CTR 向量 + 解密回环 | algo=aes-128-ctr, bytes=64, us=17 |  |
| aes128_gcm | ✅ PASS | token | 2 块 ciphertext + 16B tag + 解密回环 | algo=aes-128-gcm, bytes=32, us=131 |  |
| sm4_cbc | ✅ PASS | token | 32B CBC 向量 + 解密回环 | algo=sm4-cbc, bytes=32, us=10 |  |
| sm4_ctr | ✅ PASS | token | 32B CTR 向量 + 解密回环 | algo=sm4-ctr, bytes=32, us=11 |  |
| sm4_ecb | ✅ PASS | token | 32B 加密向量 + 解密回环 | algo=sm4-ecb, bytes=32, us=6 |  |
| p256_kat | ✅ PASS | token | 黄金 r/s + verify=0 | curve=p256, mode=fixed_k_kat, us=3922 |  |
| p256_keygen | ✅ PASS | token | verify=0 | curve=p256, mode=keygen_sign_verify, keygen_us=18715, sign_us=2378, verify_us=1415 |  |
| p384_keygen | ✅ PASS | token | verify=0 | curve=p384, mode=keygen_sign_verify, keygen_us=28834, sign_us=5617, verify_us=3755 |  |
| sm2_keygen | ✅ PASS | token | verify=0 | curve=sm2, mode=keygen_sign_verify, keygen_us=298708, sign_us=3026, verify_us=5567 |  |
| sha224_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sha224, vecs=5, long_bytes=1000000, us=214218 |  |
| sha256_kat | ✅ PASS | token | 4 短向量 + 百万'a'=cdc76e5c... | algo=sha256, vecs=5, long_bytes=1000000, us=214116 |  |
| sha384_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sha384, vecs=5, long_bytes=1000000, us=151595 |  |
| sha512_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sha512, vecs=5, long_bytes=1000000, us=151539 |  |
| sm3_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sm3, vecs=5, long_bytes=1000000, us=516371 |  |
| concurrent_hash_ecdsa | ✅ PASS | token | - | loops=5, sha_ret=0, ecdsa_ret=0 |  |
| base_mult | ✅ PASS | token | (rx,ry) 与 NIST 向量一致 | - |  |
| field_bytes | ✅ PASS | token | 返回 32/48/66 | - |  |
| point_add | ✅ PASS | token | (rx,ry) 与 NIST 向量一致 | - |  |
| scalar_mult | ✅ PASS | token | (rx,ry) 与 NIST 向量一致 | - |  |
| stress_reentry | ✅ PASS | token | 全部 ret=0 | - |  |
| aes_bad_params | ✅ PASS | token | NULL ctx/input、错误 keybits 返回非 0 | - |  |
| ecdsa_tamper_verify | ✅ PASS | token | ret != 0 | - |  |
| heap_stress_keygen | ✅ PASS | token | - | loops=16, pre_used=304, base_used=304, first_after=304, max_after=304, post_used=304, peak_used=2812, peak_blocks=49, heap=65536 |  |
| pointops_base_mult_bad | ✅ PASS | token | ret != 0 | - |  |
| pointops_field_bytes_bad | ✅ PASS | token | ret != 0（-EINVAL） | - |  |
| pointops_point_add_bad | ✅ PASS | token | ret != 0 | - |  |
| pointops_scalar_mult_bad | ✅ PASS | token | ret != 0 | - |  |
| rsa_key_mismatch | ✅ PASS | token | ret != 0 | - |  |
| rsa_keygen_non_f4_fallback | ✅ PASS | token | ret=0 + check_privkey=0 | - |  |
| sha256_bad_params | ✅ PASS | token | NULL ctx/input 返回非 0；len=0 合法；reset 后再 update 正常 | - |  |
| sha384_bad_params | ✅ PASS | token | NULL 返回非 0 | - |  |
| sm4_bad_params | ✅ PASS | token | NULL ctx/in/out、len=0、非 16 整块返回非 0；合法入参仍返回 0 | - |  |
| rsa2048_keygen | ✅ PASS | token | check=0 + verify=0 | algo=rsa2048, keylen=2048, exponent=65537, time_ms=1138 |  |
| rsa2048_modexp | ✅ PASS | token | decrypt(黄金密文)==黄金明文；sign==黄金签名；verify=0 | algo=rsa2048, stage=signverify, us=67723 |  |
| rsa2048_otbn_hit | ✅ PASS | token | - | hook_ret=0, imem_firmware=8 |  |
| rsa3072_modexp | ✅ PASS | token | decrypt 黄金明文；sign/verify 回环 | algo=rsa3072, stage=signverify, us=212821 |  |
| rsa4096_modexp | ✅ PASS | token | decrypt 黄金明文；sign/verify 回环 | algo=rsa4096, stage=signverify, us=491103 |  |
| concurrent_otbn_hash_ecc | ✅ PASS | token | 两线程均完成、ret=0、不崩溃 | loops=5, sha_ret=0, ecdsa_ret=0 |  |

## 汇总

OVERALL: PASS
