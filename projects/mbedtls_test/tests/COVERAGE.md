# mbedtls 测试覆盖矩阵

工程定位：**mbedtls API 级测试**，被测对象 = 共享树 mbedtls（`modules/crypto/mbedtls/`）
经 `zephyr/soc/linkedsemi/lsqsh/otbn/Kconfig.mbedtls` 16 个配置项路由后的
软件 / 硬件引擎（AES/SM4/SHA）/ OTBN（SHA/ECDSA/RSA/PointOps）实现。驱动路径列写
mbedtls API + Kconfig 依赖；判定 = 标准向量逐字节比对 + 解密回环（§13.2），
同一算法在 sw 基线 / 硬件 / OTBN 三个路径下向量级对齐。

**环境：无外部接线（片内软件库 + 片内加密引擎/TRNG，CLAUDE.md §14 片内标注）。**

## case 与 Kconfig 路由

| case | Kconfig 路由 | 测试项集合 |
|---|---|---|
| case_sw_baseline | 全部 LINKEDSEMI ALT 关（纯软） | hash×4 + cipher×1 + ecc×3 + rsa×2 + robustness×N = 10+N |
| case_otbn_ecc | OTBN ECDSA（P256/P384/SM2）+ SM2 曲线 | hash×4 + cipher×1 + ecc×4 + rsa×2 + pointops×5 + robustness×N = 16+N |
| case_hw_otbn_mix | SHA224/256 硬件 + SM4 硬件 + OTBN ECC | hash×4 + cipher×3 + ecc×4 + rsa×2 + pointops×5 + robustness×N = 18+N |
| case_hw_all | AES/SHA224-256/SM3/SHA384-512/SM4 硬件 ALT 全开 | hash×5 + cipher×4 + ecc×3 + rsa×2 + robustness×N = 14+N |
| case_otbn_hash | SHA224-256/SM3/SHA384-512 OTBN ALT | hash×5 + cipher×1 + ecc×3 + rsa×2 + robustness×N = 11+N |
| case_otbn_rsa | RSA OTBN ALT（2048/3072/4096） | hash×4 + cipher×1 + ecc×3 + rsa×4 + robustness×N = 12+N |
| case_otbn_all | OTBN hash + ECC + RSA 全开 | hash×5 + cipher×1 + ecc×4 + rsa×4 + pointops×5 + thread×1 + robustness×N = 20+N |
| case_sw_hw_mix | 软件 ECC/SM2 + 硬件 AES/SHA/SM4 | hash×4 + cipher×4 + ecc×4 + rsa×2 + robustness×N = 14+N |
| case_all_on_max | 16 项全开（除 DELEGATION） | hash×5 + cipher×4 + ecc×4 + rsa×4 + pointops×5 + thread×1 + robustness×N = 23+N |
| case_thread_mix | otbn_all + THREADING_ALT + 双线程并发 | hash×5 + cipher×4 + ecc×4 + rsa×4 + pointops×5 + thread×1 + robustness×N = 23+N |

> 测试项数会因 `MBEDTLS_ECP_DP_SM2_ENABLED`、`CONFIG_MBEDTLS_LINKEDSEMI_OTBN`、
> `CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT` 等条件编译变化；上表为各 case 典型项数。
> `case_sw_baseline` 无 OTBN，pointops/thread 相关项不编译。

## 覆盖矩阵（§13.3）

### 哈希

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| sha224_kat | mbedtls_sha256_starts/update/finish（is224=1；软件/OTBN/HW 路由） | 功能通路+边界长度 | 4 短向量 + 百万'a' | FIPS 180-4 + hashlib（expected/hash_vectors.h） | ✅ |
| sha256_kat | mbedtls_sha256_*（is224=0；HW/OTBN/软件路由） | 功能通路+边界长度 | 4 短向量 + 百万'a'=cdc76e5c... | FIPS 180-4 | ✅ |
| sha384_kat | mbedtls_sha512_*（is384=1；软件/HW/OTBN 路由） | 功能通路+边界长度 | 4 短向量 + 百万'a' | FIPS 180-4 + hashlib | ✅ |
| sha512_kat | mbedtls_sha512_*（is384=0） | 功能通路+边界长度 | 4 短向量 + 百万'a' | FIPS 180-4 + hashlib | ✅ |
| sm3_kat | mbedtls_sm3_starts/update/finish（OTBN/HW ALT 路由） | 功能通路+边界长度 | 4 短向量 + 百万'a' | GM/T 0004-2012 附录 A + hashlib(sm3) | ✅ |
| sha256_bad_params | mbedtls_sha256_starts/update/finish 异常参数 | 异常参数 | **is224=2 返回非 0**（文档化）；len=0 合法；reset 后再 update 正常 | API 契约 | ✅ |
| sha384_bad_params | mbedtls_sha512_starts/update 异常参数 | 异常参数 | **is384=2 返回非 0**（文档化）；合法 starts+update 正常 | API 契约 | ✅ |
| **sha256_alt_null_params**（仅 SHA256 ALT case 编入） | sha256_starts(NULL) / update(NULL, ilen>0) | **ALT 加固（超出 mbedtls 契约）** | 两项均须**返回非 0**而非崩溃 | 本驱动加固承诺（contract） | ⏳ 新增 |
| **sha512_alt_null_params**（仅 SHA512 ALT case 编入） | sha512_starts(NULL) / update(NULL, ilen>0) | **ALT 加固（超出 mbedtls 契约）** | 同上 | 同上 | ⏳ 新增 |

