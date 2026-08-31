#!/usr/bin/env python3
"""ztest_report 解析器离线单元测试（无硬件依赖）。

Run: python 810_driver_test/common/test_ztest_report.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ztest_report import parse_ztest, strip_line   # noqa: E402

LINES = [
    "\x1b[8D\x1b[Juart:~$ ZT_BOOT TGT",
    "*** Booting Zephyr OS build v0.0.0 ***",
    "CPU1 TRNG test started",
    "Running TESTSUITE trng",
    "START - basic_16",
    "INFO basic_16: ones=0",
    " PASS - basic_16 in 0.001 seconds",
    "START - invalid_args",
    "   Assertion failed at /src/trng_test.c:150 (invalid_args): ret:1 != -EINVAL",
    " FAIL - invalid_args in 0.002 seconds",
    " SKIP - distinct_32 in 0.000 seconds",
    "TESTSUITE trng failed.",
    "PROJECT EXECUTION FAILED",
]


def test_parse_ztest():
    r = parse_ztest(LINES, tgt_banners=("CPU1 TRNG",))
    assert r["boot_seen"] is True
    assert r["role"] == "TGT"
    assert r["tests"]["basic_16"]["result"] == "PASS"
    assert r["tests"]["invalid_args"]["result"] == "FAIL"
    assert "ret:1 != -EINVAL" in r["tests"]["invalid_args"]["reason"]
    assert r["tests"]["distinct_32"]["result"] == "SKIP"
    assert r["infos"]["basic_16"]["ones"] == "0"
    assert r["execution"] == "FAILED"


def test_parse_kv_multiple():
    r = parse_ztest(["INFO throughput_64k: bytes=65536,us=100,mbps=5.24"],
                    tgt_banners=("CPU1 TRNG",))
    assert r["infos"]["throughput_64k"] == {"bytes": "65536", "us": "100",
                                            "mbps": "5.24"}


def test_strip_line():
    assert strip_line("\x1b[8D\x1b[Juart:~$ INFO x: a=1\r") == "uart:~$ INFO x: a=1"


if __name__ == "__main__":
    test_parse_ztest()
    test_parse_kv_multiple()
    test_strip_line()
    print("ALL TESTS PASS")
