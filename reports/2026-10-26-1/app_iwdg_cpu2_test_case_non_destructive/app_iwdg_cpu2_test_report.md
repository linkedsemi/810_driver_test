# app_iwdg_cpu2_test 测试报告

- 工程: app_iwdg_cpu2_test
- case: case_non_destructive
- 日期: 2026-10-26 10:19:19
- 板级: lsqsh_evb@2os（cpu1 target / cpu2 controller）
- 环境: 无外部接线（IWDG 为片内外设，见 810_driver_test/README.md 接线表）
- 执行标记: UNKNOWN
- 内存兜底: 不适用（本工程无内存兜底）
- 结果: PASS (18/18)
- 核执行标记: CTRL_EXECUTION=SUCCESSFUL, TGT_EXECUTION=SUCCESSFUL

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| cpu2::test_feed_within_window_no_reset | ✅ PASS | token | - | timeout_ms=1000, elapsed_ms=1200 |  |
| cpu2::test_install_accepts_reset_cpu_core_flag | ✅ PASS | token | - | - |  |
| cpu2::test_install_invalid_window_max_zero | ✅ PASS | token | - | - |  |
| cpu2::test_install_invalid_window_min_nonzero | ✅ PASS | token | - | - |  |
| cpu2::test_install_rejects_reset_soc_flag | ✅ PASS | token | - | - |  |
| cpu2::test_install_rejects_unmatched_flags | ✅ PASS | token | - | - |  |
| cpu2::test_install_with_callback_accepted | ✅ PASS | token | - | - |  |
| cpu2::test_install_without_callback_accepted | ✅ PASS | token | - | - |  |
| cpu2::test_load_ticks_1000ms | ✅ PASS | token | - | ms=1000, ticks=32768 |  |
| cpu2::test_load_ticks_65535ms | ✅ PASS | token | - | ms=65535, ticks=2147450 |  |
| cpu2::test_load_ticks_max_no_wrap | ✅ PASS | token | - | ms=131071, ticks=4294934 |  |
| cpu2::test_load_ticks_min_timeout | ✅ PASS | token | - | ms=1, ticks=32 |  |
| cpu2::test_setup_enables_ctrl_rst_en | ✅ PASS | token | - | ctrl=0x3 |  |
| cpu2::test_setup_pause_in_sleep_enotsup | ✅ PASS | token | - | - |  |
| cpu2::test_setup_twice_ebusy | ✅ PASS | token | - | - |  |
| cpu2::test_setup_without_timeout_enotsup | ✅ PASS | token | - | - |  |
| cpu1::test_cpu1_does_not_own_app_iwdg | ✅ PASS | token | - | - |  |
| cpu1::test_cpu1_reset_reason_not_app_iwdt | ✅ PASS | token | - | reason=3 |  |
| run::timeout | ⬜ NOTE | token | - | - | case_timeout=30s 到点截断（非失败） |

## 汇总

OVERALL: PASS
