# mbedtls_test_hw_all 失败分析

> 骨架由 `common/failures_report.py` 生成；`⚠️` 已由 runner 补（会话 2026-10-10-4）。

| 项 | 值 |
|---|---|
| 工程 | mbedtls_test |
| case | case_hw_all |
| 执行标记 | FAILED |
| 内存兜底 | 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。 |
| 报告 | [mbedtls_test_report.md](../mbedtls_test_case_hw_all/mbedtls_test_report.md) |
| 结果 | FAIL (37/40) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| aes128_ctr_carry | ❌ FAIL | token | 单次/分段/回环三形态均 == 期望密文（COVERAGE.md L327：single_ok==1; segmented_ok==1; roundtrip_ok==1） | algo=aes-128-ctr, carry=word_boundary, single_ok=0, segmented_ok=1, roundtrip_ok=0 | cipher_test.c:996 zassert_true(single_ok) = false | serial_TGT.log:21 INFO；:23 断言行 |
| aes_alt_null_params | ❌ FAIL | token | 七项 NULL 入参均须返回非 0 而非崩溃（COVERAGE.md L331） | 串口打印 `PASS`（**假 PASS**）；本项执行窗口内 CPU 异常 | ZEPHYR FATAL ERROR 0（mcause 5, Load access fault, mtval 0），当前线程 = aes_alt_null_params；串口 PASS 系线程被打死后停在初值 | serial_TGT.log:57(mcause)/:80(FATAL)/:81(thread)；mepc 0x10018a56 → addr2line = `mbedtls_aes_crypt_ctr`，caller cipher_test.c:864（`crypt_ctr(..., NULL, ...)`） |
| aes_concurrent_ctx | ❌ FAIL | token | interleaved_eq_single_threaded == 1（COVERAGE.md L328） | bytes=8192, interleaved_eq_single_threaded=0 | cipher_test.c:1127 zassert_true(same) = false | serial_TGT.log:86 INFO；:88 断言行 |

## 归因

- **aes128_ctr_carry → 驱动（`aes_alt.c` `mbedtls_aes_crypt_ctr` 硬件 CTR 批量路径）**。
  同一期望密文下「单次 4 块」失败而「分段 1+3 块」通过 ⇒ 失败只出现在"计数器递增跨 32 位字边界由**硬件**完成"的那个调用；
  纯软基线（case_sw_baseline）同向量 single/seg/roundtrip 全 1 ⇒ 期望向量自洽，缺陷在硬件批量递增规则与
  mbedtls 大端 128 位整体递增不一致（混合 CTR 前提不成立）。
- **aes_concurrent_ctx → 驱动（共享 LSCRYPT 寄存器无互斥）**。线程 B 抢占 A 期间改写共享 KEY/CR 寄存器，A 剩余块算错；
  纯软基线同用例 PASS ⇒ 只出现在硬件 ALT 共享寄存器路径。
- **aes_alt_null_params → 驱动（`aes_alt.c` `mbedtls_aes_crypt_ctr` 的 `nc_off` 无 NULL 守卫）**。
  六项 setkey/ecb/cbc 的 NULL 入参守卫已在位（未 fault），仅 `crypt_ctr` 直接解引用 `*nc_off`（mtval=0 ⇒ 空指针访问）触发 Load access fault。

> 三项均落在驱动层（aes_alt.c），非用例/环境。判定：无一项可归因于测试数据或采集缺陷。

## 备注

- 对照 reports/2026-10-10-3 同 case：`aes_alt_null_params` 两轮均 FAIL（mcause 5）；
  -3 报告的故障点在 `aes_alt.c` crypt_cbc 段，本轮该段已加守卫后故障 PC 移到 `crypt_ctr`。另两项为本轮新增用例。
- `rsa4096_modexp` 上轮 180s 窗口截断 missing，本轮 timeout=240s 完成并 PASS（us=4775236）。
- 无 `0/0`、无 `error138`、无 BUILD FAILED；`PROJECT EXECUTION FAILED` 由上述 FAIL 项触发。
