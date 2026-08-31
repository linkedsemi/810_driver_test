# OTBN 测试报告

- 工程: otbn_test
- 日期: 2026-08-28 21:32:29
- 板级: lsqsh_evb@2os/lsqsh/cpu1
- 环境: tgt_uart=auto, timeout=420s

## 测试项结果

| 测试项 | 结果 | 预期(COVERAGE) | 实际(INFO) | 失败原因 | 来源 |
|---|---|---|---|---|---|
| api_sha224_kat | ✅ PASS | 3 组 KAT 逐字节相等 |  |  | src=token |
| api_sha256_kat | ✅ PASS | 4 组 KAT |  |  | src=token |
| api_sm3_kat | ✅ PASS | 3 组 KAT |  |  | src=token |
| api_sha384_kat | ✅ PASS | 3 组 KAT |  |  | src=token |
| api_sha512_kat | ✅ PASS | 3 组 KAT |  |  | src=token |
| api_sha256_varlen | ✅ PASS | 逐长度 KAT |  |  | src=token |
| api_aes_kat | ✅ PASS | NIST 向量逐字节相等 |  |  | src=token |
| api_sm4_kat | ✅ PASS | 逐字节相等 |  |  | src=token |
| api_rsa2048_kat | ✅ PASS | OpenSSL 生成的密文/签名 KAT |  |  | src=token |
| api_rsa_keygen_2048 | ✅ PASS |  | nbits=2048,time_ms=1352 |  | src=token |
| api_rsa_keygen_3072 | ✅ PASS |  | nbits=3072,time_ms=3991 |  | src=token |
| api_rsa_keygen_4096 | ✅ PASS |  | nbits=4096,time_ms=24575,rng_calls=14364 |  | src=token |
| api_ecdsa_p256_kat | ✅ PASS | r/s 与参考值逐字节相等 |  |  | src=token |
| api_ecdsa_p384 | ✅ PASS | verify==0 |  |  | src=token |
| api_ecdsa_sm2 | ✅ PASS | verify==0 |  |  | src=token |
| perf_sha256_64k | ✅ PASS | ret==0；INFO: bytes/us/kbps | bytes=65536,us=14081,kbps=4654 |  | src=token |
| perf_rsa2048_sign | ✅ PASS | ret==0；INFO: loops/avg_us | loops=5,avg_us=67044 |  | src=token |
| perf_ecdsa_p256 | ✅ PASS | ret==0；INFO: sign_ops/verify_ops | loops=10,sign_ops=434,verify_ops=714 |  | src=token |
| mix_sha256_keygen_seq | ✅ PASS | keygen 后 sha256("abc...") 仍 == KAT | keygen_ms=2270 |  | src=token |
| mix_sha256_stream_keygen | ✅ PASS | 56B 两段 update 的 digest == KAT | keygen_ms=913 |  | src=token |
| mix_keygen_sha256_keygen | ✅ PASS | 两次 keygen 均 check_privkey+sign/verify 通过 | keygen1_ms=2022,keygen2_ms=3583 |  | src=token |
| mix_sign_sha256_stream | ✅ PASS | sign/verify==0 且流式 digest == KAT |  |  | src=token |
| mix_sha512_stream_keygen | ✅ PASS | digest == KAT | keygen_ms=1779 |  | src=token |
| mix_sm3_stream_keygen | ✅ PASS | digest == KAT | keygen_ms=5245 |  | src=token |
| mix_ecdsa_sha256_stream | ✅ PASS | genkey/sign/verify==0 且 digest == KAT |  |  | src=token |
