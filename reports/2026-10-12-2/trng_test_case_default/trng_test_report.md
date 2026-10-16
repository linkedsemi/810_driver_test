# TRNG 驱动测试报告

- 工程: trng_test
- case: case_default
- 日期: 2026-10-12 22:55:50
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 环境: tgt_uart=COM16, timeout=30s
- 执行标记: SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (15/15)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| autocorr_8k | ✅ PASS | token | 前 8 个 lag 的 \|R(d)\| < 6/√n = 0.02344 | max_abs_r=0.01045, lag=1, gate=0.02344 |  |
| basic_16 | ✅ PASS | token | ret==0，且输出**至少一个字节 ≠ 预置哨兵 0xA5**（真写入） | - |  |
| block_freq_8k | ✅ PASS | token | 分 128 块×512bit 的 χ² 落在 [32, 224]（χ²(128) 的 ±6σ；均值 128、sd=√25… | blocks=128, chi_square=128.94, lo=32.0, hi=224.0 |  |
| concurrent_get | ✅ PASS | token | 双线程各 8×512B 均 ret==0，两块数据不相同 | ret0=0, ret1=0 |  |
| distinct_32 | ✅ PASS | token | 两 32B 块不相同 | - |  |
| invalid_args | ✅ PASS | token | ret==-EINVAL（两分支） | - |  |
| isr_busywait | ✅ PASS | token | ret==0，输出 32B | - |  |
| isr_nonblock | ✅ PASS | token | NULL/len==0 → -EINVAL；阻塞读 128B 后**池空 + DONE 中断已关** ⇒ 首调返回 ∈[… | first_got=0, len=64, second_got=64 |  |
| large_1024 | ✅ PASS | token | ret==0，输出 1024B | - |  |
| long_seq_1m | ✅ PASS | token | 1 MiB 分 128×8KB 全部 ret==0、无退化块、ones 占比 ∈[44%,56%]；us/bytes 上… | bytes=1048576, us=306096, ones_pct=50.019 |  |
| monobit_8k | ✅ PASS | token | ones 占比 46%~54%（8192B=65536 bit，σ≈181，±8σ≈[30146,35389]） | ones=32801, percent=50.05 |  |
| not_all_ff | ✅ PASS | token | 取熵前 memset(0xA5)；输出**≠0xA5 哨兵**且非全 0xFF | - |  |
| not_all_zero | ✅ PASS | token | 取熵前 memset(0xA5)；输出**≠0xA5 哨兵**且非全 0 | - |  |
| runs_8k | ✅ PASS | token | π 偏差 < 2/√n；游程 z =\|V−2nπ(1−π)\|/(2√(2n)·π(1−π)) < 6 | runs=32872, pi=0.50108, z=0.575 |  |
| throughput_64k | ✅ PASS | token | 64KB 总计 ret==0，无跳过块；mbps 上报 INFO | bytes=65536, us=13504, mbps=38.82 |  |

## 汇总

OVERALL: PASS
