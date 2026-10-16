# 810_driver_test 汇总（2026-10-13-1）

| 测试 | 结果 | 通过/总数 |
|---|---|---|
| [mbedtls_test_case_all_on_max](mbedtls_test_case_all_on_max/mbedtls_test_report.md) | FAIL ❌ | 53/54 |
| [mbedtls_test_case_hw_all](mbedtls_test_case_hw_all/mbedtls_test_report.md) | FAIL ❌ | 41/42 |
| [mbedtls_test_case_sw_hw_mix](mbedtls_test_case_sw_hw_mix/mbedtls_test_report.md) | FAIL ❌ | 39/40 |
| [otbn_delegation_test_case_alt_via_proxy](otbn_delegation_test_case_alt_via_proxy/otbn_delegation_test_report.md) | PASS | 24/25 |
| [otbn_delegation_test_case_dead_channel](otbn_delegation_test_case_dead_channel/otbn_delegation_test_report.md) | PASS | 2/24 |
| [otbn_delegation_test_case_default](otbn_delegation_test_case_default/otbn_delegation_test_report.md) | PASS | 24/24 |
| [otbn_delegation_test_case_explicit_api](otbn_delegation_test_case_explicit_api/otbn_delegation_test_report.md) | PASS | 24/25 |
| [otbn_delegation_test_case_no_server](otbn_delegation_test_case_no_server/otbn_delegation_test_report.md) | PASS | 2/24 |
| [otbn_delegation_test_case_rsa_delegation](otbn_delegation_test_case_rsa_delegation/otbn_delegation_test_report.md) | PASS | 3/4 |
| [otbn_delegation_test_case_seq_soak](otbn_delegation_test_case_seq_soak/otbn_delegation_test_report.md) | PASS | 1/1 |
| [otbn_delegation_test_case_throughput](otbn_delegation_test_case_throughput/otbn_delegation_test_report.md) | PASS | 1/1 |

**共 11 个测试，通过 8 个，失败 3 个**

> **本轮（服务端独立线程修复后复测，2026-10-13 第三轮）**：重跑 `otbn_delegation_test` 全部 8 个 case
> （两核同批 `-p always` 全量重建后同烧）。**8/8 case PASS、双核均 `PROJECT EXECUTION SUCCESSFUL`**；
> 与重构前基线 `tmp/baseline_2026-10-13-1/` 逐项对比 **7/7 共有 case 全部 0-DIFF**（`case_rsa_delegation`
> 为基线后新增 case，不在基线内）。上一轮的「服务端被代理 ECDSA sign 内核 panic」**已消失** ——
> 服务端 worker 从 1 KB 的 `sysworkq` 迁到独立 `otbn_deleg_thread`（8192 B 栈）后修复。
> 详见 [`otbn_delegation_test_mtu-rootcause-2026-10-13.md`](otbn_delegation_test_mtu-rootcause-2026-10-13.md)。
>
> mbedtls 三行未在本轮重跑，为同日早前结果，原样保留（其 `failures/` 三份归因文件亦然）。