### 对称加密

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| aes128_cbc | mbedtls_aes_setkey_enc/dec + mbedtls_aes_crypt_cbc（ENC/DEC；HW 经 `MBEDTLS_CIPHER_AES_LINKEDSEMI`） | 功能通路+异常参数 | 4 块加密向量 + 解密回环 | NIST SP800-38A F.2.1 | ✅ |
| aes128_ctr | mbedtls_aes_setkey_enc + mbedtls_aes_crypt_ctr | 功能通路 | 4 块 CTR 向量 + 解密回环 | NIST SP800-38A F.5.1 | ✅ |
| **aes_ecb_order** | mbedtls_aes_crypt_ecb vs crypt_cbc(单块,IV=0) vs crypt_ctr(单块,counter=明文) 三路互比 | **契约（数据字节序）** | 三路输出均须逐字节等于 FIPS-197 C.1 的 `69C4E0D8…C55A` | FIPS-197 附录 C.1（golden） | ⏳ 新增 |
| **aes128_ctr_seg** | mbedtls_aes_crypt_ctr 分段调用状态（`nc_off`/`nonce_counter`/`stream_block`） | **契约（调用状态）** | 7+16+41 分段与单次 64B 逐字节等价；调用后 `nc_off`=7/7/0、`nonce_counter`=CNT1/CNT2、`stream_block`=对应 keystream 块 | NIST F.5.1 + mbedtls CTR 契约（golden+contract） | ⏳ 新增 |
| **aes128_ctr_len** | mbedtls_aes_crypt_ctr 非整块长度 1/15/17/31 | **边界 + 越界** | 密文等于 NIST CT 前 N 字节；缓冲区两侧哨兵 0xA5 不得被改写 | NIST F.5.1（golden） | ⏳ 新增 |
| **aes128_ctr_inplace** | mbedtls_aes_crypt_ctr（input == output） | 别名 | 原地结果与非原地/标准向量一致 | NIST F.5.1（golden） | ⏳ 新增 |
| **aes128_ctr_unaligned** | mbedtls_aes_crypt_ctr（输入输出各偏移 1 字节） | 对齐 | 不触发未对齐异常，结果等于 NIST CT | NIST F.5.1（golden） | ⏳ 新增 |
| **aes128_xts** | mbedtls_aes_xts_setkey_enc/dec + crypt_xts（含 55B ciphertext stealing） | 功能通路+边界 | IEEE P1619/D16 向量 1-3 加密 KAT + 用 KAT 密文解回明文 + 55B 回环（且密文≠明文） | mbedtls `test_suite_aes.xts.data`（golden，出处 pdf00086.pdf） | ⏳ 新增 |
| **aes128_xts_multi_ctx** | 两个 aes_xts context 绑不同密钥后**交错使用** | **契约（多 context 隔离）** | 各自仍等于自己的 KAT | 同上（golden） | ⏳ 新增 |
| **aes_alt_null_params**（仅 ALT case 编入） | setkey/crypt_cbc/crypt_ecb/crypt_ctr 传 NULL ctx/key/in/out/nc_off | **ALT 加固（超出 mbedtls 契约）** | 七项均须**返回非 0**而非崩溃 | 本驱动加固承诺（contract） | ⏳ 新增 |
| aes128_gcm | mbedtls_gcm_setkey + gcm_crypt_and_tag | 功能通路+AEAD tag | 2 块 ciphertext + 16B tag + 解密回环 | NIST GCMVS（OpenSSL 复核） | ✅ |
| sm4_ecb | mbedtls_sm4_setkey + sm4_ecb_encrypt/decrypt（HW） | 功能通路 | 32B 加密向量 + 解密回环 | draft-ribose-cfrg-sm4-10 A.2.1.1 | ✅ |
| sm4_cbc | 测试层 CBC 组合（sm4_alt 无 CBC API） | 组合语义 | 32B CBC 向量 + 解密回环 | GB/T 32907（OpenSSL 复核） | ✅ |
| sm4_ctr | mbedtls_sm4_setkey + setiv + sm4_ctr_crypto（HW） | 功能通路 | 32B CTR 向量 + 解密回环 | draft-ribose-cfrg-sm4-10 A.2.5.1（OpenSSL 复核） | ✅ |
| aes_bad_params | mbedtls_aes_setkey_enc/crypt_cbc 异常参数 | 异常参数 | NULL ctx/input、错误 keybits 返回非 0 | API 契约 | ✅ |
| sm4_bad_params | mbedtls_sm4_setiv/ctr_crypto（经 HAL）+ ecb_encrypt/ecb_decrypt（内联寄存器）异常参数 | 异常参数 | NULL ctx/in/out、len=0、非 16 整块返回非 0；合法入参仍返回 0 | API 契约 | ✅ |

### ECC / ECDSA

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| p256_kat | mbedtls_ecdsa_sign（定 k）+ verify | 功能通路 | 黄金 r/s + verify=0 | samples/mbedtls_test ECDSA 验证组 | ✅ |
| p256_keygen | mbedtls_ecp_gen_keypair → sign → verify | 功能通路 | verify=0 | 回环自洽 | ✅ |
| p384_keygen | 同上，curve=SECP384R1 | 功能通路 | verify=0 | 回环自洽 | ✅ |
| sm2_keygen | 同上，curve=SM2（仅 SM2-enabled case） | 功能通路 | verify=0 | 回环自洽 | ✅ |
| ecdsa_tamper_verify | sign 后改消息再 verify 应失败 | 负例 | ret != 0 | API 契约 | ✅ |

