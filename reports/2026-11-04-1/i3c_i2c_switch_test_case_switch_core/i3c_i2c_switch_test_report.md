# i3c_i2c_switch_test 测试报告

- 工程: i3c_i2c_switch_test
- case: case_switch_core
- 日期: 2026-11-04 22:44:14
- 板级: lsqsh_evb@2os
- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）
- 执行标记: CTRL=SUCCESSFUL, TGT=SUCCESSFUL
- 内存兜底: 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。
- 结果: PASS (11/11)

## case_switch_core (PASS)

> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| pinctrl_states | ✅ PASS | token | state_count==2 且 states[1].id==PINCTRL_STATE_PRIV_START | - |  |
| i3c_baseline | ✅ PASS | token | 写 0x00..0x0F 16B；读 0xAA..0xB9 16B；case_no_bus_init 另需 nbi_ne… | - |  |
| switch_i3c_to_i2c | ✅ PASS | token | 切换后 i2c5↔i2c6 回连 16B 写读（target 侧 100ms 延时先行） | - |  |
| i2c_rw_1 | ✅ PASS | token | 写 1B 读 1B（0xAA） | - |  |
| i2c_rw_16 | ✅ PASS | token | 写 0x00..0x0F，读 0xAA..0xB9 | - |  |
| i2c_rw_17 | ✅ PASS | token | 写 0x00..0x10，读 0xAA..0xBA | - |  |
| i2c_rw_100 | ✅ PASS | token | 写 0x00..0x63，读 0xAA..0x109 模 256 | - |  |
| switch_i2c_to_i3c | ✅ PASS | token | 切回后 DAA 重新发现 target，16B 写读一致 | - |  |
| i2c_nack_bad_addr | ✅ PASS | token | 访问 0x70 返回非 0（预期 NACK） | - |  |
| switch_stress | ✅ PASS | token | 5 轮全部通过（**双核均同 id 粘滞累计**：CTRL `ctrl_failed` 与 TGT `tgt_faile… | - |  |
| broadcast_nack_recovery | ✅ PASS | token | 'N' 窗口内首帧 RSTACT whole **不报错**（原「应 NACK」判据不可达，见下方「已知基线」）；tar… | CTRL:win_broadcast_ret=0 |  |

## 汇总

OVERALL: PASS
