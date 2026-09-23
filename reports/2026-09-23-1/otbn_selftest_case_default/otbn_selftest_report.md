# OTBN 测试报告

- 工程: otbn_selftest
- case: case_default
- 日期: 2026-09-22 21:22:09
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=420s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (30/30)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| st_aes | ✅ PASS | token | ret==0 | - |  |
| st_aria | ✅ PASS | token | ret==0 | - |  |
| st_base64 | ✅ PASS | token | ret==0 | - |  |
| st_calloc | ✅ PASS | token | ret==0 | - |  |
| st_camellia | ✅ PASS | token | ret==0 | - |  |
| st_ccm | ✅ PASS | token | ret==0 | - |  |
| st_chacha20 | ✅ PASS | token | ret==0 | - |  |
| st_chachapoly | ✅ PASS | token | ret==0 | - |  |
| st_cmac | ✅ PASS | token | ret==0 | - |  |
| st_ctr_drbg | ✅ PASS | token | NOT_RUN | - |  |
| st_des | ✅ PASS | token | ret==0 | - |  |
| st_dhm | ✅ PASS | token | ret==0 | - |  |
| st_ecjpake | ✅ PASS | token | ret==0 | - |  |
| st_ecp | ✅ PASS | token | ret==0 | - |  |
| st_gcm | ✅ PASS | token | ret==0 | - |  |
| st_hmac_drbg | ✅ PASS | token | ret==0 | - |  |
| st_md5 | ✅ PASS | token | ret==0 | - |  |
| st_mpi | ✅ PASS | token | ret==0 | - |  |
| st_nist_kw | ✅ PASS | token | ret==0 | - |  |
| st_pkcs5 | ✅ PASS | token | ret==0 | - |  |
| st_poly1305 | ✅ PASS | token | ret==0 | - |  |
| st_ripemd160 | ✅ PASS | token | ret==0 | - |  |
| st_rsa | ✅ PASS | token | ret==0 | - |  |
| st_sha1 | ✅ PASS | token | ret==0 | - |  |
| st_sha224 | ✅ PASS | token | ret==0 | - |  |
| st_sha256 | ✅ PASS | token | ret==0 | - |  |
| st_sha384 | ✅ PASS | token | ret==0 | - |  |
| st_sha3 | ✅ PASS | token | ret==0 | - |  |
| st_sha512 | ✅ PASS | token | ret==0 | - |  |
| st_memory_buffer_alloc | ✅ PASS | token | NOT_RUN | - |  |

## 汇总

OVERALL: PASS
