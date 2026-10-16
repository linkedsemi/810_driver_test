# mbedtls_test_hw_all 失败分析

> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**（实测值去 `serial_*.log` 挖，归因段自行判断）。

| 项 | 值 |
|---|---|
| 工程 | mbedtls_test |
| case | case_hw_all |
| 执行标记 | FAILED |
| 内存兜底 | 不可用（内存兜底未执行） — 内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。 |
| 报告 | [mbedtls_test_report.md](../mbedtls_test_case_hw_all/mbedtls_test_report.md) |
| 结果 | FAIL (41/42) |

## 失败项明细

| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |
|---|---|---|---|---|---|---|
| aes128_ctr_carry | ❌ FAIL | token | single_ok=1 且 segmented_ok=1 且 roundtrip_ok=1（跨 32 位字进位的 CTR NIST 向量） | algo=aes-128-ctr, carry=word_boundary, single_ok=0, segmented_ok=1, roundtrip_ok=0 | WEST_TOPDIR/810_driver_test/projects/mbedtls_test/cpu1_mbedtls_test/src/cipher_test.c: 1024: cipher_aes128_ctr_carry: (single_ok is false) | serial_TGT.log:21(INFO), :25(FAIL) |

## 归因

**A 驱动（`aes_alt.c` 的 `mbedtls_aes_crypt_ctr`，本次未修好项）。**

- **现象**：`single_ok=0, segmented_ok=1, roundtrip_ok=0`。测试向量 cnt0 低 32 位 = `0xFFFFFFFF`（`cipher_test.c` 就地断言 "carry vector must start with low 32 bits all ones"）⇒ **形态 1（一次喂 4 块）在跨 32 位字进位处产出错误密文**；形态 2（1 块 + 3 块，进位恰落在调用边界）正确；形态 3（回环解密）随之错误。
- **定位**：`mbedtls_aes_crypt_ctr` 的**整块路径**（step 2「Whole 16-byte blocks via the hardware CTR engine」，`aes_config(...,0x2)`）**依赖硬件 CTR 引擎的内部计数器自增**，仅在事后用 `mbedtls_ctr_increment_counter` 回写 `nonce_counter`。硬件计数器的进位语义与 mbedtls 契约（128 位大端整链进位）**不一致 —— 表现为不跨 32 位字进位**。
- **判据（排除镜像/计数器字序）**：① 形态 1 是**单次调用内部**，输出完全由硬件产生、镜像发生在其后 ⇒ 失败点必在硬件计数器；② 若计数器字序错，则 `aes128_ctr`（计数 0,1,2,3）也会错，而它 PASS ⇒ 字序正确；③ 全部**不跨字进位**的 CTR 用例（`aes128_ctr` / `_seg` / `_len` / `_inplace` / `_unaligned`）全 PASS，**唯一**带跨字进位的 `aes128_ctr_carry` FAIL ⇒ 差异因子就是"跨 32 位字进位"。（此为强推断，尚未用示波器/寄存器级实验直接证硬件计数行为。）
- **与改动前对比**：10-12-2 基线本项即 `single_ok=0/segmented_ok=1/roundtrip_ok=0` **逐字相同** ⇒ 本次改动**未触及**该路径（其余 3 项已修，见备注）。
- **建议**：整块路径不复用硬件 CTR 引擎的自身计数，改为**逐块用 `aes_crypt_ecb_raw(nonce_counter, keystream)` 生成密钥流 + 每块软件 `mbedtls_ctr_increment_counter`**（即 O-41 记载的 b74d2396a 形态；该原语已在 tail 分支存在且正确）。

## 备注

- **与 10-12-2 差异**：本 case AES FAIL 4 项 → **1 项**（`aes_alt_null_params` / `aes_cbc_len_contract` / `aes_concurrent_ctx` 三项 FAIL→PASS）；`aes128_ctr_carry` 10-12 起即 FAIL、signature 不变。**无新增 FAIL**，总项数 42 对齐。
- 三项转 PASS 均经串口复核**无 `mcause` / `ZEPHYR FATAL ERROR`**（非 O-43 异常后假 PASS）——`serial_TGT.log` 全文件 0 条致命异常行；`aes128_ctr_carry` 亦非异常（有真实 zassert 断言行 :24）。
- 源版本：`aes_alt.c` md5 `128396c98b894df14334d13ccec10d40`（2026-10-13 11:25:52），本次 3 case 的 `.obj` 均晚于该源（`-p always` 全量重建）。
- 关联台账：`OPEN_ITEMS` **O-41**（CTR 流式契约）、**O-42**（CBC/并发/NULL 加固）。
