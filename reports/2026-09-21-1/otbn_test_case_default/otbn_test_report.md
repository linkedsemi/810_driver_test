# OTBN 测试报告

- 工程: otbn_test
- case: case_default
- 日期: 2026-09-21 11:26:48
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=420s
- 执行标记: SUCCESSFUL
- 内存兜底: 有效
- 结果: PASS (25/25)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| api_aes_kat | ✅ PASS | token | NIST 向量逐字节相等 | - |  |
| api_rsa2048_kat | ✅ PASS | token | OpenSSL 生成的密文/签名 KAT | - |  |
| api_rsa_keygen_2048 | ✅ PASS | token | - | nbits=2048, time_ms=1211 |  |
| api_rsa_keygen_3072 | ✅ PASS | token | - | nbits=3072, time_ms=20508 |  |
| api_sha224_kat | ✅ PASS | token | 3 组 KAT 逐字节相等 | - |  |
| api_sha256_kat | ✅ PASS | token | 4 组 KAT | - |  |
| api_sha256_varlen | ✅ PASS | token | 逐长度 KAT | - |  |
| api_sha384_kat | ✅ PASS | token | 3 组 KAT | - |  |
| api_sha512_kat | ✅ PASS | token | 3 组 KAT | - |  |
| api_sm3_kat | ✅ PASS | token | 3 组 KAT | - |  |
| api_sm4_kat | ✅ PASS | token | 逐字节相等 | - |  |
| mix_ecdsa_sha256_stream | ✅ PASS | token | genkey/sign/verify==0 且 digest == KAT | - |  |
| mix_keygen_sha256_keygen | ✅ PASS | token | 两次 keygen 均 check_privkey+sign/verify 通过 | keygen1_ms=6263, keygen2_ms=6682 |  |
| mix_sha256_keygen_seq | ✅ PASS | token | keygen 后 sha256("abc...") 仍 == KAT | keygen_ms=3778 |  |
| mix_sha256_stream_keygen | ✅ PASS | token | 56B 两段 update 的 digest == KAT | keygen_ms=2012 |  |
| mix_sha512_stream_keygen | ✅ PASS | token | digest == KAT | keygen_ms=4992 |  |
| mix_sign_sha256_stream | ✅ PASS | token | sign/verify==0 且流式 digest == KAT | - |  |
| mix_sm3_stream_keygen | ✅ PASS | token | digest == KAT | keygen_ms=3026 |  |
| api_ecdsa_p256_kat | ✅ PASS | token | r/s 与参考值逐字节相等 | - |  |
| api_ecdsa_p384 | ✅ PASS | token | verify==0 | - |  |
| api_ecdsa_sm2 | ✅ PASS | token | verify==0 | - |  |
| perf_ecdsa_p256 | ✅ PASS | token | ret==0；INFO: sign_ops/verify_ops | loops=10, sign_ops=416, verify_ops=714 |  |
| perf_rsa2048_sign | ✅ PASS | token | ret==0；INFO: loops/avg_us | loops=5, avg_us=67084 |  |
| perf_sha256_64k | ✅ PASS | token | ret==0；INFO: bytes/us/kbps | bytes=65536, us=14040, kbps=4667 |  |
| api_rsa_keygen_4096 | ✅ PASS | token | - | progress_rng_calls=26692, nbits=4096, time_ms=33105, rng_calls=26973 |  |

## 汇总

OVERALL: PASS
