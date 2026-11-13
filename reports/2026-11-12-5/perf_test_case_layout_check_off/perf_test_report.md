# perf_test (WSL) 测试报告

- 工程: perf_test
- case: case_layout_check_off
- 日期: 2026-11-13 18:36:58
- 板级: lsqsh_evb@2os
- 环境: tgt_uart=COM31, ctrl_uart=COM14, timeout=240s, host=wsl, gdb=172.24.48.1:1025

## case_layout_check_off: PASS (cpu1 21/21, cpu2 11/11) CTRL=SUCCESSFUL, TGT=SUCCESSFUL

- 采集时长: 26.3s（timed_out=False；**不参与判定**）

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
PERF crypto | aes128_cbc | bytes=16384 | MB/s=13.8730 | KB/s=13872.99 | us=1181
PERF crypto | aes128_cbc | bytes=32768 | MB/s=13.8378 | KB/s=13837.84 | us=2368
PERF crypto | aes128_ctr | bytes=1024 | MB/s=14.8406 | KB/s=14840.58 | us=69
PERF crypto | aes128_ctr | bytes=4096 | MB/s=15.5741 | KB/s=15574.14 | us=263
PERF crypto | aes128_ctr | bytes=16384 | MB/s=15.5152 | KB/s=15515.15 | us=1056
PERF crypto | aes128_ctr | bytes=32768 | MB/s=15.4931 | KB/s=15493.14 | us=2115
PERF crypto | aes128_ecb | bytes=1024 | MB/s=9.7524 | KB/s=9752.38 | us=105
PERF crypto | aes128_ecb | bytes=4096 | MB/s=9.8937 | KB/s=9893.72 | us=414
PERF crypto | aes128_ecb | bytes=16384 | MB/s=9.7990 | KB/s=9799.04 | us=1672
PERF crypto | aes128_ecb | bytes=32768 | MB/s=9.7408 | KB/s=9740.78 | us=3364
PERF crypto | sha256 | bytes=1024 | MB/s=23.8140 | KB/s=23813.95 | us=43
PERF crypto | sha256 | bytes=4096 | MB/s=1.6031 | KB/s=1603.13 | us=2555
PERF crypto | sha256 | bytes=16384 | MB/s=64.0000 | KB/s=64000.00 | us=256
PERF crypto | sha256 | bytes=32768 | MB/s=69.8678 | KB/s=69867.80 | us=469
PERF crypto | sha256_unaligned | bytes=1024 | MB/s=11.3778 | KB/s=11377.78 | us=90
PERF crypto | sha256_unaligned | bytes=4096 | MB/s=12.4121 | KB/s=12412.12 | us=330
PERF crypto | sha256_unaligned | bytes=16384 | MB/s=12.8301 | KB/s=12830.07 | us=1277
PERF crypto | sha256_unaligned | bytes=32768 | MB/s=12.8200 | KB/s=12820.03 | us=2556
PERF crypto | sha512 | bytes=1024 | MB/s=23.8140 | KB/s=23813.95 | us=43
PERF crypto | sha512 | bytes=4096 | MB/s=75.8519 | KB/s=75851.85 | us=54
PERF crypto | sha512 | bytes=16384 | MB/s=186.1818 | KB/s=186181.82 | us=88
PERF crypto | sha512 | bytes=32768 | MB/s=246.3759 | KB/s=246375.94 | us=133
PERF crypto | sha512_unaligned | bytes=1024 | MB/s=16.7869 | KB/s=16786.89 | us=61
PERF crypto | sha512_unaligned | bytes=4096 | MB/s=24.8242 | KB/s=24824.24 | us=165
PERF crypto | sha512_unaligned | bytes=16384 | MB/s=28.1512 | KB/s=28151.20 | us=582
PERF crypto | sha512_unaligned | bytes=32768 | MB/s=28.7691 | KB/s=28769.10 | us=1139
PERF crypto | sm3 | bytes=1024 | MB/s=23.8140 | KB/s=23813.95 | us=43
PERF crypto | sm3 | bytes=4096 | MB/s=53.1948 | KB/s=53194.81 | us=77
PERF crypto | sm3 | bytes=16384 | MB/s=64.2510 | KB/s=64250.98 | us=255
PERF crypto | sm3 | bytes=32768 | MB/s=69.7191 | KB/s=69719.15 | us=470
PERF crypto | sm3_unaligned | bytes=1024 | MB/s=11.3778 | KB/s=11377.78 | us=90
PERF crypto | sm3_unaligned | bytes=4096 | MB/s=12.4121 | KB/s=12412.12 | us=330
PERF crypto | sm3_unaligned | bytes=16384 | MB/s=12.8301 | KB/s=12830.07 | us=1277
PERF crypto | sm3_unaligned | bytes=32768 | MB/s=12.8150 | KB/s=12815.02 | us=2557
PERF crypto | sm4_ctr | bytes=1024 | MB/s=13.1282 | KB/s=13128.21 | us=78
PERF crypto | sm4_ctr | bytes=4096 | MB/s=13.2987 | KB/s=13298.70 | us=308
PERF crypto | sm4_ctr | bytes=16384 | MB/s=13.2342 | KB/s=13234.25 | us=1238
PERF crypto | sm4_ctr | bytes=32768 | MB/s=13.1651 | KB/s=13165.13 | us=2489
PERF crypto | sm4_ecb | bytes=1024 | MB/s=44.5217 | KB/s=44521.74 | us=23
PERF crypto | sm4_ecb | bytes=4096 | MB/s=46.5455 | KB/s=46545.45 | us=88
PERF crypto | sm4_ecb | bytes=16384 | MB/s=195.0476 | KB/s=195047.62 | us=84
PERF crypto | sm4_ecb | bytes=32768 | MB/s=390.0952 | KB/s=390095.24 | us=84
PERF dma | mem2mem | bytes=1024 | MB/s=20.4800 | KB/s=20480.00 | us=50
PERF dma | mem2mem | bytes=4096 | MB/s=20.6869 | KB/s=20686.87 | us=198
PERF dma | mem2mem | bytes=16384 | MB/s=20.7392 | KB/s=20739.24 | us=790
PERF flash | qspi1_erase | bytes=32768 | MB/s=1.8832 | KB/s=1883.22 | us=17400
PERF flash | qspi1_read | bytes=32768 | MB/s=47.1482 | KB/s=47148.20 | us=695
PERF flash | qspi1_write | bytes=32768 | MB/s=0.1235 | KB/s=123.50 | us=265330
PERF flash | qspi2_erase | bytes=32768 | MB/s=0.1311 | KB/s=131.09 | us=249975
PERF flash | qspi2_read | bytes=32768 | MB/s=21.9184 | KB/s=21918.39 | us=1495
PERF flash | qspi2_write | bytes=32768 | MB/s=0.5604 | KB/s=560.38 | us=58475
PERF mbox | roundtrip_64B | bytes=32000 | MB/s=2.7098 | KB/s=2709.80 | us=11809
PERF mbox | roundtrip | ops=500 | ops/s=42340.59 | us=11809
PERF trng | trng0_apb | bytes=4096 | MB/s=0.0002 | KB/s=0.19 | us=21815118
PERF trng | trng1_dw | bytes=32768 | MB/s=5.4989 | KB/s=5498.91 | us=5959
PERF crypto | sha256_otbn | bytes=1024 | MB/s=3.9385 | KB/s=3938.46 | us=260
PERF crypto | sha256_otbn | bytes=4096 | MB/s=4.6022 | KB/s=4602.25 | us=890
PERF crypto | sha256_otbn | bytes=16384 | MB/s=4.9054 | KB/s=4905.39 | us=3340
PERF crypto | sha256_otbn | bytes=32768 | MB/s=5.0066 | KB/s=5006.57 | us=6545
PERF crypto | sha256_otbn_unaligned | bytes=1024 | MB/s=3.9084 | KB/s=3908.40 | us=262
PERF crypto | sha256_otbn_unaligned | bytes=4096 | MB/s=4.7407 | KB/s=4740.74 | us=864
PERF crypto | sha256_otbn_unaligned | bytes=16384 | MB/s=4.8202 | KB/s=4820.24 | us=3399
PERF crypto | sha256_otbn_unaligned | bytes=32768 | MB/s=4.9875 | KB/s=4987.52 | us=6570
PERF crypto | sha512_otbn | bytes=1024 | MB/s=5.0945 | KB/s=5094.53 | us=201
PERF crypto | sha512_otbn | bytes=4096 | MB/s=6.4504 | KB/s=6450.39 | us=635
PERF crypto | sha512_otbn | bytes=16384 | MB/s=6.9779 | KB/s=6977.85 | us=2348
PERF crypto | sha512_otbn | bytes=32768 | MB/s=7.4710 | KB/s=7471.04 | us=4386
PERF crypto | sha512_otbn_unaligned | bytes=1024 | MB/s=4.7407 | KB/s=4740.74 | us=216
PERF crypto | sha512_otbn_unaligned | bytes=4096 | MB/s=6.6171 | KB/s=6617.12 | us=619
PERF crypto | sha512_otbn_unaligned | bytes=16384 | MB/s=6.9779 | KB/s=6977.85 | us=2348
PERF crypto | sha512_otbn_unaligned | bytes=32768 | MB/s=7.3952 | KB/s=7395.17 | us=4431
PERF crypto | sm3_otbn | bytes=1024 | MB/s=1.7747 | KB/s=1774.70 | us=577
PERF crypto | sm3_otbn | bytes=4096 | MB/s=1.9807 | KB/s=1980.66 | us=2068
PERF crypto | sm3_otbn | bytes=16384 | MB/s=2.0560 | KB/s=2055.97 | us=7969
PERF crypto | sm3_otbn | bytes=32768 | MB/s=2.0645 | KB/s=2064.52 | us=15872
PERF crypto | sm3_otbn_unaligned | bytes=1024 | MB/s=1.7747 | KB/s=1774.70 | us=577
PERF crypto | sm3_otbn_unaligned | bytes=4096 | MB/s=1.9797 | KB/s=1979.70 | us=2069
PERF crypto | sm3_otbn_unaligned | bytes=16384 | MB/s=2.0526 | KB/s=2052.62 | us=7982
PERF crypto | sm3_otbn_unaligned | bytes=32768 | MB/s=2.0611 | KB/s=2061.14 | us=15898
PERF crypto | rsa2048_modexp | ops=1 | ops/s=15.86 | us=63044
PERF flash_proxy | erase | bytes=4096 | MB/s=0.0144 | KB/s=14.36 | us=285312
PERF flash_proxy | read | bytes=4096 | MB/s=0.0169 | KB/s=16.92 | us=242029
PERF flash_proxy | write | bytes=4096 | MB/s=0.2795 | KB/s=279.50 | us=14655
```

## 汇总

OVERALL: PASS