### RSA

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| rsa2048_modexp | mbedtls_rsa_import_raw/complete → pkcs1_encrypt/decrypt/sign/verify | 功能通路+异常参数 | decrypt(黄金密文)==黄金明文；sign==黄金签名；verify=0 | OpenSSL 生成 RSA-2048 KAT（expected/rsa_vectors.h） | ✅ |
| rsa2048_keygen | mbedtls_rsa_gen_key（2048, F4）→ check → sign/verify | 功能通路 | check=0 + verify=0 | API 契约 | ✅ |
| rsa3072_modexp | 同 2048，3072-bit | 功能通路 | decrypt 黄金明文；sign/verify 回环 | Python cryptography 生成（expected/rsa_vectors_3072_4096.h） | ✅ |
| rsa4096_modexp | 同 2048，4096-bit | 功能通路 | decrypt 黄金明文；sign/verify 回环 | Python cryptography 生成 | ✅ |
| rsa_key_mismatch | rsa_public 上下文做 sign / rsa_private 做 public 应失败 | 负例 | ret != 0 | API 契约 | ✅ |
| rsa_keygen_non_f4_fallback | gen_key(2048, e=3) 应成功（可能回退软件） | 异常/回退 | ret=0 + check_privkey=0 | Kconfig help：非 F4 回退软件 | ✅ |
| **rsa2048_otbn_hit** | **直调 `mbedtls_rsa_public_otbn()`（非 static 全局符号，rsa.c:52 声明）** + `ls_otbn_imem_firmware_get()` + 公/私回环 | 功能通路（**OTBN 命中**） | hook `ret==0`（OTBN 硬件真的算完 2048-bit modexp）；`IMEM firmware == OTBN_FIRMWARE_RSA_MODEXP(8)`；`private(public(in)) == in` | `rsa_alt.c:157-202` 门限 256/384/512B + `ls_otbn_config.h:66/190` | ⏳ 新增 |

> **⚠️ RSA 覆盖声明的关键限定（审计 A 组 P1-2，2026-09-18）**：
> `modules/crypto/mbedtls/library/rsa.c` 在 OTBN hook 返回非 0 时**静默回落软件 modexp**
> （`:1287-1293` / `:1518-1524` 把 hook 返回值**改写为 0** 后继续软件路径，无日志无上抛）
> ⇒ `rsa2048/3072/4096_modexp` 的 KAT 逐字节相等**不能证明 OTBN 被使用**
> （`case_sw_baseline` 与 `case_otbn_rsa` 的 RSA 结果逐字节相同）。
> 上述 4 个 KAT 项在**开了 `CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT` 的 case**
> （`case_otbn_rsa` / `case_otbn_all` / `case_all_on_max` / `case_otbn_sha256_hw_sm3` /
> `case_thread_mix`）下**预期**走 OTBN，但"是否真的走到"由新增的 `rsa2048_otbn_hit`
> 单独负责断言（`#if defined(MBEDTLS_RSA_OTBN_HOOK)` 守卫，纯软件 case 不编入）。

### OTBN PointOps（直接 API）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| field_bytes | ls_otbn_pointops_field_bytes(P256/P384/P521) | 功能通路 | 返回 32/48/66 | otbn_pointops.h 文档 | ✅ |
| base_mult | ls_otbn_pointops_base_mult（R = k * G） | 功能通路 | (rx,ry) 与 NIST 向量一致 | otbn_pointops sample test_vectors.h | ✅ |
| scalar_mult | ls_otbn_pointops_scalar_mult（R = k * P） | 功能通路 | (rx,ry) 与 NIST 向量一致 | otbn_pointops sample | ✅ |
| point_add | ls_otbn_pointops_point_add（R = G + P） | 功能通路 | (rx,ry) 与 NIST 向量一致 | otbn_pointops sample | ✅ |
| stress_reentry | 3 曲线 x 3 运算连续 3 轮 | 重入/状态隔离 | 全部 ret=0 | 高强度调用 | ✅ |
| pointops_field_bytes_bad | field_bytes(非法 curve/NULL bytes) | 异常参数 | ret != 0（-EINVAL） | API 契约 | ✅ |
| pointops_scalar_mult_bad | NULL scalar/非法 curve | 异常参数 | ret != 0 | API 契约 | ✅ |
| pointops_base_mult_bad | NULL scalar/非法 curve | 异常参数 | ret != 0 | API 契约 | ✅ |
| pointops_point_add_bad | NULL 坐标/非法 curve | 异常参数 | ret != 0 | API 契约 | ✅ |
| ~~pointops_scalar_edge~~ | **已删除（2026-09-18）**：原断言 `ret == 0 \|\| ret != 0` 对任意 ret 恒真、**永不可 FAIL**；scalar=0 的确定性结果（无穷远点编码）无驱动/固件契约可依，按审计裁定删除，未编造替代断言 | — | — | — | ❌ 删除 |

