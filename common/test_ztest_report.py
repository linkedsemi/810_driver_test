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


def test_parse_kv_space_separated():
    """空格分隔的 kv 不得把第二个键吞进第一个键的值里（io_probe / spid 形态）。"""
    r = parse_ztest(["INFO claim_spi_spif_interconnect: cross_links=54 verdict=CONNECTED",
                     "INFO positive_control_i3c_link: scl_PJ10_PJ08=2 sda_PJ11_PJ09=2"],
                    tgt_banners=("CPU1 IOPROBE",))
    assert r["infos"]["claim_spi_spif_interconnect"] == {
        "cross_links": "54", "verdict": "CONNECTED"}
    assert r["infos"]["positive_control_i3c_link"] == {
        "scl_PJ10_PJ08": "2", "sda_PJ11_PJ09": "2"}


def test_parse_kv_space_inside_value():
    """逗号分隔 + **值内含空格**（flash_test `id=%02x %02x %02x,cap_byte=…`）时，
    空格是值的一部分，不得把值切短。"""
    r = parse_ztest(["INFO test_flash_jedec_id: id=ab cd ef,cap_byte=0x12"],
                    tgt_banners=("CPU1 FLASH",))
    assert r["infos"]["test_flash_jedec_id"] == {"id": "ab cd ef",
                                                 "cap_byte": "0x12"}


def test_strip_line():
    assert strip_line("\x1b[8D\x1b[Juart:~$ INFO x: a=1\r") == "uart:~$ INFO x: a=1"


# ------------------------------------------------------------------ R9（2026-09-29）
# 执行期间致命异常 ⇒ 不得判 PASS。语料形状取自真归档
# `reports/2026-09-29-1/mbedtls_test_case_hw_all/serial_TGT.log`（L104-134）与
# `reports/2026-09-23-1/dma_test_case_default/serial_TGT.log`（L25-55）。
FATAL_CPU_EXCEPTION = [
    "ZT_BOOT TGT",
    "Running TESTSUITE robustness",
    "===================================================================",
    "START - aes_bad_params",
    "[00:00:01.592,000] \x1b[1;31m<err> os:  mcause: 7, Store/AMO access fault\x1b[0m",
    "[00:00:01.592,000] \x1b[1;31m<err> os:   mtval: 0\x1b[0m",
    "[00:00:01.592,000] \x1b[1;31m<err> os: >>> ZEPHYR FATAL ERROR 0: CPU exception on CPU 0\x1b[0m",
    "[00:00:01.592,000] \x1b[1;31m<err> os: Current thread: 0x100874f8 (aes_bad_params)\x1b[0m",
    "[00:00:01.758,000] \x1b[1;31m<err> os: not Halting system\x1b[0m",
    " PASS - aes_bad_params in 0.173 seconds",
    "===================================================================",
    "START - sm4_bad_params",
    " PASS - sm4_bad_params in 0.001 seconds",
    "TESTSUITE robustness succeeded",
    "PROJECT EXECUTION SUCCESSFUL",
]


def test_fatal_in_window_flips_pass_to_fail():
    """R9 主判据：窗口内出现致命异常 ⇒ 该项不得判 PASS（且理由写明是 CPU 异常）。"""
    r = parse_ztest(FATAL_CPU_EXCEPTION, tgt_banners=("TGT",))
    t = r["tests"]["aes_bad_params"]
    assert t["result"] == "FAIL", "执行期间有致命异常仍判了 PASS（假 PASS 未堵）"
    assert t["token_result"] == "PASS", "原 token 判定必须留档（不得抹掉证据）"
    assert t["fatal"][0]["line"] == 7
    assert "ZEPHYR FATAL ERROR 0: CPU exception on CPU 0" in t["fatal"][0]["text"]
    assert t["fatal"][0]["cause"] == "mcause: 7, Store/AMO access fault"
    assert t["fatal"][0]["thread"] == "aes_bad_params"
    # 理由必须**可区分**：说清是 CPU 异常、不是断言失败，否则读者误判成断言没过
    assert "CPU 异常" in t["reason"] and "非断言失败" in t["reason"]
    assert "第 7 行" in t["reason"]
    assert "PASS" in t["reason"]          # 串口原文的 PASS 要显式点出是假 PASS
    # 对照：同一份采集里**没夹异常**的项逐字不变（不得扩大爆炸半径）
    assert r["tests"]["sm4_bad_params"]["result"] == "PASS"
    assert "fatal" not in r["tests"]["sm4_bad_params"]
    # 收尾标记与既有一致（R9 不改 execution 语义）
    assert r["execution"] == "SUCCESSFUL"
    assert r["infos"] == {}


