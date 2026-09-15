# OTBN 测试报告

- 工程: otbn_selftest
- 日期: 2026-09-09 13:02:52
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM10, timeout=420s
- PROJECT EXECUTION: SUCCESSFUL

## 测试项结果

| 测试项 | 结果 | 预期(COVERAGE) | 实际(INFO) | 失败原因 | 来源 |
|---|---|---|---|---|---|
| st_aes | ✅ PASS | ret==0 |  |  | src=token |
| st_aria | ✅ PASS | ret==0 |  |  | src=token |
| st_base64 | ✅ PASS | ret==0 |  |  | src=token |
| st_calloc | ✅ PASS | ret==0 |  |  | src=token |
| st_camellia | ✅ PASS | ret==0 |  |  | src=token |
| st_ccm | ✅ PASS | ret==0 |  |  | src=token |
| st_chacha20 | ✅ PASS | ret==0 |  |  | src=token |
| st_chachapoly | ✅ PASS | ret==0 |  |  | src=token |
| st_cmac | ✅ PASS | ret==0 |  |  | src=token |
| st_ctr_drbg | ✅ PASS | NOT_RUN |  |  | src=token |
| st_des | ✅ PASS | ret==0 |  |  | src=token |
| st_dhm | ✅ PASS | ret==0 |  |  | src=token |
| st_ecjpake | ✅ PASS | ret==0 |  |  | src=token |
| st_ecp | ✅ PASS | ret==0 |  |  | src=token |
| st_gcm | ✅ PASS | ret==0 |  |  | src=token |
| st_hmac_drbg | ✅ PASS | ret==0 |  |  | src=token |
| st_md5 | ✅ PASS | ret==0 |  |  | src=token |
| st_mpi | ✅ PASS | ret==0 |  |  | src=token |
| st_nist_kw | ✅ PASS | ret==0 |  |  | src=token |
| st_pkcs5 | ✅ PASS | ret==0 |  |  | src=token |
| st_poly1305 | ✅ PASS | ret==0 |  |  | src=token |
| st_ripemd160 | ✅ PASS | ret==0 |  |  | src=token |
| st_rsa | ✅ PASS | ret==0 |  |  | src=token |
| st_sha1 | ✅ PASS | ret==0 |  |  | src=token |
| st_sha224 | ✅ PASS | ret==0 |  |  | src=token |
| st_sha256 | ✅ PASS | ret==0 |  |  | src=token |
| st_sha384 | ✅ PASS | ret==0 |  |  | src=token |
| st_sha3 | ✅ PASS | ret==0 |  |  | src=token |
| st_sha512 | ✅ PASS | ret==0 |  |  | src=token |
| st_memory_buffer_alloc | ✅ PASS | NOT_RUN |  |  | src=token |
