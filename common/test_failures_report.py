#!/usr/bin/env python3
"""failures_report 离线单元测试（无硬件依赖）。

Run: python 810_driver_test/common/test_failures_report.py
"""
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from failures_report import main, out_name, parse_case_report, parse_summary  # noqa: E402

SUMMARY = """# 汇总

| 测试 | 结果 | 通过/总数 |
|---|---|---|
| [dma_test_case_default](dma_test_case_default/dma_test_report.md) | FAIL | 1/7 |
| [trng_test_case_default](trng_test_case_default/trng_test_report.md) | PASS | 9/9 |
| [eth_test_case_mdio](eth_test_case_mdio/eth_test_report.md) | FAIL | 0/0 |
| [i3c_dualcore_test_case_mixed_1m_6m25](logs/i3c_dualcore_test_case_mixed_1m_6m25.log) | FAIL | 未知 |

**共 4 个测试，通过 1 个，失败 3 个**
"""

DMA_REPORT = """# dma_test 测试报告

- 工程: dma_test
- case: case_default
- 执行标记: FAILED
- 内存兜底: 不适用（本工程无内存兜底）
- 结果: FAIL (1/7)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
| test_a | ✅ PASS | token | - | - |  |
| test_b | ❌ FAIL | token | 0x10 | - | assert at f.c:42 |
| test_c | ❌ FAIL | mem | - | 0x00 | ret not equal |
"""

ETH_REPORT = """# eth_test 测试报告

- 工程: eth_test
- case: case_mdio
- 执行标记: UNKNOWN
- 内存兜底: 无效（哨兵未置位）
- 结果: FAIL (0/0)

## 测试项结果

| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |
|---|---|---|---|---|---|
"""


def _mk_session(root: Path) -> Path:
    s = root / "2026-09-21-1"
    (s / "dma_test_case_default").mkdir(parents=True)
    (s / "eth_test_case_mdio").mkdir(parents=True)
    (s / "logs").mkdir()
    (s / "summary.md").write_text(SUMMARY, encoding="utf-8")
    (s / "dma_test_case_default" / "dma_test_report.md").write_text(
        DMA_REPORT, encoding="utf-8")
    (s / "eth_test_case_mdio" / "eth_test_report.md").write_text(
        ETH_REPORT, encoding="utf-8")
    return s


def test_out_name():
    assert out_name("dma_test_case_default") == "dma_test_default.md"
    assert out_name("i3c_dualcore_test_case_mixed_1m_6m25") == \
        "i3c_dualcore_test_mixed_1m_6m25.md"
    assert out_name("weird_no_case") == "weird_no_case.md"


def test_parse_summary():
    root = Path(tempfile.mkdtemp())
    s = _mk_session(root)
    rows = parse_summary(s)
    assert len(rows) == 4, rows
    assert rows[0][0] == "dma_test_case_default" and rows[0][2] == "FAIL"
    assert rows[1][2] == "PASS"
    assert rows[3][3].strip() == "未知"


def test_parse_case_report():
    root = Path(tempfile.mkdtemp())
    md = root / "r.md"
    md.write_text(DMA_REPORT, encoding="utf-8")
    header, fails = parse_case_report(md)
    assert header["工程"] == "dma_test" and header["执行标记"] == "FAILED"
    # 只收 FAIL 行：表头行、分隔行、PASS 行都必须被排除
    assert len(fails) == 2, fails
    assert [c[0] for c in fails] == ["test_b", "test_c"]
    assert fails[0][4] == "-" and fails[1][3] == "-"


def test_main_end_to_end():
    root = Path(tempfile.mkdtemp())
    s = _mk_session(root)
    assert main(["--session", str(s)]) == 0
    out = s / "failures"
    names = sorted(p.name for p in out.iterdir())
    # PASS 的 case 不得出文件
    assert names == ["dma_test_default.md", "eth_test_mdio.md",
                     "i3c_dualcore_test_mixed_1m_6m25.md"], names

    dma = (out / "dma_test_default.md").read_text(encoding="utf-8")
    assert "| test_b | ❌ FAIL |" in dma
    assert "test_a" not in dma, "PASS 项不应出现在失败明细里"
    assert dma.count("⚠️ 待补（报告未给") == 2, "test_b 的实测、test_c 的期望应为缺失"

    # 0/0 分支：走"未产出任何测试项"，且提示优先看内存兜底
    eth = (out / "eth_test_mdio.md").read_text(encoding="utf-8")
    assert "未产出任何测试项" in eth and "内存兜底" in eth

    # 无 per-case 报告（只有 logs/*.log）也能出文件
    i3c = (out / "i3c_dualcore_test_mixed_1m_6m25.md").read_text(encoding="utf-8")
    assert "无 per-case 报告" in i3c

    # 幂等：默认不覆盖 agent 已填的内容
    (out / "dma_test_default.md").write_text("AGENT 已填", encoding="utf-8")
    assert main(["--session", str(s)]) == 0
    assert (out / "dma_test_default.md").read_text(encoding="utf-8") == "AGENT 已填"
    # --force 才重写
    assert main(["--session", str(s), "--force"]) == 0
    assert "AGENT 已填" not in (out / "dma_test_default.md").read_text(encoding="utf-8")


def test_summary_pointer():
    """指针行：会被加上、幂等、且**绝不改 case 行**。"""
    root = Path(tempfile.mkdtemp())
    s = _mk_session(root)
    before = _case_rows(s)

    assert main(["--session", str(s)]) == 0
    txt = (s / "summary.md").read_text(encoding="utf-8")
    assert "失败归因：见 [`failures/`](failures/)" in txt
    assert "（3 个 FAIL case" in txt, "指针应报 FAIL 数（本例 3 个）"
    assert _case_rows(s) == before, "case 行不得被触碰"

    # 幂等：再跑一次不得出现第二行指针
    assert main(["--session", str(s)]) == 0
    assert (s / "summary.md").read_text(encoding="utf-8").count("> 失败归因：") == 1
    assert _case_rows(s) == before


def test_summary_pointer_cleared_when_no_fail():
    """全 PASS 时指针行应被移除（不残留上一轮的）。"""
    root = Path(tempfile.mkdtemp())
    s = _mk_session(root)
    assert main(["--session", str(s)]) == 0
    assert "> 失败归因：" in (s / "summary.md").read_text(encoding="utf-8")
    # 把判据全改成 PASS
    (s / "summary.md").write_text(
        SUMMARY.replace("| FAIL |", "| PASS |"), encoding="utf-8")
    assert main(["--session", str(s)]) == 0
    assert "> 失败归因：" not in (s / "summary.md").read_text(encoding="utf-8")


def _case_rows(session: Path):
    return [ln for ln in (session / "summary.md").read_text(encoding="utf-8").splitlines()
            if ln.startswith("| [")]


def main_():
    test_out_name()
    test_parse_summary()
    test_parse_case_report()
    test_main_end_to_end()
    test_summary_pointer()
    test_summary_pointer_cleared_when_no_fail()
    print("ALL TESTS PASS")


if __name__ == "__main__":
    main_()