### Thread / Concurrency

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| concurrent_otbn_hash_ecc | 线程 A SHA-256 OTBN + 线程 B ECDSA P-256 OTBN | 并发资源竞争 | 两线程均完成、ret=0、不崩溃 | THREADING_ALT 互斥 | ✅ |
| shared_sha256_ctx_thread | 主线程 + 子线程同时 update 同一 ctx | 共享上下文互斥 | 不崩溃，至少一个成功 | THREADING_ALT 互斥 | ✅ |
| concurrent_hash_ecdsa（套件 `mbedtls_thread_safety`，**每个 case 都编入**） | 线程 A SHA-256 + 线程 B ECDSA P-256（当前 case 已启用的算法路径） | 并发资源竞争 | 两线程均完成、ret=0 | THREADING_ALT 互斥（`concurrency_test.c`） | ✅ |

### 堆 / 资源（P1a 新增）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| **heap_stress_keygen** | mbedtls buffer allocator（`zephyr/modules/mbedtls/zephyr_init.c` 建堆 + `memory_buffer_alloc.c` 的 alloc/free 计数）经 `mbedtls_ecp_group_load` / `mbedtls_ecp_gen_keypair` / `*_free` 路径 | 统计/性质（资源泄漏） | 16 轮 P-256 keygen（每轮立即释放）后：逐轮占用 `max_after <= first_after`（不单调上涨）；`group_load→keygen×16→group_free` 整周期结束后 `post_used <= pre_used`（无净增长 = 无泄漏）；`verify()==0`（头链完好）；`peak_used < CONFIG_MBEDTLS_HEAP_SIZE`（未触顶） | 驱动源码 `memory_buffer_alloc.c`（`total_used`/`maximum_used` 计数）+ `zephyr_init.c:28-45` | ⏳ |

## 判定说明

- **向量正确性**：确定性项全部逐字节比对（失败 zassert 输出 `byte[i] 0xXX != 0xYY`，§13.5）。
- **跨 case 对齐**：同算法同向量在 sw 基线 / 硬件 / OTBN case 下全对即视为旁路一致性通过；
  共享树 alt 启用后无法在运行时强制走软件路径，因此以“跨 case 输出一致”作为路由正确性证据。
- **回环项**（*_keygen）：随机密钥无标准向量，判定 = genkey→sign→verify 自洽 + `INFO` 耗时/参数上报（§13.4），耗时只作观测不判 FAIL。
- **SM4-CBC 组合语义**：sm4_alt 头文件只暴露 ECB/CTR 原语（共享树现状，不代改），CBC 在测试层用 ECB 原语组合并与 OpenSSL 标准 CBC 向量比对——硬件 ECB 原语与链接逻辑一并被 KAT 覆盖。若共享树后续补 CBC 原语，可替换组合实现。
- **鲁棒性判定**：不追求所有异常都返回特定错误码，而是确认接口不 HardFault、返回码符合文档或保持静默失败。负例项预期 `ret != 0`；共享上下文并发项以“不崩溃”为通过标准。

## P1a 新增项说明（2026-09-17，只生成+编译通过，未烧录）

新增 **1 项**：`heap_stress_keygen`（加在**共用固件源** `src/robustness_test.c` 末尾，
故 11 个 case 变体都编入；既有测试项一行未改）。

- **新增配置项 1 处**：`prj.conf` 追加 `CONFIG_MBEDTLS_MEMORY_DEBUG=y`。
  必要性与安全性：`mbedtls_memory_buffer_alloc_cur_get` / `max_get` 及 `total_used` /
  `maximum_used` / `header_count` 计数在库源码 `memory_buffer_alloc.c` 里**全部位于
  `MBEDTLS_MEMORY_DEBUG` 之下**（`zephyr/modules/mbedtls/configs/config-tls-generic.h:363-365`
  把 `CONFIG_MBEDTLS_MEMORY_DEBUG` 映射到该宏）；不开则本用例的核心判据（堆水位）
  根本取不到数。该宏**只增加计数**，不改变分配/释放行为（`MBEDTLS_SELF_TEST` 未开，
  故 `check_all_free()` 等自检路径也不会被激活）。
- 堆本体 = `CONFIG_MBEDTLS_HEAP_SIZE`(65536) 静态 buffer，由 `CONFIG_MBEDTLS_ENABLE_HEAP=y`
  + `MBEDTLS_MEMORY_BUFFER_ALLOC_C` 在 `zephyr_init.c` 的 `init_heap()` 里建立。
- 用例自带 `#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C) && defined(MBEDTLS_MEMORY_DEBUG)`
  守卫：若该配置被关掉，本项**不编入**（不会破坏构建），但也就失去覆盖 —— 属有意为之的
  降级，已在上面写明依赖。

### 跳过项（P1a）

| 候选 | 跳过类型 | 原因 |
|---|---|---|
| **concurrent_multi_algo**（多任务同时调用不同算法） | **已有等价覆盖，不重复造** | 工程内已有两条等价用例：① `concurrency_test.c` 的 `mbedtls_thread_safety::concurrent_hash_ecdsa`（**每个 case 都编入**，线程 A SHA-256 + 线程 B ECDSA P-256，调用当前 case 已启用的算法路径）；② `thread_test.c` 的 `thread::concurrent_otbn_hash_ecc`（case_all_on_max，OTBN SHA-256 长报文 + OTBN ECDSA sign/verify）。二者已覆盖"不同任务并发调用不同算法"这一维度，再加一条只是重复。 |
| RSA 重复 keygen 堆压力 | 运行时预算 | RSA keygen（尤其 `case_sw_baseline` 的纯软 2048）单次可达秒级；本项已用 16 轮 ECP P-256 keygen 达成"重复 keygen + 逐轮水位"判据，再加 RSA 会显著拉长 case 时长且有 timeout 风险。 |

