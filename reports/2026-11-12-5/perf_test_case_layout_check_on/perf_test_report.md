# perf_test (WSL) 测试报告

- 工程: perf_test
- case: case_layout_check_on
- 日期: 2026-11-13 18:29:20
- 板级: lsqsh_evb@2os
- 环境: tgt_uart=COM31, ctrl_uart=COM14, timeout=240s, host=wsl, gdb=172.24.48.1:1025

## case_layout_check_on: PASS (cpu1 21/21, cpu2 11/11) CTRL=SUCCESSFUL, TGT=SUCCESSFUL

- 采集时长: 23.4s（timed_out=False；**不参与判定**）

| 测试项 | 核 | 类别 | 结果 |
|---|---|---|---|
| cpu2::sha256_otbn | CTRL | 性能/功能 | PASS |
| cpu2::sha256_otbn_unaligned | CTRL | 性能/功能 | PASS |
| cpu2::sha512_otbn | CTRL | 性能/功能 | PASS |
| cpu2::sha512_otbn_unaligned | CTRL | 性能/功能 | PASS |
| cpu2::sm3_otbn | CTRL | 性能/功能 | PASS |
| cpu2::sm3_otbn_unaligned | CTRL | 性能/功能 | PASS |
| cpu2::rsa2048_modexp | CTRL | 性能/功能 | PASS |
| cpu2::proxy_erase | CTRL | 性能/功能 | PASS |
| cpu2::proxy_read | CTRL | 性能/功能 | PASS |
| cpu2::proxy_write | CTRL | 性能/功能 | PASS |
| cpu2::peer_alive | CTRL | 性能/功能 | PASS |
| cpu1::aes128_cbc | TGT | 性能/功能 | PASS |
| cpu1::aes128_ctr | TGT | 性能/功能 | PASS |
| cpu1::aes128_ecb | TGT | 性能/功能 | PASS |
| cpu1::sha256 | TGT | 性能/功能 | PASS |
| cpu1::sha256_unaligned | TGT | 性能/功能 | PASS |
| cpu1::sha512 | TGT | 性能/功能 | PASS |
| cpu1::sha512_unaligned | TGT | 性能/功能 | PASS |
| cpu1::sm3 | TGT | 性能/功能 | PASS |
| cpu1::sm3_unaligned | TGT | 性能/功能 | PASS |
| cpu1::sm4_ctr | TGT | 性能/功能 | PASS |
| cpu1::sm4_ecb | TGT | 性能/功能 | PASS |
| cpu1::mem2mem | TGT | 性能/功能 | PASS |
| cpu1::flash1_erase | TGT | 性能/功能 | PASS |
| cpu1::flash1_read | TGT | 性能/功能 | PASS |
| cpu1::flash1_write | TGT | 性能/功能 | PASS |
| cpu1::flash2_erase | TGT | 性能/功能 | PASS |
| cpu1::flash2_read | TGT | 性能/功能 | PASS |
| cpu1::flash2_write | TGT | 性能/功能 | PASS |
| cpu1::roundtrip | TGT | 性能/功能 | PASS |
| cpu1::trng0_throughput | TGT | 性能/功能 | PASS |
| cpu1::trng1_throughput | TGT | 性能/功能 | PASS |

## 性能数据（PERF）

