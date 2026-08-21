# TRNG 驱动测试报告

- 工程: trng_test
- 日期: 2026-08-21 20:16:46
- 板级: lsqsh_evb@2os/lsqsh/cpu1
- 环境: tgt_uart=auto, timeout=30s

## 测试项结果

| 测试项 | 结果 | 预期(COVERAGE) | 实际(INFO) | 失败原因 | 来源 |
|---|---|---|---|---|---|
- Run @ 20:16:46 (case case_default)
| basic_16 | ✅ PASS | ret==0，输出 16B |  |  | src=token |
| not_all_zero | ✅ PASS | 16B 非全 0 |  |  | src=token |
| not_all_ff | ✅ PASS | 16B 非全 0xFF |  |  | src=token |
| isr_busywait | ✅ PASS | ret==0，输出 32B |  |  | src=token |
| large_1024 | ✅ PASS | ret==0，输出 1024B |  |  | src=token |
| invalid_args | ✅ PASS | ret==-EINVAL（两分支） |  |  | src=token |
| monobit_8k | ✅ PASS | ones 占比 46%~54%（8192B=65536 bit，σ≈181，±8σ≈[30146,35389]） | ones=32727,percent=49.94 |  | src=token |
| distinct_32 | ✅ PASS | 两 32B 块不相同 |  |  | src=token |
| throughput_64k | ✅ PASS | 64KB 总计 ret==0，无跳过块；mbps 上报 INFO | bytes=65536,us=11077,mbps=47.33 |  | src=token |
- Run @ 20:31:05 (case case_default)
| basic_16 | ⚠️ MISSING | ret==0，输出 16B |  |  | mem-unavailable |
| not_all_zero | ⚠️ MISSING | 16B 非全 0 |  |  | mem-unavailable |
| not_all_ff | ⚠️ MISSING | 16B 非全 0xFF |  |  | mem-unavailable |
| isr_busywait | ⚠️ MISSING | ret==0，输出 32B |  |  | mem-unavailable |
| large_1024 | ⚠️ MISSING | ret==0，输出 1024B |  |  | mem-unavailable |
| invalid_args | ⚠️ MISSING | ret==-EINVAL（两分支） |  |  | mem-unavailable |
| monobit_8k | ⚠️ MISSING | ones 占比 46%~54%（8192B=65536 bit，σ≈181，±8σ≈[30146,35389]） |  |  | mem-unavailable |
| distinct_32 | ⚠️ MISSING | 两 32B 块不相同 |  |  | mem-unavailable |
| throughput_64k | ⚠️ MISSING | 64KB 总计 ret==0，无跳过块；mbps 上报 INFO |  |  | mem-unavailable |
- Run @ 20:34:15 (case case_default)
| basic_16 | ✅ PASS | ret==0，输出 16B |  |  | src=token |
| not_all_zero | ✅ PASS | 16B 非全 0 |  |  | src=token |
| not_all_ff | ✅ PASS | 16B 非全 0xFF |  |  | src=token |
| isr_busywait | ✅ PASS | ret==0，输出 32B |  |  | src=token |
| large_1024 | ✅ PASS | ret==0，输出 1024B |  |  | src=token |
| invalid_args | ✅ PASS | ret==-EINVAL（两分支） |  |  | src=token |
| monobit_8k | ✅ PASS | ones 占比 46%~54%（8192B=65536 bit，σ≈181，±8σ≈[30146,35389]） | ones=32662,percent=49.84 |  | src=token |
| distinct_32 | ✅ PASS | 两 32B 块不相同 |  |  | src=token |
| throughput_64k | ✅ PASS | 64KB 总计 ret==0，无跳过块；mbps 上报 INFO | bytes=65536,us=11079,mbps=47.32 |  | src=token |