## 修订（2026-09-18，测试有效性审计 A 组 P0-2 / P1-2）

### 删除 `pointops_scalar_edge`（P0-2）
- **原状**：`robustness_test.c` 中唯一断言 `zassert_true(ret == 0 || ret != 0, ...)`
  —— 对任意 `ret` 恒真，**永不可能 FAIL**。
- **处置**：删除（源文件处留说明注释）。**未改成确定性断言**的原因：
  `ls_otbn_pointops_base_mult()`（`otbn_pointops.c:895-921`）对 scalar=0 不做校验、直接
  送 OTBN 固件，"k=0 ⇒ 无穷远点"的**编码形式**无驱动/固件契约可依，无法独立算出黄金值。
  按审计纪律"支撑不了就删，不要编一个假断言"。
- 覆盖影响：`robustness` 套件项数 -1；`pointops_scalar_mult_bad` / `base_mult_bad` /
  `point_add_bad` 三项异常参数覆盖不变。

### 新增 `rsa2048_otbn_hit`（P1-2）
- **动机**：见上方 RSA 节限定说明——静默回落使 4 个 RSA KAT 的 PASS **不证明 OTBN 可用**。
- **实现**（`rsa_test.c` 末尾，`#if defined(MBEDTLS_RSA_OTBN_HOOK)` 守卫）：
  1. `mbedtls_rsa_public_otbn(&rsa, in, out)` 返回 **0** —— 唯一能证明"OTBN 硬件完成了
     2048-bit modexp"的判据（`rsa_alt.c:157-202` 全链路成功才 return 0）。
  2. `ls_otbn_imem_firmware_get() == OTBN_FIRMWARE_RSA_MODEXP`（紧跟调用读取）——证明
     `rsa.c` 的 hook 集成路径被走到（未回落软件）。
  3. `mbedtls_rsa_private(public(in)) == in` 回环 —— 抓"返回 0 但结果错"。
- **驱动正确时必过 / 典型失效时必不过**：OTBN RSA 通路正常 ⇒ 三条全过；
  OTBN 不可用（会话/固件加载/执行失败）⇒ 第 1 条 `ret != 0` 必 FAIL；
  hook 未被 `rsa.c` 调用（回落）⇒ 第 2 条 `IMEM firmware != 8` 必 FAIL；
  硬件算错但返回 0 ⇒ 第 3 条回环失败必 FAIL。
- **未使用 `LSOTBN->TNSN_CNT` 硬件指令计数**：该寄存器在本仓库零引用、零文档，语义按
  OpenTitan `INSN_CNT` 推断、未经本芯片验证（调研结论）。按纪律**不采信未验证的期望值**，
  故不纳入断言（如需，应先跑一次打印实测值确认）。
- **未改 `modules/**`**：`mbedtls_rsa_public_otbn` 是非 static 全局符号（`zephyr.map` 已确认
  链接进固件），测试侧仅补 `extern` 原型。

---

## 修订（2026-09-29，客户反馈 AES-CTR ALT 契约）

### 背景
客户反馈 `CONFIG_MBEDTLS_CIPHER_AES_LINKEDSEMI=y` 下
`modules/crypto/mbedtls/library/aes_alt.c:358` 的 `mbedtls_aes_crypt_ctr()`
未实现 mbedtls 要求的 CTR 调用状态管理（`nc_off` / `stream_block` /
`nonce_counter` **三个状态参数一个都没用**），且循环体假定长度是 16 的整数倍、
输入输出 4 字节对齐 ⇒ 分段调用状态丢失（CTR keystream 复用）、非整块输入越界读写。

### 原覆盖为何漏掉它
原 `aes128_ctr` 只覆盖「单次调用 + 64 字节（4 个整块）+ `iv_off=0`」这一条
**恰好可能正确**的路径，且**只断言密文**，`iv_off` / `nonce_counter` /
`stream_block` 调用后状态一个都没检查 ⇒ 对该缺陷零覆盖。该 ✅ 属实但无判别力。

### 新增 5 项（全部编入**每个** case，故 `case_sw_baseline` 与硬件 case 构成 A/B 对照）
`aes_ecb_order`（数据字节序契约）、`aes128_ctr_seg`（分段等价 + 调用状态）、
`aes128_ctr_len`（1/15/17/31 + 越界哨兵）、`aes128_ctr_inplace`（原地）、
`aes128_ctr_unaligned`（未对齐）。

**`aes_ecb_order` 的判定逻辑**：`aes_alt.c` 三条路径对同一组 DATA/RES 寄存器用了
**互相矛盾**的字节序约定 —— ECB(`:149-159`) 不做转换且是唯一传 `CRYPT_TYPE=0x2` 的，
CBC(`:200-211`) 与 CTR(`:383-393`) 做 `__builtin_bswap32` 且传 `CRYPT_TYPE=0x0`。
同一硬件不可能两种约定都对。本项用「单块 CBC(IV=0)」与「单块 CTR(counter=明文, 明文=0)」
都必须等于 AES-ECB(同一块)」这一**与硬件字节序无关的不变量**去判别。
注意 mbedtls 3.6.2 的 GCM 经 `block_cipher.c:189` 落到 `mbedtls_aes_crypt_ecb`，
故 ECB 的判定同时决定 GCM/CFB/OFB/XTS 是否正确。