```text
PERF crypto | aes128_cbc | bytes=1024 | MB/s=13.6533 | KB/s=13653.33 | us=75
PERF crypto | aes128_cbc | bytes=4096 | MB/s=13.9320 | KB/s=13931.97 | us=294
PERF crypto | aes128_cbc | bytes=16384 | MB/s=13.8495 | KB/s=13849.54 | us=1183
PERF crypto | aes128_cbc | bytes=32768 | MB/s=13.8203 | KB/s=13820.33 | us=2371
PERF crypto | aes128_ctr | bytes=1024 | MB/s=15.0588 | KB/s=15058.82 | us=68
PERF crypto | aes128_ctr | bytes=4096 | MB/s=15.5152 | KB/s=15515.15 | us=264
PERF crypto | aes128_ctr | bytes=16384 | MB/s=15.4858 | KB/s=15485.82 | us=1058
PERF crypto | aes128_ctr | bytes=32768 | MB/s=15.4639 | KB/s=15463.90 | us=2119
PERF crypto | aes128_ecb | bytes=1024 | MB/s=9.8462 | KB/s=9846.15 | us=104
PERF crypto | aes128_ecb | bytes=4096 | MB/s=9.9177 | KB/s=9917.68 | us=413
PERF crypto | aes128_ecb | bytes=16384 | MB/s=9.8462 | KB/s=9846.15 | us=1664
PERF crypto | aes128_ecb | bytes=32768 | MB/s=9.7786 | KB/s=9778.57 | us=3351
PERF crypto | sha256 | bytes=1024 | MB/s=23.2727 | KB/s=23272.73 | us=44
PERF crypto | sha256 | bytes=4096 | MB/s=1.6031 | KB/s=1603.13 | us=2555
PERF crypto | sha256 | bytes=16384 | MB/s=63.7510 | KB/s=63750.97 | us=257
PERF crypto | sha256 | bytes=32768 | MB/s=69.5711 | KB/s=69571.13 | us=471
PERF crypto | sha256_unaligned | bytes=1024 | MB/s=11.3778 | KB/s=11377.78 | us=90
PERF crypto | sha256_unaligned | bytes=4096 | MB/s=12.4121 | KB/s=12412.12 | us=330
PERF crypto | sha256_unaligned | bytes=16384 | MB/s=12.8502 | KB/s=12850.20 | us=1275
PERF crypto | sha256_unaligned | bytes=32768 | MB/s=12.8856 | KB/s=12885.57 | us=2543
PERF crypto | sha512 | bytes=1024 | MB/s=23.2727 | KB/s=23272.73 | us=44
PERF crypto | sha512 | bytes=4096 | MB/s=73.1429 | KB/s=73142.86 | us=56
PERF crypto | sha512 | bytes=16384 | MB/s=188.3218 | KB/s=188321.84 | us=87
PERF crypto | sha512 | bytes=32768 | MB/s=239.1825 | KB/s=239182.48 | us=137
PERF crypto | sha512_unaligned | bytes=1024 | MB/s=16.5161 | KB/s=16516.13 | us=62
PERF crypto | sha512_unaligned | bytes=4096 | MB/s=24.3810 | KB/s=24380.95 | us=168
PERF crypto | sha512_unaligned | bytes=16384 | MB/s=27.7695 | KB/s=27769.49 | us=590
PERF crypto | sha512_unaligned | bytes=32768 | MB/s=28.3951 | KB/s=28395.15 | us=1154
PERF crypto | sm3 | bytes=1024 | MB/s=20.8980 | KB/s=20897.96 | us=49
PERF crypto | sm3 | bytes=4096 | MB/s=53.1948 | KB/s=53194.81 | us=77
PERF crypto | sm3 | bytes=16384 | MB/s=64.0000 | KB/s=64000.00 | us=256
PERF crypto | sm3 | bytes=32768 | MB/s=69.4237 | KB/s=69423.73 | us=472
PERF crypto | sm3_unaligned | bytes=1024 | MB/s=11.5056 | KB/s=11505.62 | us=89
PERF crypto | sm3_unaligned | bytes=4096 | MB/s=12.4498 | KB/s=12449.85 | us=329
PERF crypto | sm3_unaligned | bytes=16384 | MB/s=12.8502 | KB/s=12850.20 | us=1275
PERF crypto | sm3_unaligned | bytes=32768 | MB/s=12.8250 | KB/s=12825.05 | us=2555
PERF crypto | sm4_ctr | bytes=1024 | MB/s=13.1282 | KB/s=13128.21 | us=78
PERF crypto | sm4_ctr | bytes=4096 | MB/s=13.2987 | KB/s=13298.70 | us=308
PERF crypto | sm4_ctr | bytes=16384 | MB/s=13.2557 | KB/s=13255.66 | us=1236
PERF crypto | sm4_ctr | bytes=32768 | MB/s=13.1651 | KB/s=13165.13 | us=2489
PERF crypto | sm4_ecb | bytes=1024 | MB/s=44.5217 | KB/s=44521.74 | us=23
PERF crypto | sm4_ecb | bytes=4096 | MB/s=46.5455 | KB/s=46545.45 | us=88
PERF crypto | sm4_ecb | bytes=16384 | MB/s=197.3976 | KB/s=197397.59 | us=83
PERF crypto | sm4_ecb | bytes=32768 | MB/s=394.7952 | KB/s=394795.18 | us=83
PERF dma | mem2mem | bytes=1024 | MB/s=22.7556 | KB/s=22755.56 | us=45
PERF dma | mem2mem | bytes=4096 | MB/s=23.6763 | KB/s=23676.30 | us=173
PERF dma | mem2mem | bytes=16384 | MB/s=23.9182 | KB/s=23918.25 | us=685
PERF flash | qspi1_erase | bytes=32768 | MB/s=1.8827 | KB/s=1882.68 | us=17405
PERF flash | qspi1_read | bytes=32768 | MB/s=47.1482 | KB/s=47148.20 | us=695
PERF flash | qspi1_write | bytes=32768 | MB/s=0.1203 | KB/s=120.32 | us=272332
PERF flash | qspi2_erase | bytes=32768 | MB/s=0.1311 | KB/s=131.09 | us=249974
PERF flash | qspi2_read | bytes=32768 | MB/s=21.8891 | KB/s=21889.11 | us=1497
PERF flash | qspi2_write | bytes=32768 | MB/s=0.5624 | KB/s=562.38 | us=58267
PERF mbox | roundtrip_64B | bytes=32000 | MB/s=2.0197 | KB/s=2019.69 | us=15844
PERF mbox | roundtrip | ops=500 | ops/s=31557.69 | us=15844
PERF trng | trng0_apb | bytes=4096 | MB/s=0.0002 | KB/s=0.20 | us=20951266
PERF trng | trng1_dw | bytes=32768 | MB/s=5.2911 | KB/s=5291.14 | us=6193
PERF crypto | sha256_otbn | bytes=1024 | MB/s=4.0000 | KB/s=4000.00 | us=256
PERF crypto | sha256_otbn | bytes=4096 | MB/s=4.6126 | KB/s=4612.61 | us=888
PERF crypto | sha256_otbn | bytes=16384 | MB/s=4.8878 | KB/s=4887.83 | us=3352
PERF crypto | sha256_otbn | bytes=32768 | MB/s=5.0104 | KB/s=5010.40 | us=6540
PERF crypto | sha256_otbn_unaligned | bytes=1024 | MB/s=3.9844 | KB/s=3984.44 | us=257
PERF crypto | sha256_otbn_unaligned | bytes=4096 | MB/s=4.6074 | KB/s=4607.42 | us=889
PERF crypto | sha256_otbn_unaligned | bytes=16384 | MB/s=4.8617 | KB/s=4861.72 | us=3370
PERF crypto | sha256_otbn_unaligned | bytes=32768 | MB/s=4.9913 | KB/s=4991.32 | us=6565
PERF crypto | sha512_otbn | bytes=1024 | MB/s=5.1457 | KB/s=5145.73 | us=199
PERF crypto | sha512_otbn | bytes=4096 | MB/s=6.6278 | KB/s=6627.83 | us=618
PERF crypto | sha512_otbn | bytes=16384 | MB/s=6.9957 | KB/s=6995.73 | us=2342
PERF crypto | sha512_otbn | bytes=32768 | MB/s=7.4388 | KB/s=7438.82 | us=4405
PERF crypto | sha512_otbn_unaligned | bytes=1024 | MB/s=4.9951 | KB/s=4995.12 | us=205
PERF crypto | sha512_otbn_unaligned | bytes=4096 | MB/s=6.4708 | KB/s=6470.77 | us=633
PERF crypto | sha512_otbn_unaligned | bytes=16384 | MB/s=6.9957 | KB/s=6995.73 | us=2342
PERF crypto | sha512_otbn_unaligned | bytes=32768 | MB/s=7.4898 | KB/s=7489.83 | us=4375
PERF crypto | sm3_otbn | bytes=1024 | MB/s=1.8124 | KB/s=1812.39 | us=565
PERF crypto | sm3_otbn | bytes=4096 | MB/s=1.9961 | KB/s=1996.10 | us=2052
PERF crypto | sm3_otbn | bytes=16384 | MB/s=2.0567 | KB/s=2056.74 | us=7966
PERF crypto | sm3_otbn | bytes=32768 | MB/s=2.0652 | KB/s=2065.17 | us=15867
PERF crypto | sm3_otbn_unaligned | bytes=1024 | MB/s=1.7778 | KB/s=1777.78 | us=576
PERF crypto | sm3_otbn_unaligned | bytes=4096 | MB/s=1.9787 | KB/s=1978.74 | us=2070
PERF crypto | sm3_otbn_unaligned | bytes=16384 | MB/s=2.0534 | KB/s=2053.39 | us=7979
PERF crypto | sm3_otbn_unaligned | bytes=32768 | MB/s=2.0618 | KB/s=2061.79 | us=15893
PERF crypto | rsa2048_modexp | ops=1 | ops/s=15.86 | us=63041
PERF flash_proxy | erase | bytes=4096 | MB/s=0.0139 | KB/s=13.95 | us=293710
PERF flash_proxy | read | bytes=4096 | MB/s=0.0167 | KB/s=16.71 | us=245187
PERF flash_proxy | write | bytes=4096 | MB/s=0.2781 | KB/s=278.15 | us=14726
```

## 汇总

OVERALL: PASS
