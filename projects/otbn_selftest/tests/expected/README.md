# expected/

selftest 工程的预期数据全部内嵌在 mbedtls 库的 `mbedtls_<alg>_self_test()`
实现中（编译进固件，官方 KAT：FIPS 180-4/202、NIST SP800-38x、RFC 8439 等），
本工程不另存向量文件。OTBN 适配接口的独立 KAT 见兄弟工程
`projects/otbn_test/`（向量内嵌其固件源码）。
