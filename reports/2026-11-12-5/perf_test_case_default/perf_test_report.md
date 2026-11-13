# perf_test (WSL) 测试报告

- 工程: perf_test
- case: case_default
- 日期: 2026-11-13 02:12:06
- 板级: lsqsh_evb@2os
- 环境: tgt_uart=COM31, ctrl_uart=COM14, timeout=240s, host=wsl, gdb=172.24.48.1:1025

## case_default: PASS (cpu1 21/21, cpu2 11/11) CTRL=SUCCESSFUL, TGT=SUCCESSFUL

- 采集时长: 29.1s（timed_out=False；**不参与判定**）

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
PERF crypto | aes128_cbc | bytes=4096 | MB/s=13.8378 | KB/s=13837.84 | us=296
PERF crypto | aes128_cbc | bytes=16384 | MB/s=13.9201 | KB/s=13920.14 | us=1177
PERF crypto | aes128_cbc | bytes=32768 | MB/s=13.8087 | KB/s=13808.68 | us=2373
PERF crypto | aes128_ctr | bytes=1024 | MB/s=15.0588 | KB/s=15058.82 | us=68
PERF crypto | aes128_ctr | bytes=4096 | MB/s=15.3985 | KB/s=15398.50 | us=266
PERF crypto | aes128_ctr | bytes=16384 | MB/s=15.5741 | KB/s=15574.14 | us=1052
PERF crypto | aes128_ctr | bytes=32768 | MB/s=15.4420 | KB/s=15442.04 | us=2122
PERF crypto | aes128_ecb | bytes=1024 | MB/s=9.3091 | KB/s=9309.09 | us=110
PERF crypto | aes128_ecb | bytes=4096 | MB/s=9.3730 | KB/s=9373.00 | us=437
PERF crypto | aes128_ecb | bytes=16384 | MB/s=9.3945 | KB/s=9394.50 | us=1744
PERF crypto | aes128_ecb | bytes=32768 | MB/s=9.3383 | KB/s=9338.27 | us=3509
PERF crypto | sha256 | bytes=1024 | MB/s=23.2727 | KB/s=23272.73 | us=44
PERF crypto | sha256 | bytes=4096 | MB/s=1.6145 | KB/s=1614.51 | us=2537
PERF crypto | sha256 | bytes=16384 | MB/s=64.0000 | KB/s=64000.00 | us=256
PERF crypto | sha256 | bytes=32768 | MB/s=69.7191 | KB/s=69719.15 | us=470
PERF crypto | sha256_unaligned | bytes=1024 | MB/s=11.3778 | KB/s=11377.78 | us=90
PERF crypto | sha256_unaligned | bytes=4096 | MB/s=12.4498 | KB/s=12449.85 | us=329
PERF crypto | sha256_unaligned | bytes=16384 | MB/s=12.8603 | KB/s=12860.28 | us=1274
PERF crypto | sha256_unaligned | bytes=32768 | MB/s=12.8452 | KB/s=12845.16 | us=2551
PERF crypto | sha512 | bytes=1024 | MB/s=23.8140 | KB/s=23813.95 | us=43
PERF crypto | sha512 | bytes=4096 | MB/s=77.2830 | KB/s=77283.02 | us=53
PERF crypto | sha512 | bytes=16384 | MB/s=186.1818 | KB/s=186181.82 | us=88
PERF crypto | sha512 | bytes=32768 | MB/s=246.3759 | KB/s=246375.94 | us=133
PERF crypto | sha512_unaligned | bytes=1024 | MB/s=16.7869 | KB/s=16786.89 | us=61
PERF crypto | sha512_unaligned | bytes=4096 | MB/s=24.6747 | KB/s=24674.70 | us=166
PERF crypto | sha512_unaligned | bytes=16384 | MB/s=27.9590 | KB/s=27959.04 | us=586
PERF crypto | sha512_unaligned | bytes=32768 | MB/s=28.5684 | KB/s=28568.44 | us=1147
PERF crypto | sm3 | bytes=1024 | MB/s=20.4800 | KB/s=20480.00 | us=50
PERF crypto | sm3 | bytes=4096 | MB/s=53.1948 | KB/s=53194.81 | us=77
PERF crypto | sm3 | bytes=16384 | MB/s=63.5039 | KB/s=63503.88 | us=258
PERF crypto | sm3 | bytes=32768 | MB/s=69.5711 | KB/s=69571.13 | us=471
PERF crypto | sm3_unaligned | bytes=1024 | MB/s=11.5056 | KB/s=11505.62 | us=89
PERF crypto | sm3_unaligned | bytes=4096 | MB/s=12.4498 | KB/s=12449.85 | us=329
PERF crypto | sm3_unaligned | bytes=16384 | MB/s=12.8301 | KB/s=12830.07 | us=1277
PERF crypto | sm3_unaligned | bytes=32768 | MB/s=12.8754 | KB/s=12875.44 | us=2545
PERF crypto | sm4_ctr | bytes=1024 | MB/s=13.1282 | KB/s=13128.21 | us=78
PERF crypto | sm4_ctr | bytes=4096 | MB/s=13.2987 | KB/s=13298.70 | us=308
PERF crypto | sm4_ctr | bytes=16384 | MB/s=13.2236 | KB/s=13223.57 | us=1239
PERF crypto | sm4_ctr | bytes=32768 | MB/s=13.1651 | KB/s=13165.13 | us=2489
PERF crypto | sm4_ecb | bytes=1024 | MB/s=44.5217 | KB/s=44521.74 | us=23
PERF crypto | sm4_ecb | bytes=4096 | MB/s=46.0225 | KB/s=46022.47 | us=89
PERF crypto | sm4_ecb | bytes=16384 | MB/s=197.3976 | KB/s=197397.59 | us=83
PERF crypto | sm4_ecb | bytes=32768 | MB/s=394.7952 | KB/s=394795.18 | us=83
PERF dma | mem2mem | bytes=1024 | MB/s=21.7872 | KB/s=21787.23 | us=47
PERF dma | mem2mem | bytes=4096 | MB/s=22.6298 | KB/s=22629.83 | us=181
PERF dma | mem2mem | bytes=16384 | MB/s=22.6611 | KB/s=22661.13 | us=723
PERF flash | qspi1_erase | bytes=32768 | MB/s=1.8822 | KB/s=1882.24 | us=17409
PERF flash | qspi1_read | bytes=32768 | MB/s=47.1482 | KB/s=47148.20 | us=695
PERF flash | qspi1_write | bytes=32768 | MB/s=0.1209 | KB/s=120.95 | us=270933
PERF flash | qspi2_erase | bytes=32768 | MB/s=0.1267 | KB/s=126.67 | us=258686
PERF flash | qspi2_read | bytes=32768 | MB/s=21.9184 | KB/s=21918.39 | us=1495
PERF flash | qspi2_write | bytes=32768 | MB/s=0.6163 | KB/s=616.29 | us=53170
PERF mbox | roundtrip_64B | bytes=32000 | MB/s=1.9802 | KB/s=1980.20 | us=16160
PERF mbox | roundtrip | ops=500 | ops/s=30940.59 | us=16160
PERF trng | trng0_apb | bytes=4096 | MB/s=0.0002 | KB/s=0.17 | us=24473546
PERF trng | trng1_dw | bytes=32768 | MB/s=3.5540 | KB/s=3554.01 | us=9220
PERF crypto | sha256_otbn | bytes=1024 | MB/s=3.8496 | KB/s=3849.62 | us=266
PERF crypto | sha256_otbn | bytes=4096 | MB/s=4.6126 | KB/s=4612.61 | us=888
PERF crypto | sha256_otbn | bytes=16384 | MB/s=4.8019 | KB/s=4801.88 | us=3412
PERF crypto | sha256_otbn | bytes=32768 | MB/s=5.0096 | KB/s=5009.63 | us=6541
PERF crypto | sha256_otbn_unaligned | bytes=1024 | MB/s=3.9385 | KB/s=3938.46 | us=260
PERF crypto | sha256_otbn_unaligned | bytes=4096 | MB/s=4.7517 | KB/s=4751.74 | us=862
PERF crypto | sha256_otbn_unaligned | bytes=16384 | MB/s=4.8245 | KB/s=4824.50 | us=3396
PERF crypto | sha256_otbn_unaligned | bytes=32768 | MB/s=4.9906 | KB/s=4990.56 | us=6566
PERF crypto | sha512_otbn | bytes=1024 | MB/s=5.1457 | KB/s=5145.73 | us=199
PERF crypto | sha512_otbn | bytes=4096 | MB/s=6.4708 | KB/s=6470.77 | us=633
PERF crypto | sha512_otbn | bytes=16384 | MB/s=6.9987 | KB/s=6998.72 | us=2341
PERF crypto | sha512_otbn | bytes=32768 | MB/s=7.4762 | KB/s=7476.16 | us=4383
PERF crypto | sha512_otbn_unaligned | bytes=1024 | MB/s=4.7850 | KB/s=4785.05 | us=214
PERF crypto | sha512_otbn_unaligned | bytes=4096 | MB/s=6.8040 | KB/s=6803.99 | us=602
PERF crypto | sha512_otbn_unaligned | bytes=16384 | MB/s=6.9957 | KB/s=6995.73 | us=2342
PERF crypto | sha512_otbn_unaligned | bytes=32768 | MB/s=7.3885 | KB/s=7388.50 | us=4435
PERF crypto | sm3_otbn | bytes=1024 | MB/s=1.8028 | KB/s=1802.82 | us=568
PERF crypto | sm3_otbn | bytes=4096 | MB/s=1.9826 | KB/s=1982.58 | us=2066
PERF crypto | sm3_otbn | bytes=16384 | MB/s=2.0567 | KB/s=2056.74 | us=7966
PERF crypto | sm3_otbn | bytes=32768 | MB/s=2.0652 | KB/s=2065.17 | us=15867
PERF crypto | sm3_otbn_unaligned | bytes=1024 | MB/s=1.7996 | KB/s=1799.65 | us=569
PERF crypto | sm3_otbn_unaligned | bytes=4096 | MB/s=1.9816 | KB/s=1981.62 | us=2067
PERF crypto | sm3_otbn_unaligned | bytes=16384 | MB/s=2.0534 | KB/s=2053.39 | us=7979
PERF crypto | sm3_otbn_unaligned | bytes=32768 | MB/s=2.0618 | KB/s=2061.79 | us=15893
PERF crypto | rsa2048_modexp | ops=1 | ops/s=15.86 | us=63041
PERF flash_proxy | erase | bytes=4096 | MB/s=0.0140 | KB/s=13.99 | us=292698
PERF flash_proxy | read | bytes=4096 | MB/s=0.0170 | KB/s=17.01 | us=240781
PERF flash_proxy | write | bytes=4096 | MB/s=0.2974 | KB/s=297.35 | us=13775
```

## 汇总

OVERALL: PASS
