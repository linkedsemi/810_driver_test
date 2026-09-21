# OTBN selftest 测试覆盖矩阵

被测对象：mbedtls 全量 selftest 在本 SoC 加速配置下的正确性（片内，无外部连接路径，§14 标注"片内"）。
配置与 samples/mbedtls_selftest 完全一致（SHA 走硬件引擎 HARDWARE_ALT，RSA/ECDSA 走 OTBN ALT）。

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| st_calloc | mbedtls 平台 calloc/free + 平台 sanity（SIZE_MAX/padding/NULL/**snprintf 参考向量**） | 功能通路 | ret==0 | mbedtls 自检契约 + C99 snprintf 语义 | ⬜ |
| st_md5 | mbedtls_md5_self_test | 功能通路 | ret==0 | mbedtls 内置 KAT（RFC 1321） | ⬜ |
| st_ripemd160 | mbedtls_ripemd160_self_test | 功能通路 | ret==0 | mbedtls 内置 KAT | ⬜ |
| st_sha1 | mbedtls_sha1_self_test | 功能通路 | ret==0 | FIPS 180 | ⬜ |
| st_sha224 | mbedtls_sha256_self_test（is224 分支，硬件引擎） | 功能通路 | ret==0 | FIPS 180-4 | ⬜ |
| st_sha256 | mbedtls_sha256_self_test（硬件引擎） | 功能通路 | ret==0 | FIPS 180-4 | ⬜ |
| st_sha384 | mbedtls_sha512_self_test（is384 分支，硬件引擎） | 功能通路 | ret==0 | FIPS 180-4 | ⬜ |
| st_sha512 | mbedtls_sha512_self_test（硬件引擎） | 功能通路 | ret==0 | FIPS 180-4 | ⬜ |
| st_sha3 | mbedtls_sha3_self_test | 功能通路 | ret==0 | FIPS 202 | ⬜ |
| st_des | mbedtls_des_self_test | 功能通路 | ret==0 | mbedtls 内置 KAT | ⬜ |
| st_aes | mbedtls_aes_self_test（硬件 AES alt） | 功能通路 | ret==0 | NIST SP800-38A | ⬜ |
| st_gcm | mbedtls_gcm_self_test | 功能通路 | ret==0 | NIST SP800-38D | ⬜ |
| st_ccm | mbedtls_ccm_self_test | 功能通路 | ret==0 | NIST SP800-38C | ⬜ |
| st_nist_kw | mbedtls_nist_kw_self_test | 功能通路 | ret==0 | NIST SP800-38F | ⬜ |
| st_cmac | mbedtls_cmac_self_test | 功能通路 | ret==0 | NIST SP800-38B | ⬜ |
| st_chacha20 | mbedtls_chacha20_self_test | 功能通路 | ret==0 | RFC 8439 | ⬜ |
| st_poly1305 | mbedtls_poly1305_self_test | 功能通路 | ret==0 | RFC 8439 | ⬜ |
| st_chachapoly | mbedtls_chachapoly_self_test | 功能通路 | ret==0 | RFC 8439 | ⬜ |
| st_base64 | mbedtls_base64_self_test | 功能通路 | ret==0 | RFC 4648 | ⬜ |
| st_mpi | mbedtls_mpi_self_test | 功能通路 | ret==0 | mbedtls 内置自检 | ⬜ |
| st_rsa | **mbedtls_rsa_self_test（RSA-1024，纯软件基线）** | 功能通路 | ret==0 | mbedtls 内置 KAT（`rsa.c` 内硬编码 RSA-1024） | ⬜ |
| st_camellia | mbedtls_camellia_self_test | 功能通路 | ret==0 | RFC 3713 | ⬜ |
| st_aria | mbedtls_aria_self_test | 功能通路 | ret==0 | RFC 5794 | ⬜ |
| st_ctr_drbg | mbedtls_ctr_drbg_self_test（需 MBEDTLS_CTR_DRBG_C，未开 → NOT_RUN） | - | NOT_RUN | 配置裁剪 | ⬜ |
| st_hmac_drbg | mbedtls_hmac_drbg_self_test | 功能通路 | ret==0 | NIST SP800-90A | ⬜ |
| st_ecp | mbedtls_ecp_self_test（OTBN ECDSA alt 路径关联） | 功能通路 | ret==0 | mbedtls 内置自检 | ⬜ |
| st_ecjpake | mbedtls_ecjpake_self_test | 功能通路 | ret==0 | mbedtls 内置自检 | ⬜ |
| st_dhm | mbedtls_dhm_self_test | 功能通路 | ret==0 | mbedtls 内置自检 | ⬜ |
| st_entropy | entropy 自检（需 MBEDTLS_ENTROPY_C，未开 → NOT_RUN） | - | NOT_RUN | 配置裁剪（样本注释：涉及穿插计算，硬件不支持） | ⬜ |
| st_pkcs5 | mbedtls_pkcs5_self_test | 功能通路 | ret==0 | RFC 8018 | ⬜ |
| st_memory_buffer_alloc | 需 MBEDTLS_MEMORY_BUFFER_ALLOC_C（Zephyr 用平台 heap → NOT_RUN） | - | NOT_RUN | 配置裁剪 | ⬜ |

> 预期数据全部内嵌于 mbedtls 库的 self_test 实现（编译进固件），本工程不另存向量文件。
> OTBN 适配接口的独立 KAT 与 RSA/SHA 混用复现见兄弟工程 `projects/otbn_test/`。

## 修订（2026-09-18，测试有效性审计 A 组 P1-1 / P1-2）

### st_calloc —— snprintf 子检查恢复判别力（P1-1）
- **原状**：`platform_sanity_check()` 里的 `run_test_snprintf()`（6 组确定性参考向量，
  `selftest_run.c:217-245`）不匹配时**只打 `[warn]`、不计入 failures** ⇒ 该子检查
  恒不可 FAIL。
- **改**：恢复 `failures++`（`selftest_run.c:282-284` 起）。**选"恢复计数"而非"删除向量"**，
  理由：本工程未定义 `MBEDTLS_PLATFORM_SNPRINTF_ALT`（prj.conf 无该 Kconfig）⇒
  `mbedtls_snprintf` 就是 picolibc 的 `snprintf`；对常量入参，C99 的返回/截断/NUL 保留
  语义是确定的，参考向量在合规平台必然通过 ⇒ 不匹配属**真实平台缺陷**，应当 FAIL。
- **驱动正确时必过 / 典型失效时必不过**：picolibc 合规 ⇒ 必过；若平台 snprintf 被替换成
  stub/截断语义错误（例如不返回 would-be 长度、越界写 NUL）⇒ 必不过。

### st_rsa —— 覆盖声明降级（P1-2）
- **原状声明**："OTBN modexp alt"，暗示覆盖 OTBN RSA 通路。
- **实际**：`st_rsa` 调 `mbedtls_rsa_self_test(1)`，其密钥是**共享树 `modules/crypto/mbedtls/
  library/rsa.c` 内硬编码的 RSA-1024**（`#define KEY_LEN 128`，`rsa.c:2891-2894`）⇒
  `ctx->len == 128`，而 OTBN hook 门限为 256/384/512 字节
  （`rsa_alt.c:157-159`）⇒ hook 每次直接 `return -ENOTSUP`、**静默回落软件**。
  即使 `CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT=y`（本工程 prj.conf:88 确实开着），
  **本项结果与关闭该配置逐字节相同** ⇒ 对 OTBN **零覆盖**。
- **处置**：**降级覆盖声明**（上表 st_rsa 行已改为"RSA-1024，纯软件基线"）。
  未改 `modules/**`（RSA-1024 密钥在库源码内），也未另造断言——因为没有可依据的
  OTBN 切入点可让本项变可证伪。
- **OTBN RSA 通路的判别力由兄弟工程补齐**：`projects/mbedtls_test` 新增
  `rsa2048_otbn_hit`（仅在开启 RSA OTBN ALT 的 case 编入），直调
  `mbedtls_rsa_public_otbn()` 断言返回 0 + `ls_otbn_imem_firmware_get() ==
  OTBN_FIRMWARE_RSA_MODEXP` + 公/私回环，见该工程 COVERAGE「RSA」节。
---

## 独立预期（机器可读）

> 格式 / 判定口径 / 伪独立识别规则见 `common/expect_check.py` 文件头。
> 文法真源：`SPEC_REFERENCE.md` §6.5.6（`expect` 块文法 / op 表 / 三档标签 / 两条纪律）。
> 只登记**不由被测代码算出**的预期；没有确定预期的项显式写 `!none`。
> 来源标签：`handcalc`(手算) `spec`(外部规范/寄存器或 DT 定义) `golden`(黄金向量)
> `crosscheck`(跨来源交叉) `contract`(API 契约值) `driversrc`(转录自被测驱动实现，
> **只证"没变"不证"仍对"**) `samecode`(同源复算，**非独立**) `!none`(无确定预期)。

```expect
# 测试项 | 断言 | 来源标签:说明
st_calloc | !none | !none:mbedtls 自检返回码，多个子项合取
st_md5 | !none | !none:同上
st_ripemd160 | !none | !none:同上
st_sha1 | !none | !none:同上
st_sha224 | !none | !none:同上
st_sha256 | !none | !none:同上
st_sha384 | !none | !none:同上
st_sha512 | !none | !none:同上
st_sha3 | !none | !none:同上
st_des | !none | !none:同上
st_aes | !none | !none:同上
st_gcm | !none | !none:同上
st_ccm | !none | !none:同上
st_nist_kw | !none | !none:同上
st_cmac | !none | !none:同上
st_chacha20 | !none | !none:同上
st_poly1305 | !none | !none:同上
st_chachapoly | !none | !none:同上
st_base64 | !none | !none:同上
st_mpi | !none | !none:同上
st_rsa | !none | !none:同上
st_camellia | !none | !none:同上
st_aria | !none | !none:同上
st_ctr_drbg | !none | !none:原文即 `NOT_RUN`（配置裁剪）⇒ 无预期
st_hmac_drbg | !none | !none:mbedtls 自检返回码
st_ecp | !none | !none:同上
st_ecjpake | !none | !none:同上
st_dhm | !none | !none:同上
st_entropy | !none | !none:原文即 `NOT_RUN`（配置裁剪）⇒ 无预期
st_pkcs5 | !none | !none:mbedtls 自检返回码
st_memory_buffer_alloc | !none | !none:原文即 `NOT_RUN`（配置裁剪）⇒ 无预期
```
