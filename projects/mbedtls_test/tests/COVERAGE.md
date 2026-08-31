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
| sha256_bad_params | mbedtls_sha256_starts/update/finish 异常参数 | 异常参数 | NULL ctx/input 返回非 0；len=0 合法；reset 后再 update 正常 | API 契约 | ✅ |
| sha384_bad_params | mbedtls_sha512_starts/update/finish 异常参数 | 异常参数 | NULL 返回非 0 | API 契约 | ✅ |

### 对称加密

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| aes128_cbc | mbedtls_aes_setkey_enc/dec + mbedtls_aes_crypt_cbc（ENC/DEC；HW 经 `MBEDTLS_CIPHER_AES_LINKEDSEMI`） | 功能通路+异常参数 | 4 块加密向量 + 解密回环 | NIST SP800-38A F.2.1 | ✅ |
| aes128_ctr | mbedtls_aes_setkey_enc + mbedtls_aes_crypt_ctr | 功能通路 | 4 块 CTR 向量 + 解密回环 | NIST SP800-38A F.5.1 | ✅ |
| aes128_gcm | mbedtls_gcm_setkey + gcm_crypt_and_tag | 功能通路+AEAD tag | 2 块 ciphertext + 16B tag + 解密回环 | NIST GCMVS（OpenSSL 复核） | ✅ |
| sm4_ecb | mbedtls_sm4_setkey + sm4_ecb_encrypt/decrypt（HW） | 功能通路 | 32B 加密向量 + 解密回环 | draft-ribose-cfrg-sm4-10 A.2.1.1 | ✅ |
| sm4_cbc | 测试层 CBC 组合（sm4_alt 无 CBC API） | 组合语义 | 32B CBC 向量 + 解密回环 | GB/T 32907（OpenSSL 复核） | ✅ |
| sm4_ctr | mbedtls_sm4_setkey + setiv + sm4_ctr_crypto（HW） | 功能通路 | 32B CTR 向量 + 解密回环 | draft-ribose-cfrg-sm4-10 A.2.5.1（OpenSSL 复核） | ✅ |
| aes_bad_params | mbedtls_aes_setkey_enc/crypt_cbc 异常参数 | 异常参数 | NULL ctx/input、错误 keybits 返回非 0 | API 契约 | ✅ |
| sm4_bad_params | mbedtls_sm4_setiv/ctr_crypto 异常参数 | 异常参数 | NULL input 返回非 0 | API 契约 | ✅ |

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
| pointops_scalar_edge | scalar 全 0 / 全 0xFF | 临界值 | 不 HardFault，ret 可记录 | 边界 | ✅ |

### Thread / Concurrency

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| concurrent_otbn_hash_ecc | 线程 A SHA-256 OTBN + 线程 B ECDSA P-256 OTBN | 并发资源竞争 | 两线程均完成、ret=0、不崩溃 | THREADING_ALT 互斥 | ✅ |
| shared_sha256_ctx_thread | 主线程 + 子线程同时 update 同一 ctx | 共享上下文互斥 | 不崩溃，至少一个成功 | THREADING_ALT 互斥 | ✅ |

## 判定说明

- **向量正确性**：确定性项全部逐字节比对（失败 zassert 输出 `byte[i] 0xXX != 0xYY`，§13.5）。
- **跨 case 对齐**：同算法同向量在 sw 基线 / 硬件 / OTBN case 下全对即视为旁路一致性通过；
  共享树 alt 启用后无法在运行时强制走软件路径，因此以“跨 case 输出一致”作为路由正确性证据。
- **回环项**（*_keygen）：随机密钥无标准向量，判定 = genkey→sign→verify 自洽 + `INFO` 耗时/参数上报（§13.4），耗时只作观测不判 FAIL。
- **SM4-CBC 组合语义**：sm4_alt 头文件只暴露 ECB/CTR 原语（共享树现状，不代改），CBC 在测试层用 ECB 原语组合并与 OpenSSL 标准 CBC 向量比对——硬件 ECB 原语与链接逻辑一并被 KAT 覆盖。若共享树后续补 CBC 原语，可替换组合实现。
- **鲁棒性判定**：不追求所有异常都返回特定错误码，而是确认接口不 HardFault、返回码符合文档或保持静默失败。负例项预期 `ret != 0`；共享上下文并发项以“不崩溃”为通过标准。
