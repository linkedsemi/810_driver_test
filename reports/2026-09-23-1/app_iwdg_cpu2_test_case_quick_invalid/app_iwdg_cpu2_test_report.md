# app_iwdg_cpu2_test 测试报告

- 工程: app_iwdg_cpu2_test
- case: case_quick_invalid
- 日期: 2026-09-22 23:33:00
- 板级: lsqsh_evb@2os（cpu1 target / cpu2 controller）
- 环境: 无外部接线（IWDG 为片内外设，见 810_driver_test/README.md 接线表）
- 执行标记: UNKNOWN
- 内存兜底: 不适用（本工程无内存兜底）
- 结果: PASS (3/4)
- 核执行标记: CTRL_EXECUTION=SUCCESSFUL, TGT_EXECUTION=SUCCESSFUL

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| cpu2::test_quick_timeout_zero_leaves_device_unready | ✅ PASS | token | - | ready=0, initialized=1, init_res=22 |  |
| cpu1::test_cpu1_does_not_own_app_iwdg | ✅ PASS | token | - | - |  |
| cpu1::test_cpu1_reset_reason_not_app_iwdt | ✅ PASS | token | - | reason=3 |  |
| run::timeout | ⬜ NOTE | token | - | - | case_timeout=120s 到点截断（非失败） |

## 汇总

OVERALL: PASS