### 向量来源（生成器自校验）
`tests/tmp/gen_cipher_vectors.py` 扩展后重新生成 `expected/cipher_vectors_extra.h`：
新增 `MT_AES128ECB_{KEY,PT,CT}`（FIPS-197 C.1，生成器内 assert 比对公开值）、
`MT_AES128CTR_KS`（keystream = PT XOR CT）、`MT_AES128CTR_CNT1/CNT2`（递增后 counter）。
已 diff 确认**只有新增、无既存宏漂移**。

### 同轮修复的其余 ALT 缺陷（O-42，客户未报告，经审计发现后按用户"有问题都要修"裁定一并处理）

同一轮契约审计在同一文件查出另 4 类缺陷，均已修（`modules/crypto/mbedtls/library/aes_alt.c`）：

| 缺陷 | 原状 | 修法（对齐上游 `library/aes.c`） | 新增门禁 |
|---|---|---|---|
| CBC 用断言代替错误码 | `__ASSERT_NO_MSG(length % 16 == 0)`；`CONFIG_ASSERT=n` 时断言消失，循环按 16B 整块读写 ⇒ **越界读写** | 返回 `MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH` | 既有 `aes_bad_params` 现可真正生效 |
| CBC `length == 0` | 丢了上游的早退；加密分支 `memcpy(iv, output-16, 16)` **读调用方缓冲区之前 16 字节**，解密分支把**未初始化栈**写进 `iv` | 补 `if (length == 0) return 0;` | 同上 |
| `aes_hw_setkey` 非法 keybits | 静默 `return 0`、`CRYPT_AESKS` 落 0 ⇒ **用上一次遗留的 KEY 寄存器**算 128 位 AES 并报成功；且 `keybits/8` 未校验，>256 时 `memcpy` 溢出 `ctx->key[32]` | 返回 `MBEDTLS_ERR_AES_INVALID_KEY_LENGTH`；`setkey_enc` 补 `ctx/key` 空指针守卫 | 既有 `aes_bad_params` |
| XTS key1 存文件级全局量 | 只存**指向调用者缓冲的指针** ⇒ 多 context 互踩 / 悬垂 | 存进 `ctx->crypt`（同上游）；去掉每块重复 setkey | **新增** `aes128_xts` / `aes128_xts_multi_ctx` |
| key 装载强转 `uint32_t*` | 假定调用者 key 缓冲 4 字节对齐，未对齐在本核 fault | 改 `MBEDTLS_GET_UINT32_BE` 按字节读（LE 上等价，且对齐安全） | 既有全部 AES KAT |

> **未处理（登记在 `OPEN_ITEMS.md` O-42）**：`aes_free` 不清硬件 KEY 寄存器、忙等无超时。
>
> **➕ 第二轮补修（2026-09-29，板级首验后）**：
> ① **共享寄存器互斥**（原 O-42⑤）：新增文件级 `K_MUTEX_DEFINE(aes_hw_lock)`，
> 把「重装 key → 写 DATA → GO → 轮询」作为**一个事务**包住（只锁单次寄存器写不够 ——
> 别的用户的密钥仍会落在我们的 setkey 与 GO 之间）。`crypt_ctr` 内部改调**不带锁**的
> `aes_hw_ecb_block()`，否则会重复获取同一把不可重入的 `k_mutex` 而死锁。
> 注意：`k_mutex` 是睡眠锁 ⇒ **AES 不可在 ISR 中调用**。
> ② **NULL 入参加固**：`crypt_cbc`/`crypt_ecb`/`crypt_ctr` 补 NULL 守卫
> （板级首验发现 `aes_bad_params` 的故障点只是**移位**到了
> `crypt_cbc` 的 `memcpy(last_input_block, NULL, 16)` —— 原先只守了 setkey）。
> ③ **数据访问对齐安全**：ECB/CBC 的数据读写也改字节访问器（原先强转 `uint32_t*`）。
> 注意两条路径用的访问器**相反**且都必须如此 —— ECB 走硬件 BYTE_SWAP 模式用
> `*_LE`（不手动换序），CBC 走 NOT_SWAPPED 用 `*_BE`（硬件不换、软件换）。
> ④ **`aes_bad_params` 拆分**：它原先混入两条**超出 mbedtls 契约**的 NULL 断言，
> 而上游 `mbedtls_aes_setkey_enc()` 对 ctx/key 不做 NULL 检查（`library/aes.c` 直接解引用），
> **软件基线必然崩溃** ⇒ 纯软件 case 因一条它永远无法满足的断言而失败。
> 已把 NULL 加固移到仅 ALT 编入的新用例 `aes_alt_null_params`，共享用例只留契约断言
> （错误 keybits → 错误码）。**覆盖没有丢，只是换了归属**。

### ⚠️ 判定链同期变更（`common/`，影响全部 810 工程）
`common/ztest_report.py` + `common/judge.py` 新增 R9：**测试项执行窗口内出现 `ZEPHYR FATAL ERROR` 的判 FAIL**。
原先 ztest 的 `test_result` 停在初值、异常吞成假 PASS。本工程受影响的 4 项中
`aes_bad_params` 已由上面的 NULL 守卫消除异常；其余三项（`sha256_bad_params` / `sha384_bad_params` /
`ecdsa_tamper_verify`）的 NULL 解引用在 `sha256_alt.c` / `sha512_alt.c` / `ecdsa.c`，
R9 落地后曾由"假 PASS"转为**真 FAIL**（那是暴露既有缺陷，不是回归）。

