# mbedtls 测试报告

- 工程: mbedtls_test
- case: case_sw_hw_mix
- 日期: 2026-09-15 12:27:44
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=600s（环境细节见 810_driver_test/README.md 外设接线记录）
- PROJECT EXECUTION: SUCCESSFUL

## 测试项结果

| 测试项 | 结果 | 预期(COVERAGE) | 实际(INFO) | 失败原因 | 来源 |
|---|---|---|---|---|---|
| aes128_cbc | ✅ PASS | 4 块加密向量 + 解密回环 | algo=aes-128-cbc, bytes=64, us=22 |  | src=token |
| aes128_ctr | ✅ PASS | 4 块 CTR 向量 + 解密回环 | algo=aes-128-ctr, bytes=64, us=18 |  | src=token |
| aes128_gcm | ✅ PASS | 2 块 ciphertext + 16B tag + 解密回环 | algo=aes-128-gcm, bytes=32, us=135 |  | src=token |
| sm4_cbc | ✅ PASS | 32B CBC 向量 + 解密回环 | algo=sm4-cbc, bytes=32, us=10 |  | src=token |
| sm4_ctr | ✅ PASS | 32B CTR 向量 + 解密回环 | algo=sm4-ctr, bytes=32, us=12 |  | src=token |
| sm4_ecb | ✅ PASS | 32B 加密向量 + 解密回环 | algo=sm4-ecb, bytes=32, us=6 |  | src=token |
| p256_kat | ✅ PASS | 黄金 r/s + verify=0 | curve=p256, mode=fixed_k_kat, us=89580 |  | src=token |
| p256_keygen | ✅ PASS | verify=0 | curve=p256, mode=keygen_sign_verify, keygen_us=18079, sign_us=19797, verify_us=70080 |  | src=token |
| p384_keygen | ✅ PASS | verify=0 | curve=p384, mode=keygen_sign_verify, keygen_us=28927, sign_us=32099, verify_us=121077 |  | src=token |
| sha224_kat | ✅ PASS | 4 短向量 + 百万'a' | algo=sha224, vecs=5, long_bytes=1000000, us=74833 |  | src=token |
| sha256_kat | ✅ PASS | 4 短向量 + 百万'a'=cdc76e5c... | algo=sha256, vecs=5, long_bytes=1000000, us=74825 |  | src=token |
| sha384_kat | ✅ PASS | 4 短向量 + 百万'a' | algo=sha384, vecs=5, long_bytes=1000000, us=226058 |  | src=token |
| sha512_kat | ✅ PASS | 4 短向量 + 百万'a' | algo=sha512, vecs=5, long_bytes=1000000, us=225993 |  | src=token |
| concurrent_hash_ecdsa | ✅ PASS |  | loops=5, sha_ret=0, ecdsa_ret=0 |  | src=token |
| aes_bad_params | ✅ PASS | NULL ctx/input、错误 keybits 返回非 0 |  |  | src=token |
| ecdsa_tamper_verify | ✅ PASS | ret != 0 |  |  | src=token |
| rsa_key_mismatch | ✅ PASS | ret != 0 |  |  | src=token |
| rsa_keygen_non_f4_fallback | ✅ PASS | ret=0 + check_privkey=0 |  |  | src=token |
| sha256_bad_params | ✅ PASS | NULL ctx/input 返回非 0；len=0 合法；reset 后再 update 正常 |  |  | src=token |
| sha384_bad_params | ✅ PASS | NULL 返回非 0 |  |  | src=token |
| sm4_bad_params | ✅ PASS | NULL input 返回非 0 |  |  | src=token |
| rsa2048_keygen | ✅ PASS | check=0 + verify=0 | algo=rsa2048, keylen=2048, exponent=65537, time_ms=13707 |  | src=token |
| rsa2048_modexp | ✅ PASS | decrypt(黄金密文)==黄金明文；sign==黄金签名；verify=0 | algo=rsa2048, stage=signverify, us=439143 |  | src=token |
| rsa3072_modexp | ✅ PASS | decrypt 黄金明文；sign/verify 回环 |  |  | src=token |
| rsa4096_modexp | ✅ PASS | decrypt 黄金明文；sign/verify 回环 |  |  | src=token |
