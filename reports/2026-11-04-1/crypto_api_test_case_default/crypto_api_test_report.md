# crypto_api_test 测试报告

- 工程: crypto_api_test
- case: case_default
- 日期: 2026-11-04 21:59:50
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=180s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: FAIL (42/44)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| sha224_kat | ✅ PASS | token | digest == `h_sha224_abc` | algo=sha224, bytes=3, kat=1, ret=0, us=10 |  |
| sha224_split | ✅ PASS | token | 13+20+23 分段 digest == `h_sha224_m448` | algo=sha224, bytes=56, segments=3, split_eq_kat=1, ret=0, us=15 |  |
| sha256_kat | ✅ PASS | token | digest == `h_sha256_abc` | algo=sha256, bytes=3, kat=1, ret=0, us=9 |  |
| sha256_pad_boundary | ✅ PASS | token | 四个 digest 分别 == `h_sha256_pad_{0,55,56,64}` | algo=sha256, lens=0_55_56_64, len0=1, len55=1, len56=1, len64=1 |  |
| sha256_split | ✅ PASS | token | 分段 digest == `h_sha256_m448` | algo=sha256, bytes=56, segments=3, split_eq_kat=1, ret=0, us=14 |  |
| sha384_kat | ✅ PASS | token | digest == `h_sha384_abc` | algo=sha384, bytes=3, kat=1, ret=0, us=25 |  |
| sha384_split | ✅ PASS | token | 32+48+32 分段 digest == `h_sha384_m896` | algo=sha384, bytes=112, segments=3, split_eq_kat=1, ret=0, us=42 |  |
| sha512_kat | ✅ PASS | token | digest == `h_sha512_abc` | algo=sha512, bytes=3, kat=1, ret=0, us=24 |  |
| sha512_pad_boundary | ✅ PASS | token | 四个 digest 分别 == `h_sha512_pad_{0,111,112,128}` | algo=sha512, lens=0_111_112_128, len0=1, len111=1, len112=1, len128=1 |  |
| sha512_split | ✅ PASS | token | 分段 digest == `h_sha512_m896` | algo=sha512, bytes=112, segments=3, split_eq_kat=1, ret=0, us=41 |  |
| sm3_kat | ✅ PASS | token | digest == `h_sm3_abc` | algo=sm3, bytes=3, kat=1, ret=0, us=9 |  |
| sm3_split | ✅ PASS | token | 分段 digest == `h_sm3_m448` | algo=sm3, bytes=56, segments=3, split_eq_kat=1, ret=0, us=14 |  |
| hash_bad_algo_sha256 | ✅ PASS | token | 两者均 `ret == -ENOTSUP` | sha384=-134, sha512=-134 |  |
| hash_bad_algo_sha512 | ✅ PASS | token | 三者均 `ret == -ENOTSUP` | sha224=-134, sha256=-134, sm3=-134 |  |
| hash_bad_flags | ✅ PASS | token | `ret == -ENOTSUP` | flags=async\|sep_io, ret=-134, ok=1 |  |
| hash_caps_sha256 | ❌ FAIL | token | caps 含 **RAW_KEY**（wrapper 断言要求，见 #158）/ SYNC_OPS / SEPARATE… | - | CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0`（归档第 113 行；mcause: 11, Environment call from M-mode）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 |
| hash_caps_sha512 | ❌ FAIL | token | caps 含 **RAW_KEY**（wrapper 断言要求，见 #158）/ SYNC_OPS / SEPARATE… | - | CPU 异常（执行期间致命异常，**非断言失败**）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0`（归档第 151 行；mcause: 11, Environment call from M-mode）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 |
| cbc | ✅ PASS | token | 64B 密文 == `sm4_cbc_ct`；解密回环 == 明文 | algo=sm4-cbc, bytes=64, kat=1, roundtrip=1, enc_ret=0, dec_ret=0, us=71 |  |
| ccm | ✅ PASS | token | 64B 密文 == `sm4_ccm_ct`；16B tag == `sm4_ccm_tag`；用该 tag 解密回环… | algo=sm4-ccm, bytes=64, aad=8, kat=1, tag=1, roundtrip=1, enc_ret=0, dec_ret=0, us=130 |  |
| ctr | ✅ PASS | token | 64B 密文 == `sm4_ctr_ct`；解密回环 == 明文 | algo=sm4-ctr, bytes=64, ctr_len=32, kat=1, roundtrip=1, enc_ret=0, dec_ret=0, us=67 |  |
| ecb | ✅ PASS | token | 64B 密文 == `sm4_ecb_ct`；解密回环 == 明文 | algo=sm4-ecb, bytes=64, kat=1, roundtrip=1, enc_ret=0, dec_ret=0, us=64 |  |
| gcm | ✅ PASS | token | 64B 密文 == `sm4_gcm_ct`；16B tag == `sm4_gcm_tag`；用该 tag 解密回环… | algo=sm4-gcm, bytes=64, aad=8, kat=1, tag=1, roundtrip=1, enc_ret=0, dec_ret=0, us=168 |  |
| sm4_bad_algo | ✅ PASS | token | `ret == -ENOTSUP` | algo=aes, ret=-134, ok=1 |  |
| sm4_bad_flags | ✅ PASS | token | `ret == -ENOTSUP` | flags=0x52, ret=-134, ok=1 |  |
| sm4_bad_keylen | ✅ PASS | token | `ret == -ENOTSUP` | keylen=24, ret=-134, ok=1 |  |
| sm4_bad_mode | ✅ PASS | token | `ret == -ENOTSUP` | mode=0, ret=-134, ok=1 |  |
| sm4_caps | ✅ PASS | token | caps 含 RAW_KEY / SYNC_OPS / SEPARATE_IO_BUFS；设备 ready | ready=1, caps=0x132, raw_key=1, sync=1, sep_io=1 |  |
| sm4_ccm_params | ✅ PASS | token | 正对照 `ret==0`；out_buf NULL / in_buf NULL / ad NULL / **out_bu… | base=0, null_out=-22, null_in=-22, null_ad=-22, small_out=-22, tag3=-22, tag5=-22, tag18=-22 |  |
| sm4_ccm_aad_large | ✅ PASS | token | - | mode=ccm, aad_len=64, ret=0, roundtrip=1 |  |
| sm4_ccm_aad_none | ✅ PASS | token | - | mode=ccm, aad_len=0, ret=0, roundtrip=1 |  |
| sm4_ccm_aad_tamper | ✅ PASS | token | - | mode=ccm, tamper=aad, control_ret=0, bad_ret=-77 |  |
| sm4_ccm_aad_unaligned | ✅ PASS | token | - | mode=ccm, aad_len=13, ret=0, roundtrip=1 |  |
| sm4_ccm_noncelen_sweep | ✅ PASS | token | - | nonce_len=13, ret=0, roundtrip=1 |  |
| sm4_ccm_tag_tamper | ✅ PASS | token | - | mode=ccm, tamper=tag, control_ret=0, bad_ret=-77 |  |
| sm4_ccm_taglen_sweep | ✅ PASS | token | - | tag_len=16, ret=0, roundtrip=1 |  |
| sm4_ctr_ctrlen_sweep | ✅ PASS | token | - | ctr_len=128, enc_ret=0, dec_ret=0, roundtrip=1 |  |
| sm4_ctr_len_sweep | ✅ PASS | token | - | len=4081, enc_ret=0, dec_ret=0, out_len=4081 |  |
| sm4_ecb_len_sweep | ✅ PASS | token | - | len=4080, enc_ret=0, dec_ret=0, out_len=4080 |  |
| sm4_ecb_zero_len | ✅ PASS | token | - | ret=0, out_len=0 |  |
| sm4_gcm_aad_large | ✅ PASS | token | - | mode=gcm, aad_len=64, ret=0, roundtrip=1 |  |
| sm4_gcm_aad_none | ✅ PASS | token | - | mode=gcm, aad_len=0, ret=0, roundtrip=1 |  |
| sm4_gcm_aad_tamper | ✅ PASS | token | - | mode=gcm, tamper=aad, control_ret=0, bad_ret=-77 |  |
| sm4_gcm_tag_tamper | ✅ PASS | token | - | mode=gcm, tamper=tag, control_ret=0, bad_ret=-77 |  |
| sm4_stdkey_ecb | ✅ PASS | token | - | ret=0, ct=681edf34d206965e86b3e94f536e4246 |  |

## 汇总

OVERALL: FAIL
