# 810_driver_test 汇总（2026-10-16-6）

> 本会话 = 一轮 OTBN ECDSA «代理 vs 直接 ALT» 隔离吞吐测量（3 个 case，同一实验，已并入本目录）。
> 方法学与结论见 `otbn_delegation_test/tests/COVERAGE.md` §1 方法学边界。

| 测试 | 结果 | 通过/总数 |
|---|---|---|
| [otbn_delegation_test_case_throughput_iso_proxy](otbn_delegation_test_case_throughput_iso_proxy/otbn_delegation_test_report.md) | PASS | 1/1 |
| [otbn_delegation_test_case_throughput_iso_local](otbn_delegation_test_case_throughput_iso_local/otbn_delegation_test_report.md) | PASS | 1/1 |
| [otbn_delegation_test_case_throughput_iso_local_hwrng](otbn_delegation_test_case_throughput_iso_local_hwrng/otbn_delegation_test_report.md) | PASS | 1/1 |

**共 3 个测试，通过 3 个，失败 0 个**

> 原始证据 = 各 case 目录下 `serial_CTRL.log` / `serial_TGT.log`。
> 注意：两遍烧录法的已知归档竞争（KNOWN_ISSUES #117）会让每核日志**头部**混入 prime 轮残留；
> **取 `Booting Zephyr` 之后的 9 行 `INFO bench:` 才是本轮 capture 数据**。
