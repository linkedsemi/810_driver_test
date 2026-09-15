# mbedtls 测试报告

- 工程: mbedtls_test
- case: case_otbn_ecc
- 日期: 2026-09-07 23:02:01
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM10, timeout=600s（环境细节见 810_driver_test/README.md 外设接线记录）
- PROJECT EXECUTION: SUCCESSFUL

## 测试项结果

| 测试项 | 结果 | 预期(COVERAGE) | 实际(INFO) | 失败原因 | 来源 |
|---|---|---|---|---|---|
| aes128_cbc | ✅ PASS | 4 块加密向量 + 解密回环 | algo=aes-128-cbc, bytes=64, us=55 |  | src=token |
| aes128_ctr | ✅ PASS | 4 块 CTR 向量 + 解密回环 | algo=aes-128-ctr, bytes=64, us=38 |  | src=token |
| aes128_gcm | ✅ PASS | 2 块 ciphertext + 16B tag + 解密回环 | algo=aes-128-gcm, bytes=32, us=156 |  | src=token |
| p256_kat | ✅ PASS | 黄金 r/s + verify=0 | curve=p256, mode=fixed_k_kat, us=3916 |  | src=token |
| p256_keygen | ✅ PASS | verify=0 | curve=p256, mode=keygen_sign_verify, keygen_us=18099, sign_us=2378, verify_us=1446 |  | src=token |
| p384_keygen | ✅ PASS | verify=0 | curve=p384, mode=keygen_sign_verify, keygen_us=28395, sign_us=5616, verify_us=3771 |  | src=token |
| sm2_keygen | ✅ PASS | verify=0 | curve=sm2, mode=keygen_sign_verify, keygen_us=292701, sign_us=3026, verify_us=5567 |  | src=token |
| sha224_kat | ✅ PASS | 4 短向量 + 百万'a' | algo=sha224, vecs=5, long_bytes=1000000, us=139709 |  | src=token |
| sha256_kat | ✅ PASS | 4 短向量 + 百万'a'=cdc76e5c... | algo=sha256, vecs=5, long_bytes=1000000, us=139997 |  | src=token |
| sha384_kat | ✅ PASS | 4 短向量 + 百万'a' | algo=sha384, vecs=5, long_bytes=1000000, us=207608 |  | src=token |
| sha512_kat | ✅ PASS | 4 短向量 + 百万'a' | algo=sha512, vecs=5, long_bytes=1000000, us=207592 |  | src=token |
| concurrent_hash_ecdsa | ✅ PASS |  | loops=5, sha_ret=0, ecdsa_ret=0 |  | src=token |
| base_mult | ✅ PASS | (rx,ry) 与 NIST 向量一致 |  |  | src=token |
| field_bytes | ✅ PASS | 返回 32/48/66 |  |  | src=token |
| point_add | ✅ PASS | (rx,ry) 与 NIST 向量一致 |  |  | src=token |
| scalar_mult | ✅ PASS | (rx,ry) 与 NIST 向量一致 |  |  | src=token |
| stress_reentry | ✅ PASS | 全部 ret=0 |  |  | src=token |
| aes_bad_params | ✅ PASS | NULL ctx/input、错误 keybits 返回非 0 |  |  | src=token |
| ecdsa_tamper_verify | ✅ PASS | ret != 0 |  |  | src=token |
| pointops_base_mult_bad | ✅ PASS | ret != 0 |  |  | src=token |
| pointops_field_bytes_bad | ✅ PASS | ret != 0（-EINVAL） |  |  | src=token |
| pointops_point_add_bad | ✅ PASS | ret != 0 |  |  | src=token |
| pointops_scalar_edge | ✅ PASS | 不 HardFault，ret 可记录 |  |  | src=token |
| pointops_scalar_mult_bad | ✅ PASS | ret != 0 |  |  | src=token |
| rsa_key_mismatch | ✅ PASS | ret != 0 |  |  | src=token |
| rsa_keygen_non_f4_fallback | ✅ PASS | ret=0 + check_privkey=0 |  |  | src=token |
| sha256_bad_params | ✅ PASS | NULL ctx/input 返回非 0；len=0 合法；reset 后再 update 正常 |  |  | src=token |
| sha384_bad_params | ✅ PASS | NULL 返回非 0 |  |  | src=token |
| shared_sha256_ctx_thread | ✅ PASS | 不崩溃，至少一个成功 |  |  | src=token |
| rsa2048_keygen | ✅ PASS | check=0 + verify=0 | algo=rsa2048, keylen=2048, exponent=65537, time_ms=5551 |  | src=token |
| rsa2048_modexp | ✅ PASS | decrypt(黄金密文)==黄金明文；sign==黄金签名；verify=0 | algo=rsa2048, stage=signverify, us=431744 |  | src=token |
| rsa3072_modexp | ✅ PASS | decrypt 黄金明文；sign/verify 回环 |  |  | src=token |
| rsa4096_modexp | ✅ PASS | decrypt 黄金明文；sign/verify 回环 |  |  | src=token |