> **✅ 后续处置（2026-09-29，用户裁定"拆测试 + 只守自己的 ALT"）**：
> 复核 `include/mbedtls/*.h` 发现 —— mbedtls **逐函数**声明哪些入参允许 NULL
> （如 `sha256_free` 写 "may be NULL"），而 `starts`/`update` 的 ctx 写的是
> **"must not be NULL"**；上游实现对 NULL ctx 不做检查、直接解引用。
> ⇒ `robustness_test.c` 里那一族 NULL 断言**断言的是 mbedtls 明令禁止的调用方式**，
> **软件基线必然崩溃**，它们此前"通过"纯粹是异常被吞成假 PASS。
> **处置**：把 NULL 断言从共享用例移出，共享用例只断言**文档化**契约
> （`is224/is384` 非法 → 错误码；`len=0` 合法；正常路径不变），
> NULL 加固移入 **ALT 专属用例**（`aes_alt_null_params` / `sha256_alt_null_params` /
> `sha512_alt_null_params`，按 `config-linkedsemi-alt.h` 的同一条件门控）。
> **覆盖没有丢，只是换了归属。**
> 我们的 ALT 文件保留全部 NULL 守卫（`aes_alt.c` / `sha256_alt.c` / `sha512_alt.c` / `ecdsa_alt.c`）；
> **vendor 文件（`library/ecdsa.c` 等）的守卫已回退**，不向上游 mbedtls 分叉。
> ⚠️ 未覆盖：OTBN ECDSA ALT 的 NULL 守卫（`ecdsa_alt.c`）**没有对应用例** ——
> 它在非 OTBN ECC 的 case 里不参与编译，无法用统一门控表达，属已知缺口。


### ⚠️ 计数表状态
上文「case 与 Kconfig 路由」表的 `cipher×N` 各格**在本轮新增 7 项后已全部偏小**，
且该表原有数字与本文件源码实际编入项**本就不一致**（例：`case_sw_baseline` 标
`cipher×1`，而 `cipher_test.c` 里 `aes128_cbc`/`aes128_ctr`/`aes128_gcm` 三项
无条件编入）。**故不按推算改数**，待本轮实测报告出来后按实际项数重新导出。

---

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
>
> **登记规则**（避免"为没有确定预期的项编造预期"）：
> 只能落到**单个标量**上的确定预期才登记断言（取该量的自然 INFO 键名，即使当前
> 未上报 —— 那时判定为 `NO_ACTUAL`，属"有预期但观测不到"的缺口，与"无预期"不同）；
> 断言是"多子返回值合取"或"结构性质"的项一律写 `!none`，不硬凑。
>
> ✅ **已修（2026-09-29 复核）**：本条原记 "`rsa_test.c:233/263` 的 `ZT_INFO("rsa%d_modexp", ...)`
> 把格式串当名字字面量 ⇒ 两项恒为 NO_ACTUAL"。**该记载已过期**：当前源码
> `rsa_test.c:193` 的 `rsa_modexp_kat(const char *ztest_name, int bits, ...)` 已把名字
> 作为参数传入，`:233`/`:263` 用的是 `ZT_INFO(ztest_name, ...)`，调用点
> `:274`/`:287` 分别传 `"rsa3072_modexp"` / `"rsa4096_modexp"`。
> 报告侧亦已能取到值（`case_hw_all` 见 `rsa3072_modexp us=…`、`rsa4096_modexp us=…`）。
> ⇒ 这两项的预期**不是** NO_ACTUAL，已可正常判定；下方 expect 行里的 ★ 标注一并删除。