def test_fatal_without_result_line_creates_item():
    """项被异常打断、**从未打出结果行** ⇒ 照样建 FAIL 条目。

    否则按 token 表取项的家族会**静默少一项**（分母跟着缩水）⇒ 仍是假 PASS。
    """
    lines = [
        "START - test_dma_invalid_channel",
        "<err> os: >>> ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0",
        "<err> os: Current thread: 0x10088310 (test_dma_invalid_channel)",
        "<err> os: not Halting system",
    ]
    r = parse_ztest(lines)
    t = r["tests"]["test_dma_invalid_channel"]
    assert t["result"] == "FAIL"
    assert t["token_result"] is None
    assert "CPU 异常" in t["reason"]


def test_fatal_attribution_by_thread_name_ignores_line_order():
    """横幅落在结果行**之后**（deferred log 行序）时，仍按 `Current thread` 的
    线程名归属到真正的项 —— 线程名是异常发生时刻的真相。"""
    lines = [
        "START - ecdsa_tamper_verify",
        " PASS - ecdsa_tamper_verify in 0.331 seconds",
        "<err> os:  mcause: 5, Load access fault",
        "<err> os: >>> ZEPHYR FATAL ERROR 0: CPU exception on CPU 0",
        "<err> os: Current thread: 0x100874f8 (ecdsa_tamper_verify)",
    ]
    r = parse_ztest(lines)
    assert r["tests"]["ecdsa_tamper_verify"]["result"] == "FAIL"
    assert r["fatal_events"][0]["owner"] == "ecdsa_tamper_verify"


def test_fatal_outside_window_is_not_attributed():
    """窗口外（无 START / 线程名不是测试项）的异常：**记录但绝不改判任何项**
    —— 不静默放过，也不扩大爆炸半径。"""
    lines = [
        "START - basic",
        " PASS - basic in 0.001 seconds",
        "<err> os: >>> ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0",
        "<err> os: Current thread: 0x10088310 (sysworkq)",
        "START - next_one",
        " PASS - next_one in 0.001 seconds",
    ]
    r = parse_ztest(lines)
    assert r["tests"]["basic"]["result"] == "PASS"
    assert r["tests"]["next_one"]["result"] == "PASS"
    assert len(r["fatal_events"]) == 1
    assert r["fatal_events"][0]["owner"] is None


def test_mcause_alone_does_not_flip():
    """`mcause:` 只作取证补充，**单独出现不触发判定**（防误伤：它不是致命的直接
    证据，只是异常现场的一行）。"""
    lines = [
        "START - only_cause",
        "<err> os:  mcause: 7, Store/AMO access fault",
        " PASS - only_cause in 0.001 seconds",
    ]
    r = parse_ztest(lines)
    assert r["tests"]["only_cause"]["result"] == "PASS"
    assert r["fatal_events"] == []


def test_fatal_on_already_failing_item_keeps_both_reasons():
    """已在 FAIL 的项再夹异常：结果仍 FAIL，理由**两者都在**（异常 + 原断言理由）。"""
    lines = [
        "START - mixed",
        "   Assertion failed at /src/x.c:10 (mixed): ret:1 != 0",
        " FAIL - mixed in 0.002 seconds",
        "<err> os: >>> ZEPHYR FATAL ERROR 0: CPU exception on CPU 0",
        "<err> os: Current thread: 0x100874f8 (mixed)",
    ]
    r = parse_ztest(lines)
    t = r["tests"]["mixed"]
    assert t["result"] == "FAIL"
    assert "CPU 异常" in t["reason"]
    assert "ret:1 != 0" in t["reason"]
    assert t["token_result"] == "FAIL"


if __name__ == "__main__":
    test_parse_ztest()
    test_parse_kv_multiple()
    test_parse_kv_space_separated()
    test_parse_kv_space_inside_value()
    test_strip_line()
    test_fatal_in_window_flips_pass_to_fail()
    test_fatal_without_result_line_creates_item()
    test_fatal_attribution_by_thread_name_ignores_line_order()
    test_fatal_outside_window_is_not_attributed()
    test_mcause_alone_does_not_flip()
    test_fatal_on_already_failing_item_keeps_both_reasons()
    print("ALL TESTS PASS")
