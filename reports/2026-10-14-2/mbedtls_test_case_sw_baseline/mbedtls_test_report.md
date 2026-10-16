# mbedtls 测试报告

- 工程: mbedtls_test
- case: case_sw_baseline
- 日期: 2026-10-14 16:42:29
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=240.0s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (35/35)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| aes128_cbc | ✅ PASS | token | 4 块加密向量 + 解密回环 | algo=aes-128-cbc, bytes=64, us=54 |  |
| aes128_ctr | ✅ PASS | token | 4 块 CTR 向量 + 解密回环 | algo=aes-128-ctr, bytes=64, us=41 |  |
| aes128_ctr_carry | ✅ PASS | token | - | algo=aes-128-ctr, n=4, single_ok=1, segmented_ok=1, roundtrip_ok=1, single_bits=1111, seg_bits=1111 |  |
| aes128_ctr_inplace | ✅ PASS | token | - | algo=aes-128-ctr, bytes=64, inplace_eq_nist=1 |  |
| aes128_ctr_len | ✅ PASS | token | - | algo=aes-128-ctr, lens=1_15_17_31, len1=1, len15=1, len17=1, len31=1, guard_ok=1, us=25 |  |
| aes128_ctr_seg | ✅ PASS | token | - | algo=aes-128-ctr, seg=7+16+41, bytes=64, seg_eq_nist=1, state_after7=1, state_after23=1, nc_off_final=0 |  |
| aes128_ctr_unaligned | ✅ PASS | token | - | algo=aes-128-ctr, bytes=64, unaligned_eq_nist=1 |  |
| aes128_gcm | ✅ PASS | token | 2 块 ciphertext + 16B tag + 解密回环 | algo=aes-128-gcm, bytes=32, us=161 |  |
| aes128_xts | ✅ PASS | token | - | algo=aes-128-xts, kat1=1, kat2=1, kat3=1, dec1=1, dec2=1, dec3=1, steal55=1 |  |
| aes128_xts_multi_ctx | ✅ PASS | token | - | algo=aes-128-xts, ctx_a_eq_kat=1, ctx_b_eq_kat=1 |  |
| aes192_256_kat | ✅ PASS | token | - | ecb192=1, cbc192=1, dec192=1, ecb256=1, cbc256=1, dec256=1 |  |
| aes_cbc_len_contract | ✅ PASS | token | - | zero_len_ok=1, bad_len_ok=1 |  |
| aes_concurrent_ctx | ✅ PASS | token | - | bytes=8192, interleaved_eq_single_threaded=1 |  |
| aes_ecb_cbc_unaligned | ✅ PASS | token | - | ecb_unaligned_ok=1, cbc_unaligned_ok=1 |  |
| aes_ecb_order | ✅ PASS | token | - | ecb_eq_nist=1, cbc_eq_nist=1, ctr_eq_nist=1, ecb=69c4e0d86a7b0430d8cdb78070b4c55a, cbc=69c4e0d86a7b0430d8cdb78070b4c55a, ctr=69c4e0d86a7b0430d8cdb78070b4c55a, nist=69c4e0d86a7b0430d8cdb78070b4c55a |  |
| p256_kat | ✅ PASS | token | 黄金 r/s + verify=0 | curve=p256, mode=fixed_k_kat, us=86335 |  |
| p256_keygen | ✅ PASS | token | verify=0 | curve=p256, mode=keygen_sign_verify, keygen_us=17553, sign_us=19190, verify_us=66825 |  |
| p384_keygen | ✅ PASS | token | verify=0 | curve=p384, mode=keygen_sign_verify, keygen_us=27774, sign_us=30731, verify_us=115126 |  |
| sha224_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sha224, vecs=5, long_bytes=1000000, us=138066 |  |
| sha256_kat | ✅ PASS | token | 4 短向量 + 百万'a'=cdc76e5c... | algo=sha256, vecs=5, long_bytes=1000000, us=138081 |  |
| sha384_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sha384, vecs=5, long_bytes=1000000, us=204945 |  |
| sha512_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sha512, vecs=5, long_bytes=1000000, us=204952 |  |
| concurrent_hash_ecdsa | ✅ PASS | token | - | loops=5, sha_ret=0, ecdsa_ret=0 |  |
| aes_bad_params | ✅ PASS | token | **仅断言文档化契约**：错误 keybits 返回非 0；合法 setkey 返回 0（NULL 断言已移入 `aes… | - |  |
| ecdsa_tamper_verify | ✅ PASS | token | ret != 0 | - |  |
| heap_stress_keygen | ✅ PASS | token | - | loops=16, pre_used=0, base_used=0, first_after=0, max_after=0, post_used=0, peak_used=5240, peak_blocks=67, heap=65536 |  |
| rsa_key_mismatch | ✅ PASS | token | ret != 0 | - |  |
| rsa_keygen_non_f4_fallback | ✅ PASS | token | ret=0 + check_privkey=0 | - |  |
| sha256_bad_params | ✅ PASS | token | **is224=2 返回非 0**（文档化）；len=0 合法；reset 后再 update 正常 | - |  |
| sha384_bad_params | ✅ PASS | token | **is384=2 返回非 0**（文档化）；合法 starts+update 正常 | - |  |
| shared_sha256_ctx_thread | ✅ PASS | token | 不崩溃，至少一个成功 | - |  |
| rsa2048_keygen | ✅ PASS | token | check=0 + verify=0 | algo=rsa2048, keylen=2048, exponent=65537, time_ms=13999 |  |
| rsa2048_modexp | ✅ PASS | token | decrypt(黄金密文)==黄金明文；sign==黄金签名；verify=0 | algo=rsa2048, stage=signverify, us=434920 |  |
| rsa3072_modexp | ✅ PASS | token | decrypt 黄金明文；sign/verify 回环 | algo=rsa3072, stage=signverify, us=1276959 |  |
| rsa4096_modexp | ✅ PASS | token | decrypt 黄金明文；sign/verify 回环 | algo=rsa4096, stage=signverify, us=4785620 |  |

## 汇总

OVERALL: PASS
