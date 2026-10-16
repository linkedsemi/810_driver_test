# mbedtls_test_hw_all 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | mbedtls_test |
| case | case_hw_all |
| 执行标记 | UNKNOWN |
| 内存兜底 | 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。 |
| 报告 | [mbedtls_test_report.md](../mbedtls_test_case_hw_all/mbedtls_test_report.md) |
| 结果 | FAIL (36/38) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| aes_alt_null_params | ❌ FAIL | token | 7 个 NULL 入参全部返回非 0（-33）；INFO 7 项全 -33（09-29-5 基线原文：`setkey_null_ctx=-33,setkey_null_key=-33,cbc_null_in=-33,cbc_null_out=-33,ecb_null_in=-33,ecb_null_out=-33,ctr_null_ncoff=-33`） | **无 INFO 行**（测试项在打印 ZT_INFO 前即 fault）；串口 `PASS` 为初值假 PASS | CPU 异常（执行期间致命异常，非断言失败）：本项执行窗口内固件打印了 `ZEPHYR FATAL ERROR 0: CPU exception on CPU 0`（归档第 73 行；mcause: 5, Load access fault）=> 测试线程被内核异常/panic 打断，ztest 的 `ztest_test_fail()` 未被调用，逐项结果停在**初值 PASS** —— 那是假 PASS。按本模块/判定框架『任何不确定都不允许产生 PASS』判 FAIL。★ 串口该项仍会打印 `PASS`，报告以本理由为准（勿读成断言失败）。 | serial_TGT.log:48-77（START@48；mcause@50、mtval@51、mepc@62、FATAL@73、Current thread@74、PASS@76） |
| rsa4096_modexp | ❌ FAIL | missing | decrypt 黄金明文；sign/verify 回环 | `algo=rsa4096, stage=dec, us=4533846`（只出第 1 个 stage；signverify 段未打印）—— 无断言、无 reason，测试项被采集窗口截断 | **窗口截断**（非断言失败）：全项无 `Assertion failed`、报告 reason 为空、执行标记 `UNKNOWN`（无 `PROJECT EXECUTION`）⇒ 180s 采集窗到点时该项仍在跑，被截成 FAIL。截断前的 151s 墙钟（`[3s]`→`[154s]`）落在前一项 `rsa_keygen_non_f4_fallback`（软件 RSA 非 F4 素数搜索，重尾）上 | serial_TGT.log:196-197（末尾即该项 INFO，文件止于此）；`tmp/mbedtls_3case_2026-10-10.log` 中 elapsed 由 [3s] 跳到 [154s]、rsa4096 START@[178s] |

## 归因

**① aes_alt_null_params —— 归因：驱动（aes_alt.c「NULL 入参加固」退化）。**

证据链：
- 故障 PC `mepc=0x10018458`，`addr2line` 解析 = `mbedtls_aes_crypt_ctr` @ `modules/crypto/mbedtls/library/aes_alt.c`（line-table 落 :461，函数体本身无该行号 → 优化后行号偏差；函数内 `*nc_off` 的解引用在 :379 `size_t offset = *nc_off;` / :455 `*nc_off = offset;`）。
- `mtval=0` ⇒ 访问地址 = 0（**NULL 解引用**），`mcause=5` = Load access fault。
- 调用者 = `cipher_test.c:864` = `r_ctr = mbedtls_aes_crypt_ctr(&ctx, 16, NULL /*nc_off*/, nc, sb, in, out);` —— 即 7 个 NULL 入参用例中的 **NULL nc_off** 一项。
- 函数体自 `:378` 起即 `size_t offset = *nc_off;`，**全函数无 `nc_off == NULL` 检查**（同文件其它入口有：`:36` ctx、`:107` ctx/key、`:139`/`:182` input/output）。
- 对照 09-29-5（旧 aes_alt.c）：同一项 INFO 7 项全 `-33` ⇒ **旧版对 NULL nc_off 有守卫、新版丢了** ⇒ 本项为**真回归**。

**② rsa4096_modexp —— 归因：环境/用例（采集窗超时），非驱动、非断言失败。**

- 无 `Assertion failed`、reason 空、执行标记 `UNKNOWN` ⇒ 是「窗口到点、项未跑完」的截断，不是"测出来是错的"。
- 该项本体逻辑无证据显示异常；同工程同日的 `sw_baseline`/`otbn_all` 中同名 `rsa4096_modexp` 均 PASS（分别为软件/OTBN 路径）。
- 窗口被前一项 `rsa_keygen_non_f4_fallback`（软件 RSA 非 F4 素数搜索）的**墙钟重尾**吃掉：本次区间 `[3s]→[154s] ≈ 151s`；同会话 `sw_baseline` 同名项为 `[3s]→[41s] ≈ 38s`；09-29-5 该套件整体 <180s 跑完。⇒ 属**重尾耗时抽样**（与 `debug-knowledge` `heavy-tail-latency-benchmark` 同族），非 AES/驱动改动所致。
- 残留不确定：无法完全排除 `aes_alt_null_params` 的 fault 对后续调度有扰动（OTBN 路径无此 fault 且无 stall）——但 `sw_baseline`（无 fault）同名项仍耗 38s，说明该项本就慢且抖动大。要坐实需重跑一轮（本轮未做）。

## 备注

- **与上次回归（09-29-5，同 timeout=180s）的差异**：该轮 hw_all 为 38/38 PASS、`aes_alt_null_params` INFO 7 项全 -33、`ZEPHYR FATAL=0`。本轮新增 **1 次 ZEPHYR FATAL ERROR**（位置 = aes_alt_null_params）并把该项 R9 改判 FAIL ⇒ **AES ALT 的 NULL 加固回归**。
- AES 正向路径（ctr_seg / ctr_len / ctr_unaligned / ecb_cbc_unaligned / ctr_inplace / ecb_order / xts / xts_multi_ctx）本轮**全 PASS**，与 09-29-5 逐项一致（无回归）。
- 相关：本条为 `aes_alt.c`（工作区未提交版本，md5 `be211a8b…`）的 NULL 守卫缺口；`aes_alt.c` 无 `K_MUTEX/K_SEM`（本轮 3 case 未覆盖 AES 并发，无从验证）。

