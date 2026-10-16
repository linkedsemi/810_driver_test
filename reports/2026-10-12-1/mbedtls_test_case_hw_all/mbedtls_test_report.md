# mbedtls 测试报告

- 工程: mbedtls_test
- case: case_hw_all
- 日期: 2026-10-12 11:51:01
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=auto, timeout=600s
- 执行标记: FAILED
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: FAIL (38/42)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| aes128_cbc | ✅ PASS | token | 4 块加密向量 + 解密回环 | algo=aes-128-cbc, bytes=64, us=21 |  |
| aes128_ctr | ✅ PASS | token | 4 块 CTR 向量 + 解密回环 | algo=aes-128-ctr, bytes=64, us=18 |  |
| aes128_ctr_carry | ❌ FAIL | token | - | algo=aes-128-ctr, carry=word_boundary, single_ok=0, segmented_ok=1, roundtrip_ok=0 | WEST_TOPDIR/810_driver_test/projects/mbedtls_test/cpu1_mbedtls_test/src/cipher_test.c: 1024: cipher_aes128_ctr_carry: (single_ok is false) |
| aes128_ctr_inplace | ✅ PASS | token | - | algo=aes-128-ctr, bytes=64, inplace_eq_nist=1 |  |
| aes128_ctr_len | ✅ PASS | token | - | algo=aes-128-ctr, lens=1_15_17_31, len1=1, len15=1, len17=1, len31=1, guard_ok=1, us=14 |  |
| aes128_ctr_seg | ✅ PASS | token | - | algo=aes-128-ctr, seg=7+16+41, bytes=64, seg_eq_nist=1, state_after7=1, state_after23=1, nc_off_final=0 |  |
| aes128_ctr_unaligned | ✅ PASS | token | - | algo=aes-128-ctr, bytes=64, unaligned_eq_nist=1 |  |
| aes128_gcm | ✅ PASS | token | 2 块 ciphertext + 16B tag + 解密回环 | algo=aes-128-gcm, bytes=32, us=130 |  |
| aes128_xts | ✅ PASS | token | - | algo=aes-128-xts, kat1=1, kat2=1, kat3=1, dec1=1, dec2=1, dec3=1, steal55=1 |  |
| aes128_xts_multi_ctx | ✅ PASS | token | - | algo=aes-128-xts, ctx_a_eq_kat=1, ctx_b_eq_kat=1 |  |
| aes192_256_kat | ✅ PASS | token | - | ecb192=1, cbc192=1, dec192=1, ecb256=1, cbc256=1, dec256=1 |  |
| aes_alt_null_params | ❌ FAIL | token | - | setkey_null_ctx=-33, setkey_null_key=-33 | CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 0: CPU exception on CPU 0`（归档第 86 行；mcause: 5, Load access fault）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 |
| aes_cbc_len_contract | ❌ FAIL | token | - | zero_len_ok=0 | CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0`（归档第 125 行；mcause: 11, Environment call from M-mode）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 |
| aes_concurrent_ctx | ❌ FAIL | token | - | bytes=8192, interleaved_eq_single_threaded=0 | WEST_TOPDIR/810_driver_test/projects/mbedtls_test/cpu1_mbedtls_test/src/cipher_test.c: 1155: cipher_aes_concurrent_ctx: (same is false) |
| aes_ecb_cbc_unaligned | ✅ PASS | token | - | ecb_unaligned_ok=1, cbc_unaligned_ok=1 |  |
| aes_ecb_order | ✅ PASS | token | - | ecb_eq_nist=1, cbc_eq_nist=1, ctr_eq_nist=1, ecb=69c4e0d86a7b0430d8cdb78070b4c55a, cbc=69c4e0d86a7b0430d8cdb78070b4c55a, ctr=69c4e0d86a7b0430d8cdb78070b4c55a, nist=69c4e0d86a7b0430d8cdb78070b4c55a |  |
| sm4_cbc | ✅ PASS | token | 32B CBC 向量 + 解密回环 | algo=sm4-cbc, bytes=32, us=11 |  |
| sm4_ctr | ✅ PASS | token | 32B CTR 向量 + 解密回环 | algo=sm4-ctr, bytes=32, us=11 |  |
| sm4_ecb | ✅ PASS | token | 32B 加密向量 + 解密回环 | algo=sm4-ecb, bytes=32, us=6 |  |
| p256_kat | ✅ PASS | token | 黄金 r/s + verify=0 | curve=p256, mode=fixed_k_kat, us=85666 |  |
| p256_keygen | ✅ PASS | token | verify=0 | curve=p256, mode=keygen_sign_verify, keygen_us=17204, sign_us=18881, verify_us=66667 |  |
| p384_keygen | ✅ PASS | token | verify=0 | curve=p384, mode=keygen_sign_verify, keygen_us=27795, sign_us=30942, verify_us=115483 |  |
| sha224_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sha224, vecs=5, long_bytes=1000000, us=67326 |  |
| sha256_alt_null_params | ✅ PASS | token | - | starts_null_ctx=-116, update_null_input=-116 |  |
| sha256_kat | ✅ PASS | token | 4 短向量 + 百万'a'=cdc76e5c... | algo=sha256, vecs=5, long_bytes=1000000, us=67289 |  |
| sha384_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sha384, vecs=5, long_bytes=1000000, us=32674 |  |
| sha512_alt_null_params | ✅ PASS | token | - | starts_null_ctx=-117, update_null_input=-117 |  |
| sha512_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sha512, vecs=5, long_bytes=1000000, us=32835 |  |
| sm3_kat | ✅ PASS | token | 4 短向量 + 百万'a' | algo=sm3, vecs=5, long_bytes=1000000, us=67180 |  |
| concurrent_hash_ecdsa | ✅ PASS | token | - | loops=5, sha_ret=0, ecdsa_ret=0 |  |
| aes_bad_params | ✅ PASS | token | **仅断言文档化契约**：错误 keybits 返回非 0；合法 setkey 返回 0（NULL 断言已移入 `aes… | - |  |
| ecdsa_tamper_verify | ✅ PASS | token | ret != 0 | - |  |
| heap_stress_keygen | ✅ PASS | token | - | loops=16, pre_used=0, base_used=0, first_after=0, max_after=0, post_used=0, peak_used=5224, peak_blocks=65, heap=65536 |  |
| rsa_key_mismatch | ✅ PASS | token | ret != 0 | - |  |
| rsa_keygen_non_f4_fallback | ✅ PASS | token | ret=0 + check_privkey=0 | - |  |
| sha256_bad_params | ✅ PASS | token | **is224=2 返回非 0**（文档化）；len=0 合法；reset 后再 update 正常 | - |  |
| sha384_bad_params | ✅ PASS | token | **is384=2 返回非 0**（文档化）；合法 starts+update 正常 | - |  |
| sm4_bad_params | ✅ PASS | token | NULL ctx/in/out、len=0、非 16 整块返回非 0；合法入参仍返回 0 | - |  |
| rsa2048_keygen | ✅ PASS | token | check=0 + verify=0 | algo=rsa2048, keylen=2048, exponent=65537, time_ms=6829 |  |
| rsa2048_modexp | ✅ PASS | token | decrypt(黄金密文)==黄金明文；sign==黄金签名；verify=0 | algo=rsa2048, stage=signverify, us=434986 |  |
| rsa3072_modexp | ✅ PASS | token | decrypt 黄金明文；sign/verify 回环 | algo=rsa3072, stage=signverify, us=1274959 |  |
| rsa4096_modexp | ✅ PASS | token | decrypt 黄金明文；sign/verify 回环 | algo=rsa4096, stage=signverify, us=4859514 |  |

## 汇总

OVERALL: FAIL
