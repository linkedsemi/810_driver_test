# 全量回归分析报告 — 2026-09-02

## 最终结果：19/23 PASS（82.6%）

| # | 工程 | Case | 结果 | 通过/总数 | 退出码 |
|---|------|------|------|----------|--------|
| 1 | trng_test | case_default | ✅ PASS | 9/9 | 0 |
| 2 | rtc_test | case_default | ❌ FAIL | 7/9 | 1 |
| 3 | otbn_test | case_default | 💥 CRASH | 0/0 | 1 |
| 4 | otbn_selftest | case_default | 💥 CRASH | 0/0 | 1 |
| 5 | i2c_pure_test | case_pure_i2c_400k | ✅ PASS | 1/1 | 0 |
| 6 | i3c_i2c_switch_test | case_switch_core | ✅ PASS | 10/10 | 0 |
| 7 | i3c_dualcore_test | case_mixed_1m_12m5 | ✅ PASS | 32/32 | 0 |
| 8 | i3c_dualcore_test | case_mixed_1m_6m25 | ✅ PASS | 32/32 | 0 |
| 9 | i3c_dualcore_test | case_mixed_400k_12m5 | ✅ PASS | 32/32 | 0 |
| 10 | i3c_dualcore_test | case_mixed_400k_1m | ✅ PASS | 32/32 | 0 |
| 11 | i3c_dualcore_test | case_mixed_400k_6m25 | ❌ FAIL | 19/32 | 1 |
| 12 | i3c_dualcore_test | case_pure_i3c_12m5 | ✅ PASS | 32/32 | 0 |
| 13 | i3c_dualcore_test | case_pure_i3c_6m25 | ✅ PASS | 32/32 | 0 |
| 14 | mbedtls_test | case_sw_baseline | ✅ PASS | 22/22 | 0 |
| 15 | mbedtls_test | case_otbn_ecc | ✅ PASS | 33/33 | 0 |
| 16 | mbedtls_test | case_hw_otbn_mix | ✅ PASS | 36/36 | 0 |
| 17 | mbedtls_test | case_hw_all | ✅ PASS | 26/26 | 0 |
| 18 | mbedtls_test | case_otbn_hash | ✅ PASS | 32/32 | 0 |
| 19 | mbedtls_test | case_otbn_rsa | ✅ PASS | 32/32 | 0 |
| 20 | mbedtls_test | case_otbn_all | ✅ PASS | 33/33 | 0 |
| 21 | mbedtls_test | case_otbn_sha256_hw_sm3 | ✅ PASS | 33/33 | 0 |
| 22 | mbedtls_test | case_sw_hw_mix | ✅ PASS | 25/25 | 0 |
| 23 | mbedtls_test | case_all_on_max | ✅ PASS | 38/38 | 0 |

---

## 框架问题清单

### 🔴 P0-1：otbn 两工程 `dump_results` 模块导入崩溃（已修复）

- **现象**：`run_test.py:551` 的 `from dump_results import TEST_NAMES` 抛 `ModuleNotFoundError`
- **影响**：otbn_test + otbn_selftest 直接退出，无报告、无 summary 行
- **根因**：`dump_results.py` 在 `tests/scripts/`，但 `sys.path` 只插入了 `common/`，缺少 `scripts/`
- **对比**：trng/rtc 用 `subprocess` 调 `dump_results.py --json` 不受影响；otbn 直接 `import`
- **修复**：在两个 `run_test.py` 顶部追加 `sys.path.insert(0, str(Path(__file__).resolve().parent / "scripts"))`
- **状态**：✅ 已修复

### 🔴 P0-2：崩溃的 case 不写 summary 行

- **现象**：otbn 两工程 crash 后 summary.md 中无对应行，汇总表不完整（21行 vs 23 case）
- **根因**：`run_all.py` 调子脚本获取退出码后只打印 PASS/FAIL，但子脚本 crash（非0退出）时若未写报告文件，summary 中缺失该行
- **影响**：汇总统计漏项，回归报告不完整
- **建议修复**：`run_all.py` 在 case 循环中，若子脚本 rc≠0 且 session summary 中无该 case 行，主动写一行 `| <case> | CRASH(rc=N) | 0/0 |`

### 🟡 P1-1：rtc_test 两项真实测试失败

- **失败项**：`set_get_roundtrip`（mem来源）、`alarm_fires`（token来源）
- **特征**：execution=None（无 PROJECT EXECUTION 行），7/9 PASS
- **性质**：非框架问题，是 RTC 驱动/firmware 层的测试失败
- **建议**：用 `qsh-debug` 分层定位，或单独重跑 rtc_test 验证是否间歇性

### 🟡 P1-2：i3c mixed_400k_6m25 firmware 执行挂死

- **现象**：前19项全 PASS，第20项（write_delay_read_16，WDR 时序测试）开始后 firmware 挂住
- **特征**：serial 静默 10s+，harness 等到 case_timeout(60s) 后标记剩余13项 NOT_RUN
- **日志证据**：`ctrl=349 tgt=143` 行数后无新增，idle 持续增长到 60s
- **性质**：I3C 驱动在 400k/6.25M 混合速率下 WDR 机制的时序问题（§10.32 已知间歇项）
- **对比**：其他 6 个 i3c case 全部 32/32 PASS
- **建议**：单独复跑验证是否间歇性；若稳定复现需 I3C 驱动侧 `scl-overhead-ns` 调参

### 🟢 P2-1：run_all.py 的 stdout 输出与 case log 不同步

- **现象**：`run_all.py` 主进程 stdout 只有 precheck + START/DONE 行，case 详细输出在各子 log 中
- **影响**：`TaskOutput` 工具只能看到主进程输出（14行），需要手动读 session 目录
- **建议**：在每个 case DONE 后追加一行结果摘要到 stdout（如 `PASS 9/9` 或 `FAIL 19/32`）

### 🟢 P2-2：cases.yaml 无 schema 校验

- **现象**：之前 `gpio_toggle_test` 条目无脚本映射但不会报错，只在运行时 SKIP
- **建议**：`run_all.py --dry-run` 应对 `??` 映射报 WARNING 或 ERROR

---

## 优化建议

### 1. otbn `dump_results` 导入方式统一化
当前 trng/rtc 用 subprocess 调 `dump_results.py --json`，otbn 直接 import。建议统一为 subprocess 方式（与 trng/rtc 一致），消除 sys.path 依赖。

### 2. run_all.py 健壮性增强
- 子脚本 crash 时自动补写 summary 行（标记 CRASH）
- 每 case DONE 后 stdout 打印结果摘要
- dry-run 检测 `??` 映射并告警

### 3. i3c WDR 时序 margin 增强
400k/6.25M 混合速率是唯一不稳定的组合。建议：
- 增加 case_timeout 从 60s 到 90s（慢速组合需要更多时间）
- 驱动侧 `scl-overhead-ns` 参数针对 6.25M 调优

### 4. 串口采集 idle 判据细化
当前 idle=10s + 无 PROJECT EXECUTION 就继续等是对的，但日志中每秒打一条诊断显得冗余。建议合并为每 5s 打一次。
