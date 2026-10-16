# i2c_pure_test 测试报告

- 工程: i2c_pure_test
- case: case_pure_i2c_alt_100k_400k
- 日期: 2026-10-12 17:25:30
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）
- 执行标记: CTRL=SUCCESSFUL, TGT=SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (1/1)

## case_pure_i2c_alt_100k_400k (PASS)

> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| i2c_wave | ✅ PASS | token | 100 轮 ×（写 16B 0x00..0x0F + 读 16B 0xAA..0xB9）全部匹配；轮间 100ms 静默… | CTRL:avg_us_100k=3385, avg_us_400k=963, delay_ms=100, n_100k=50, n_400k=50, rate_khz=100alt400, ratio_100k_over_400k=3.51, rounds=100; TGT:bytes=1600, reads=1750, writes=100 |  |

## 汇总

OVERALL: PASS
