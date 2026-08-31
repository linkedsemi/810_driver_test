# OTBN selftest 测试覆盖矩阵

被测对象：mbedtls 全量 selftest 在本 SoC 加速配置下的正确性（片内，无外部连接路径，§14 标注"片内"）。
配置与 samples/mbedtls_selftest 完全一致（SHA 走硬件引擎 HARDWARE_ALT，RSA/ECDSA 走 OTBN ALT）。

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| st_calloc | mbedtls 平台 calloc/free + 平台 sanity（SIZE_MAX/padding/NULL） | 功能通路 | ret==0 | mbedtls 自检契约 | ⬜ |
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
| st_rsa | mbedtls_rsa_self_test（OTBN modexp alt） | 功能通路 | ret==0 | mbedtls 内置 KAT | ⬜ |
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
