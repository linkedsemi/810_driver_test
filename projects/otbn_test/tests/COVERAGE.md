# OTBN 测试覆盖矩阵

被测对象：OTBN 协处理器的 mbedtls 适配层（片内外设，无外部连接路径，§14 标注"片内"）

| 适配层 | 文件 |
|---|---|
| hash（SHA256/SM3/SHA512 OTBN 固件调度） | `zephyr/soc/linkedsemi/lsqsh/otbn/otbn_hash.c` |
| session/IMEM/DMEM/中断 | `zephyr/soc/linkedsemi/lsqsh/otbn/otbn_common.c` |
| RSA modexp/keygen alt | `modules/crypto/mbedtls/library/rsa_alt.c` |
| SHA256/SM3、SHA512 alt | `modules/crypto/mbedtls/library/sha256_alt.c`、`sha512_alt.c` |
| ECDSA P256/P384/SM2 alt | `modules/crypto/mbedtls/library/ecdsa_alt.c` |

## Phase 1：mbedtls selftest（st_*）

已拆到兄弟工程 `projects/otbn_selftest/`（镜像体积原因，见该工程 COVERAGE.md）。

## Phase 2：OTBN 适配接口（api_* / perf_*）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| api_sha224_kat | otbn_hash_update/final（SHA224 分支） | 功能通路 | 3 组 KAT 逐字节相等 | FIPS 180-4 | ⬜ |
| api_sha256_kat | otbn_hash_update/final（SHA256 分支，含 64B 整块） | 功能通路 | 4 组 KAT | FIPS 180-4 | ⬜ |
| api_sm3_kat | sm3_process_blocks/restore_state/save_state | 功能通路 | 3 组 KAT | GM/T 0004 | ⬜ |
| api_sha384_kat | sha512_process_block（is384 分支） | 功能通路 | 3 组 KAT | FIPS 180-4 | ⬜ |
| api_sha512_kat | sha512_process_block 主路径 | 功能通路 | 3 组 KAT | FIPS 180-4 | ⬜ |
| api_sha256_varlen | otbn_hash_update 的 remain_data 拼凑/DMEM 批量/收尾（14 长度 x 4 偏移 x 单/多 update） | 边界长度 | 逐长度 KAT | FIPS 180-4 + 实测黄金值（mbedtls_test 沉淀） | ⬜ |
| api_aes_kat | aes_alt.c 硬件 AES（ecb/cbc/cfb128/cfb8/ofb/ctr/xts） | 功能通路 | NIST 向量逐字节相等 | NIST SP800-38A/38B 系列 | ⬜ |
| api_sm4_kat | sm4_alt.c 硬件 SM4（ecb/ctr） | 功能通路 | 逐字节相等 | draft-ribose-cfrg-sm4-10 | ⬜ |
| api_rsa2048_kat | rsa_alt.c modexp 公/私钥 + PKCS1 enc/dec/sign/verify | 功能通路 | OpenSSL 生成的密文/签名 KAT | OpenSSL 黄金向量（rsa_test_key.h） | ⬜ |
| api_rsa_keygen_2048/3072/4096 | rsa_alt.c mbedtls_rsa_gen_key_otbn（CHECK_PRIME + KEY_FROM_PQ） | 功能通路 | check_privkey==0 且 sign/verify 通过 | FIPS 186-5 流程契约 | ⬜ |
| api_ecdsa_p256_kat | ecdsa_alt.c P-256 定 k 签名 | 功能通路 | r/s 与参考值逐字节相等 | 定 k 黄金向量（mbedtls_test 沉淀） | ⬜ |
| api_ecdsa_p384 | ecdsa_alt.c P-384 genkey/sign/verify | 功能通路 | verify==0 | API 契约 | ⬜ |
| api_ecdsa_sm2 | ecdsa_alt.c SM2 genkey/sign/verify | 功能通路 | verify==0 | API 契约 | ⬜ |
| perf_sha256_64k | otbn_hash 批量块路径 | 统计/性质 | ret==0；INFO: bytes/us/kbps | 性能基线（throughput 样本） | ⬜ |
| perf_rsa2048_sign | rsa_alt.c modexp 私钥 | 统计/性质 | ret==0；INFO: loops/avg_us | 性能基线 | ⬜ |
| perf_ecdsa_p256 | ecdsa_alt.c P-256 sign/verify | 统计/性质 | ret==0；INFO: sign_ops/verify_ops | 性能基线 | ⬜ |

## Phase 3：OTBN 固件混用复现（mix_*，已知 bug 判定）

| 测试项 | 驱动路径(函数+分支) | 维度 | 预期数据 | 预期来源 | 状态 |
|--------|--------------------|------|----------|----------|------|
| mix_sha256_keygen_seq | load_firmware 的 s_last_firmware_id 缓存：keygen 后同 firmware hash 是否重载 | 中断/时序（固件切换） | keygen 后 sha256("abc...") 仍 == KAT | FIPS 180-4 KAT + 前后一致性 | ⬜ |
| mix_sha256_stream_keygen | otbn_hash_update 流式中间态 restore/save 跨 keygen | 中断/时序 | 56B 两段 update 的 digest == KAT | FIPS 180-4 | ⬜ |
| mix_keygen_sha256_keygen | hash 之后再 keygen：DMEM/IMEM 现场互毁检查 | 中断/时序 | 两次 keygen 均 check_privkey+sign/verify 通过 | FIPS 186-5 流程契约 | ⬜ |
| mix_sign_sha256_stream | RSA_MODEXP 固件与 hash 固件切换 | 中断/时序 | sign/verify==0 且流式 digest == KAT | FIPS 180-4 + API 契约 | ⬜ |
| mix_sha512_stream_keygen | sha512 流式跨 keygen（128B 块算法） | 中断/时序 | digest == KAT | FIPS 180-4 | ⬜ |
| mix_sm3_stream_keygen | sm3 流式跨 keygen | 中断/时序 | digest == KAT | GM/T 0004 | ⬜ |
| mix_ecdsa_sha256_stream | ECDSA 固件与 hash 固件切换 | 中断/时序 | genkey/sign/verify==0 且 digest == KAT | FIPS 180-4 + API 契约 | ⬜ |

## 说明

- 确定性测试的预期值全部内嵌固件（`src/otbn_api_test.c` / `src/mix_test.c` /
  `src/rsa_test_key.h`），与 samples/mbedtls_test 同源；expected/ 目录只留说明。
- 不含硬件 SHA 的 DMA 路径测试（`SHA224_SHA256_SM3_LINKEDSEMI_HARDWARE_ALT` 与
  OTBN SHA ALT 互斥，本工程全走 OTBN；DMA 路径属另一配置轴）。
