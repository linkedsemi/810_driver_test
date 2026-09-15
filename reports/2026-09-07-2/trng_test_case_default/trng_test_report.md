# TRNG 驱动测试报告

- 工程: trng_test
- 日期: 2026-09-07 22:37:21
- 板级: lsqsh_evb@2os/lsqsh/cpu1
- 环境: tgt_uart=COM10, timeout=30s

## 测试项结果

| 测试项 | 结果 | 预期(COVERAGE) | 实际(INFO) | 失败原因 | 来源 |
|---|---|---|---|---|---|
| basic_16 | ✅ PASS | ret==0，输出 16B |  |  | src=token |
| not_all_zero | ✅ PASS | 16B 非全 0 |  |  | src=token |
| not_all_ff | ✅ PASS | 16B 非全 0xFF |  |  | src=token |
| isr_busywait | ✅ PASS | ret==0，输出 32B |  |  | src=token |
| large_1024 | ✅ PASS | ret==0，输出 1024B |  |  | src=token |
| invalid_args | ✅ PASS | ret==-EINVAL（两分支） |  |  | src=token |
| monobit_8k | ✅ PASS | ones 占比 46%~54%（8192B=65536 bit，σ≈181，±8σ≈[30146,35389]） | ones=32950, percent=50.28 |  | src=token |
| distinct_32 | ✅ PASS | 两 32B 块不相同 |  |  | src=token |
| throughput_64k | ✅ PASS | 64KB 总计 ret==0，无跳过块；mbps 上报 INFO | bytes=65536, us=15230, mbps=34.42 |  | src=token |
