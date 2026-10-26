# wdt_test 测试报告

- 工程: wdt_test
- case: case_boundary
- 日期: 2026-10-24 00:15:04
- 板级: lsqsh_evb@1os/lsqsh/cpu1
- 执行标记: SUCCESSFUL
- 内存兜底: 不适用（本工程无内存兜底）
- 结果: PASS (19/19)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| test_install_accepted_on_supported_flags | ✅ PASS | token | - | - |  |
| test_install_rejects_unmatched_flags | ✅ PASS | token | - | - |  |
| test_install_with_callback_accepted | ✅ PASS | token | - | - |  |
| test_install_without_callback_accepted | ✅ PASS | token | - | - |  |
| test_load_ticks_1000ms | ✅ PASS | token | - | - |  |
| test_load_ticks_just_below_saturation | ✅ PASS | token | - | - |  |
| test_load_ticks_min_timeout | ✅ PASS | token | - | - |  |
| test_load_ticks_saturation_boundary | ✅ PASS | token | - | - |  |
| test_load_ticks_uint32_max_timeout | ✅ PASS | token | - | - |  |
| test_pmu_iwdg_lifecycle | ✅ PASS | token | - | - |  |
| test_pmu_iwdg_rejects_reset_cpu_core_flag | ✅ PASS | token | - | - |  |
| test_sec_iwdg_rejects_reset_soc_flag | ✅ PASS | token | - | - |  |
| test_setup_pause_halted_by_dbg_feed_disable | ✅ PASS | token | - | - |  |
| test_setup_pause_in_sleep_enotsup | ✅ PASS | token | - | - |  |
| test_setup_twice_ebusy | ✅ PASS | token | - | - |  |
| test_setup_without_timeout_enotsup | ✅ PASS | token | - | - |  |
| test_window_max_zero_rejected | ✅ PASS | token | - | - |  |
| test_window_min_equals_max_rejected | ✅ PASS | token | - | - |  |
| test_window_min_nonzero_rejected | ✅ PASS | token | - | - |  |

## 汇总

OVERALL: PASS
