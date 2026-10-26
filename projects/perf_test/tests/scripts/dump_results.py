#!/usr/bin/env python3
"""perf_test 测试项清单（链接序）。

perf_test 的批量路径不取内存兜底（common/acquire.py::dual_run 传 mem=None），
本文件只作为测试项清单的权威源与人工核对入口。顺序必须与 ELF 的
`z_ztest_unit_test__<suite>__<test>` 段一致（SORT_BY_NAME）：
  TGT: perf_crypto 11 -> perf_dma 1 -> perf_flash 6 -> perf_mbox 1 -> perf_trng 2
  CTRL: perf_flash_proxy 3 -> perf_peer 1

核对命令：
  nm --numeric-sort <elf> | grep _ztest_unit_test__
"""

TEST_NAMES_BY_CORE = {
    "TGT": [
        "aes128_cbc", "aes128_ctr", "aes128_ecb",
        "sha256", "sha256_unaligned",
        "sha512", "sha512_unaligned",
        "sm3", "sm3_unaligned",
        "sm4_ctr", "sm4_ecb",
        "mem2mem",
        "flash1_erase", "flash1_read", "flash1_write",
        "flash2_erase", "flash2_read", "flash2_write",
        "roundtrip",
        "trng0_throughput", "trng1_throughput",
    ],
    "CTRL": [
        "sha256_otbn", "sha256_otbn_unaligned",
        "sha512_otbn", "sha512_otbn_unaligned",
        "sm3_otbn", "sm3_otbn_unaligned",
        "rsa2048_modexp",
        "proxy_erase", "proxy_read", "proxy_write", "peer_alive",
    ],
}

if __name__ == "__main__":
    for core, names in TEST_NAMES_BY_CORE.items():
        print(f"{core}: {len(names)} item(s)")
        for i, name in enumerate(names):
            print(f"  {i:2d} {name}")
