# i2c_pure_test 测试报告

- 工程: i2c_pure_test
- case: case_pure_i2c_400k
- 日期: 2026-10-16 14:10:34
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）
- 执行标记: CTRL=FAILED, TGT=None
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: FAIL (0/1；失败 1)

## case_pure_i2c_400k (FAIL)

> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定
> 证据缺失（非驱动失败）：TGT 核全部 1 项无确定结果（串口未捕到 + 内存兜底无结果） ⇒ 不得因另一核完好判 PASS

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| i2c_wave | ❌ FAIL | missing | 100 轮 ×（写 16B 0x00..0x0F + 读 16B 0xAA..0xB9）全部匹配；轮间 100ms 静默… | - | CTRL:WEST_TOPDIR/810_driver_test/projects/i2c_pure_test/cpu0_i2c_master/src/main.c: 114: i2c_pure_ctrl_i2c_wave: (ret not equal to 0) |

## 汇总

OVERALL: FAIL