```expect
# 测试项 | 断言 | 来源标签:说明
sha224_kat | algo == sha224; vecs == 5; long_bytes == 1000000 | handcalc:FIPS 180-4 4 组短向量 + 百万个 'a' ⇒ 5 组、长向量 1e6B
sha256_kat | algo == sha256; vecs == 5; long_bytes == 1000000 | handcalc:同上（sha256）
sha384_kat | algo == sha384; vecs == 5; long_bytes == 1000000 | handcalc:同上（sha384）
sha512_kat | algo == sha512; vecs == 5; long_bytes == 1000000 | handcalc:同上（sha512）
sm3_kat | algo == sm3; vecs == 5; long_bytes == 1000000 | handcalc:GM/T 0004 附录 A 4 组 + 百万 'a'
aes128_cbc | algo == aes-128-cbc; bytes == 64 | handcalc:NIST SP800-38A F.2.1 四块 × 16B = 64
aes128_ctr | algo == aes-128-ctr; bytes == 64 | handcalc:NIST SP800-38A F.5.1 四块 × 16B = 64
aes_ecb_order | ecb_eq_nist == 1; cbc_eq_nist == 1; ctr_eq_nist == 1 | golden:FIPS-197 附录 C.1 —— 三路各以不同参数形态还原同一块 ECB，必须都等于标准密文
aes128_ctr_seg | seg_eq_nist == 1; state_after7 == 1; state_after23 == 1; nc_off_final == 0 | golden:NIST F.5.1 —— 分段只改变 XOR 切分、不改变 keystream；状态量由公开 counter 序列推出
aes128_ctr_len | len1 == 1; len15 == 1; len17 == 1; len31 == 1; guard_ok == 1 | golden:NIST F.5.1 密文前 N 字节 + 缓冲区哨兵不被改写
aes128_ctr_inplace | inplace_eq_nist == 1 | golden:NIST F.5.1
aes128_ctr_unaligned | unaligned_eq_nist == 1 | golden:NIST F.5.1
aes128_xts | kat1 == 1; kat2 == 1; kat3 == 1; dec1 == 1; dec2 == 1; dec3 == 1; steal55 == 1 | golden:IEEE P1619/D16 向量 1-3（mbedtls test_suite_aes.xts.data）+ 55B 回环
aes128_xts_multi_ctx | ctx_a_eq_kat == 1; ctx_b_eq_kat == 1 | golden:同上 —— 两 context 交错后各须等于自己的 KAT
aes_alt_null_params | setkey_null_ctx != 0; setkey_null_key != 0; cbc_null_in != 0; cbc_null_out != 0; ecb_null_in != 0; ecb_null_out != 0; ctr_null_ncoff != 0 | contract:ALT 加固承诺（超出 mbedtls 契约，故仅 ALT case 编入）—— 每项须返回错误码而非崩溃
aes128_gcm | algo == aes-128-gcm; bytes == 32 | handcalc:NIST GCMVS 两块 × 16B = 32
sm4_ecb | algo == sm4-ecb; bytes == 32 | handcalc:draft-ribose-cfrg-sm4-10 A.2.1.1 两组 × 16B
sm4_cbc | algo == sm4-cbc; bytes == 32 | handcalc:GB/T 32907 两组 × 16B
sm4_ctr | algo == sm4-ctr; bytes == 32 | handcalc:draft-ribose-cfrg-sm4-10 A.2.5.1 两组 × 16B
p256_kat | curve == p256; mode == fixed_k_kat | spec:定 k 黄金向量组的自报标识（错标即发现）
p256_keygen | curve == p256; mode == keygen_sign_verify | spec:同上
p384_keygen | curve == p384; mode == keygen_sign_verify | spec:同上
rsa2048_keygen | algo == rsa2048; keylen == 2048; exponent == 65537 | handcalc:RSA-2048 + F4 指数（FIPS 186-5 常用取值）
rsa2048_modexp | algo == rsa2048; stage == signverify | handcalc:2048-bit modexp 的 sign/verify 段
rsa3072_modexp | algo == rsa3072; stage == signverify | handcalc:3072-bit 段（INFO 名已按调用点传入，可取值）
rsa4096_modexp | algo == rsa4096; stage == signverify | handcalc:4096-bit 段（INFO 名已按调用点传入，可取值）
concurrent_hash_ecdsa | loops == 5; sha_ret == 0; ecdsa_ret == 0 | contract:THREADING_ALT 互斥下的并发返回码
heap_stress_keygen | loops == 16; max_after <= first_after | handcalc:16 轮 keygen 后逐轮占用不得单调上涨（内存不泄漏的不变量）
ecdsa_tamper_verify | ret != 0 | contract:API 契约——篡改签名必须验签失败（★无 INFO 通道 ⇒ NO_ACTUAL）
rsa_key_mismatch | ret != 0 | contract:API 契约——密钥不匹配必须失败（★无 INFO 通道 ⇒ NO_ACTUAL）
rsa_keygen_non_f4_fallback | ret == 0; check_privkey == 0 | contract:Kconfig help 的非 F4 回退路径（★无 INFO 通道 ⇒ NO_ACTUAL）
sm2_keygen | verify == 0 | contract:API 契约（★无 INFO 通道 ⇒ NO_ACTUAL）
stress_reentry | ret == 0 | contract:API 契约（★无 INFO 通道 ⇒ NO_ACTUAL）
pointops_field_bytes_bad | ret != 0 | contract:API 契约（★无 INFO 通道 ⇒ NO_ACTUAL）
pointops_scalar_mult_bad | ret != 0 | contract:API 契约（★无 INFO 通道 ⇒ NO_ACTUAL）
pointops_base_mult_bad | ret != 0 | contract:API 契约（★无 INFO 通道 ⇒ NO_ACTUAL）
pointops_point_add_bad | ret != 0 | contract:API 契约（★无 INFO 通道 ⇒ NO_ACTUAL）
concurrent_otbn_hash_ecc | sha_ret == 0; ecdsa_ret == 0 | contract:THREADING_ALT 互斥下的并发返回码（★无 INFO 通道时 ⇒ NO_ACTUAL）
sha256_bad_params | !none | !none:契约是多个子返回值的**合取**，落不到单个标量上，不硬凑
sha384_bad_params | !none | !none:同上
sha256_alt_null_params | starts_null_ctx != 0; update_null_input != 0 | contract:ALT 加固承诺（超出 mbedtls 契约，仅 SHA256 ALT case 编入）
sha512_alt_null_params | starts_null_ctx != 0; update_null_input != 0 | contract:同上（SHA512 ALT）
aes_bad_params | !none | !none:同上
sm4_bad_params | !none | !none:同上
field_bytes | !none | !none:按调用上下文返回 32/48/66，非单值断言
base_mult | !none | !none:断言对象是 NIST 向量比对结果，非单个标量
scalar_mult | !none | !none:同上
point_add | !none | !none:同上
shared_sha256_ctx_thread | !none | !none:"不崩溃、至少一个成功"无标量语义
```
