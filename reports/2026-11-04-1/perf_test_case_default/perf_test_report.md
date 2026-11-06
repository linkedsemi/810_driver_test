# perf_test 测试报告

- 工程: perf_test
- case: case_default
- 日期: 2026-11-05 01:10:04
- 板级: lsqsh_evb@2os（cpu1 / cpu2）
- 环境: 无外部接线（TRNG/crypto/DMA 片内；mbox 走共享 SRAM）
- 内存兜底: 不适用（本工程无内存兜底）
- 结果: PASS (32/32)
- 核执行标记: CTRL_EXECUTION=SUCCESSFUL, TGT_EXECUTION=SUCCESSFUL

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| cpu2::peer_alive | ✅ PASS | token | - | - |  |
| cpu2::proxy_erase | ✅ PASS | token | - | mb_s_4k=0.0256 |  |
| cpu2::proxy_read | ✅ PASS | token | - | mb_s_4k=0.0181 |  |
| cpu2::proxy_write | ✅ PASS | token | - | mb_s_4k=0.0798 |  |
| cpu2::rsa2048_modexp | ✅ PASS | token | - | ops_s=15.86 |  |
| cpu2::sha256_otbn | ✅ PASS | token | - | mb_s_1k=4.0157, mb_s_4k=4.6178, mb_s_16k=4.8937, mb_s_32k=5.0135 |  |
| cpu2::sha256_otbn_unaligned | ✅ PASS | token | - | mb_s_1k=4.0000, mb_s_4k=4.6126, mb_s_16k=4.8689, mb_s_32k=4.9906 |  |
| cpu2::sha512_otbn | ✅ PASS | token | - | mb_s_1k=5.1717, mb_s_4k=6.6494, mb_s_16k=6.9987, mb_s_32k=7.4186 |  |
| cpu2::sha512_otbn_unaligned | ✅ PASS | token | - | mb_s_1k=5.1717, mb_s_4k=6.4810, mb_s_16k=6.9987, mb_s_32k=7.4727 |  |
| cpu2::sm3_otbn | ✅ PASS | token | - | mb_s_1k=1.7778, mb_s_4k=1.9961, mb_s_16k=2.0573, mb_s_32k=2.0656 |  |
| cpu2::sm3_otbn_unaligned | ✅ PASS | token | - | mb_s_1k=1.7809, mb_s_4k=1.9816, mb_s_16k=2.0536, mb_s_32k=2.0620 |  |
| cpu1::aes128_cbc | ✅ PASS | token | - | mb_s_1k=13.6533, mb_s_4k=13.9320, mb_s_16k=13.8965, mb_s_32k=13.8203 |  |
| cpu1::aes128_ctr | ✅ PASS | token | - | mb_s_1k=15.0588, mb_s_4k=15.5741, mb_s_16k=15.5446, mb_s_32k=15.4712 |  |
| cpu1::aes128_ecb | ✅ PASS | token | - | mb_s_1k=8.1270, mb_s_4k=8.2084, mb_s_16k=8.1634, mb_s_32k=8.1209 |  |
| cpu1::flash1_erase | ✅ PASS | token | - | mb_s_32k=1.8623 |  |
| cpu1::flash1_read | ✅ PASS | token | - | mb_s_32k=46.8784 |  |
| cpu1::flash1_write | ✅ PASS | token | - | mb_s_32k=0.1225 |  |
| cpu1::flash2_erase | ✅ PASS | token | - | mb_s_32k=0.2454 |  |
| cpu1::flash2_read | ✅ PASS | token | - | mb_s_32k=22.0215 |  |
| cpu1::flash2_write | ✅ PASS | token | - | mb_s_32k=0.4083 |  |
| cpu1::mem2mem | ✅ PASS | token | - | mb_s_1k=21.3333, mb_s_4k=22.3825, mb_s_16k=22.5986 |  |
| cpu1::roundtrip | ✅ PASS | token | - | mb_s_31k=1.1862, ops_s=18534.99 |  |
| cpu1::sha256 | ✅ PASS | token | - | mb_s_1k=23.2727, mb_s_4k=1.6126, mb_s_16k=64.0000, mb_s_32k=69.7191 |  |
| cpu1::sha256_unaligned | ✅ PASS | token | - | mb_s_1k=11.3778, mb_s_4k=12.4121, mb_s_16k=12.8502, mb_s_32k=12.8906 |  |
| cpu1::sha512 | ✅ PASS | token | - | mb_s_1k=24.3810, mb_s_4k=77.2830, mb_s_16k=192.7529, mb_s_32k=248.2424 |  |
| cpu1::sha512_unaligned | ✅ PASS | token | - | mb_s_1k=16.7869, mb_s_4k=24.8242, mb_s_16k=28.3460, mb_s_32k=28.9214 |  |
| cpu1::sm3 | ✅ PASS | token | - | mb_s_1k=24.3810, mb_s_4k=53.8947, mb_s_16k=64.2510, mb_s_32k=69.8678 |  |
| cpu1::sm3_unaligned | ✅ PASS | token | - | mb_s_1k=11.3778, mb_s_4k=12.4121, mb_s_16k=12.8502, mb_s_32k=12.8906 |  |
| cpu1::sm4_ctr | ✅ PASS | token | - | mb_s_1k=13.1282, mb_s_4k=13.4737, mb_s_16k=13.3203, mb_s_32k=13.2342 |  |
| cpu1::sm4_ecb | ✅ PASS | token | - | mb_s_1k=44.5217, mb_s_4k=46.5455, mb_s_16k=195.0476, mb_s_32k=390.0952 |  |
| cpu1::trng0_throughput | ✅ PASS | token | - | mb_s_4k=0.0005 |  |
| cpu1::trng1_throughput | ✅ PASS | token | - | mb_s_32k=7.1530 |  |

## 汇总

OVERALL: PASS
