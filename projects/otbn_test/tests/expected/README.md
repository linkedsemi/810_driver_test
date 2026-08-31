# expected/

OTBN 工程的确定性预期数据全部**内嵌在固件源码**中（与 samples/mbedtls_test 同源）：

- SHA-224/256/384/512、SM3 KAT：`cpu1_otbn_test/src/otbn_api_test.c`（FIPS 180-4 / GM/T 0004 官方向量）
- SHA-256 多长度黄金值：同文件 `test_sha256_various_lengths()`（mbedtls_test 沉淀的实测值）
- AES/SM4 KAT：同文件（NIST SP800-38A、draft-ribose-cfrg-sm4-10）
- RSA-2048 OpenSSL 黄金密钥/密文/签名：`cpu1_otbn_test/src/rsa_test_key.h`
- ECDSA P-256 定 k 黄金 r/s：`otbn_api_test.c` 的 `ecdsa_p256_kat_test()`
- 混用测试基准：`src/mix_test.c` 顶部的 `mix_msg_sha256/sha512/sm3` 常量

本目录按 §13 规范保留，作为"预期数据位置"的说明锚点。
