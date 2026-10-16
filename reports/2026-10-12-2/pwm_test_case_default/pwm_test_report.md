# pwm_test 测试报告

- 工程: pwm_test
- case: case_default
- 日期: 2026-10-12 22:20:48
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 执行标记: SUCCESSFUL
- 内存兜底: 不适用（本工程无内存兜底）
- 结果: PASS (12/12)
- 备注: 只验 pwm_ls.c 接口可用性；**波形正确性未验证**（需示波器/逻辑分析仪）

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| channel_isolation | ✅ PASS | token | - | - |  |
| concurrent_same_channel | ✅ PASS | token | - | - |  |
| cycle_boundary | ✅ PASS | token | - | - |  |
| cycles_per_sec_contract | ✅ PASS | token | - | - |  |
| dt_spec_path | ✅ PASS | token | - | - |  |
| init_path_readback | ✅ PASS | token | - | - |  |
| invalid_channel | ✅ PASS | token | - | - |  |
| invalid_period_pulse | ✅ PASS | token | - | - |  |
| no_disable_api | ✅ PASS | token | - | - |  |
| ns_api_path | ✅ PASS | token | - | - |  |
| polarity_flags | ✅ PASS | token | - | - |  |
| set_cycles_readback | ✅ PASS | token | - | - |  |

## 汇总

OVERALL: PASS
