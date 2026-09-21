#!/usr/bin/env python3
"""expect_check 离线单元测试（无硬件、无归档依赖）。

Run: python 810_driver_test/common/test_expect_check.py
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from expect_check import (                     # noqa: E402
    AGREE, NO_EXPECTATION, NO_ACTUAL, DISAGREE,
    INDEPENDENT, PSEUDO_TAUTOLOGY, PSEUDO_SAME_SOURCE, STRONG, WEAK,
    ExpectEntry, parse_assertion, parse_expect_block, discriminating_power,
    classify, expand_kv, extract_actuals_from_md, _resolve_cols, _as_num,
    resolve_actual,
)


# ---------------------------------------------------------------- 断言解析与求值
def test_parse_assertion_ops():
    a = parse_assertion("delta_s in [0,5]")
    assert (a.key, a.op, a.value) == ("delta_s", "in", (0, 5))
    assert parse_assertion("calib_reg == 0x36007FFE").value == 0x36007FFE
    assert parse_assertion("ctrl.b1 == 1").key == "ctrl.b1"
    assert parse_assertion("ret != -EINVAL") is not None
    assert parse_assertion("z < 6").op == "<"
    assert parse_assertion("r ~ ^0\\.[0-9]+$").op == "~"
    assert parse_assertion("x absent").op == "absent"
    assert parse_assertion("garbage no op here") is None


def test_range_predicate_inclusive():
    """回归：区间断言曾因 `_as_num(tuple)` 返回 None 而**恒不满足**。"""
    a = parse_assertion("chi in [32,224]")
    assert a.test("32") is True and a.test(224.0) is True
    assert a.test(31) is False and a.test(225) is False
    assert a.test("127.30") is True


def test_hex_and_string_equality():
    a = parse_assertion("calib == 0x36007FFE")
    assert a.test("0x36007ffe") is True
    assert a.test("0x36007fff") is False
    assert parse_assertion("k ~ ^up$").test("up") is True
    assert parse_assertion("k ~ ^up$").test("down") is False
    # 键缺失：普通断言不成立；`absent` 成立
    assert a.test(None) is False
    assert parse_assertion("k absent").test(None) is True
    assert parse_assertion("k absent").test("") is True


def test_as_num():
    assert _as_num("0x10") == 16.0
    assert _as_num("2147450") == 2147450.0
    assert _as_num("-134") == -134.0
    assert _as_num("up") is None
    assert _as_num(True) is None


# ---------------------------------------------------------------- 伪独立识别
def _entry(assertions, tag="handcalc", note=""):
    e = ExpectEntry(name="x", tag=tag, note=note)
    for s in assertions.split(";"):
        e.assertions.append(parse_assertion(s))
    return e


def test_probe_detects_tautology():
    # 宽到覆盖全部探针的区间 ⇒ 不可能 DISAGREE ⇒ 伪独立
    assert discriminating_power(parse_assertion("x in [0,18446744073709551615]")) == 0
    assert discriminating_power(parse_assertion("s ~ .*")) == 0
    # 有判别力的断言必须拒掉至少一个探针
    assert discriminating_power(parse_assertion("x in [32,224]")) > 0
    assert discriminating_power(parse_assertion("x == 5")) > 0
    assert discriminating_power(parse_assertion("x != 0xA5")) > 0
    assert discriminating_power(parse_assertion("x >= 0")) > 0


def test_classify_three_gates():
    # 闸 1：标注 samecode
    e = _entry("v == 100", tag="samecode")
    assert classify(e)[0] == PSEUDO_SAME_SOURCE
    # 闸 2：关键词（note 或 COVERAGE「预期来源」列）
    e = _entry("ppb == 100000000", tag="handcalc", note="公式反推得到一个值")
    assert classify(e)[0] == PSEUDO_SAME_SOURCE
    assert classify(_entry("v == 1", tag="spec"), "用驱动自身公式反解")[0] == PSEUDO_SAME_SOURCE
    # 闸 3：恒真（真跑探针）
    assert classify(_entry("x in [0,18446744073709551615]", tag="golden"))[0] == PSEUDO_TAUTOLOGY
    # 正常：独立 + 强
    ind, strength, _ = classify(_entry("chi == 127", tag="golden"))
    assert ind == INDEPENDENT and strength == STRONG
    # contract 独立但弱
    ind, strength, _ = classify(_entry("ret == -EINVAL", tag="contract"))
    assert ind == INDEPENDENT and strength == WEAK


# ---------------------------------------------------------------- 段与实测解析
COV_SAMPLE = """# demo

| 测试项 | 驱动路径 | 维度 | 预期数据 | 预期来源 | 状态 |
|---|---|---|---|---|---|
| alpha | d | x | `a == 1` | 手算 | ✅ |
| beta | d | x | 无 | 公式反推 | ✅ |

## 独立预期（机器可读）

```expect
# name | assertions | provenance
alpha | a == 1; b in [0,5] | handcalc:手工代入
beta  | v == 100 | handcalc:由驱动公式反推
gamma | !none | !none:观测项，无判别力
```
"""


def test_parse_expect_block_and_fence_scope():
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "COVERAGE.md")
        with open(p, "w", encoding="utf-8", newline="") as f:
            f.write(COV_SAMPLE)
        e = parse_expect_block(p)
        assert set(e) == {"alpha", "beta", "gamma"}
        assert len(e["alpha"].assertions) == 2
        assert e["gamma"].is_none
        assert e["gamma"].tag == "!none"
        # 围栏外的覆盖矩阵行不得被当成 expect 行
        assert "beta" in e and e["beta"].assertions[0].key == "v"


def test_coverage_source_feeds_keyword_gate():
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "COVERAGE.md")
        with open(p, "w", encoding="utf-8", newline="") as f:
            f.write(COV_SAMPLE)
        from expect_check import coverage_rows_of
        rows = coverage_rows_of(p)
        assert rows["alpha"] == "手算" and rows["beta"] == "公式反推"
        e = parse_expect_block(p)
        assert classify(e["beta"], rows["beta"])[0] == PSEUDO_SAME_SOURCE
        assert classify(e["alpha"], rows["alpha"])[0] == INDEPENDENT


def test_expand_kv_and_md_extraction():
    assert expand_kv("calib_reg=0x36007ffe,ctrl=0x0000000f") == {
        "calib_reg": "0x36007ffe", "ctrl": "0x0000000f"}
    assert expand_kv("fire1") == {"value": "fire1"}
    assert expand_kv("-") == {}
    md = ("| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |\n"
          "|---|---|---|---|---|---|\n"
          "| t1 | ✅ PASS | token | e | a=1,b=2 |  |\n"
          "| t2 | ✅ PASS | token | e | - |  |\n")
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "r.md")
        with open(p, "w", encoding="utf-8", newline="") as f:
            f.write(md)
        got = extract_actuals_from_md(p)
        assert got["t1"] == {"a": "1", "b": "2"}
        assert "t2" not in got


def test_resolve_actual_bit_key():
    """`ctrl.b1` 位取语法：没有它就只能断言整个 CTRL 字（其余位是残留，不是预期）。"""
    actual = {"ctrl": "0x0000000f", "ret": "-134"}
    assert resolve_actual(actual, "ret") == "-134"
    assert resolve_actual(actual, "ctrl.b1") == 1
    assert resolve_actual(actual, "ctrl.b0") == 1
    assert resolve_actual(actual, "ctrl.b4") == 0
    assert resolve_actual(actual, "ctrl.b99") == 0
    assert resolve_actual(actual, "missing") is None
    assert resolve_actual(actual, "missing.b1") is None
    assert parse_assertion("ctrl.b1 == 1").test(resolve_actual(actual, "ctrl.b1"))


def test_cross_key_relational_assertion():
    """跨键不变量（堆不单调上涨）不能写成字面量 —— 那是把某次实测值抄成预期。"""
    a = parse_assertion("max_after <= first_after")
    assert a.rhs_key == "first_after"
    lookup = {"max_after": "432", "first_after": "432",
              "leaky": "9000"}.get
    assert a.test("432", lookup) is True
    assert a.test("9000", lookup) is False
    # 缺键 ⇒ 不成立（fail-closed），不得静默算过
    assert a.test(None, lookup) is False
    assert a.test("432", (lambda k: None)) is False
    # 字面量右端仍是字面量（别把 `z < 6` 误判成跨键）
    assert parse_assertion("z < 6").rhs_key is None
    # 关系式仍有判别力（用右端=0 探）
    assert discriminating_power(a) > 0


def test_resolve_cols_header_driven():
    # io_probe 式布局：比模板多一个 `#` 列，且列名不同
    hdr = ["#", "测试项（ZTEST `io_probe::<name>`）", "触达代码路径", "维度",
           "预期数据", "预期来源", "状态"]
    n, e, s = _resolve_cols(hdr)
    assert (n, e, s) == (1, 4, 5)
    hdr2 = ["测试项", "驱动路径(函数+分支)", "维度", "预期数据", "预期来源", "状态"]
    assert _resolve_cols(hdr2) == (0, 3, 4)


COV_CASE = """# demo project

| 测试项 | 驱动路径 | 维度 | 预期数据 | 预期来源 | 状态 |
|---|---|---|---|---|---|
| good | d | x | `a == 1` | 手算 | ☐ |
| bad  | d | x | `a == 2` | 手算 | ☐ |
| partial | d | x | `a == 1` | 手算 | ☐ |
| nokey | d | x | `z == 9` | 手算 | ☐ |
| skipped | d | x | — | — | ☐ |

## 独立预期（机器可读）

```expect
good    | a == 1        | handcalc:手工
bad     | a == 2        | handcalc:手工
partial | a == 1; q == 5 | handcalc:手工
nokey   | z == 9        | handcalc:手工
skipped | !none         | !none:观测项
```
"""


def _mk_case(tmp, name, result, infos):
    d = os.path.join(tmp, name)
    os.makedirs(d)
    payload = {"results": {k: v for k, v in result.items()},
               "infos": infos}
    with open(os.path.join(d, "demo_report.json"), "w", encoding="utf-8",
              newline="") as f:
        import json as _j
        f.write(_j.dumps(payload))
    return d


def test_check_case_end_to_end():
    """端到端：AGREE / DISAGREE / NO_ACTUAL / 假PASS / 独立佐证 / 缺键不算违规。"""
    from expect_check import check_case, DISAGREE, AGREE, NO_ACTUAL, NO_EXPECTATION
    with tempfile.TemporaryDirectory() as tmp:
        cov = os.path.join(tmp, "COVERAGE.md")
        with open(cov, "w", encoding="utf-8", newline="") as f:
            f.write(COV_CASE)
        case = _mk_case(tmp, "demo_case_x",
                        {"good": "PASS", "bad": "PASS", "partial": "PASS",
                         "nokey": "PASS", "skipped": "PASS"},
                        {"good": {"a": "1"}, "bad": {"a": "1"},
                         "partial": {"a": "1"}})
        rep = check_case("demo", "case_x", case, cov_path=cov, root=tmp)
        got = {i["name"]: i for i in rep["items"]}
        assert got["good"]["verdict"] == AGREE
        # 违反外部真源却判 PASS ⇒ 假 PASS 风险
        assert got["bad"]["verdict"] == DISAGREE and got["bad"]["conflict"] is True
        # 部分键缺失：只用能取到的键判定，缺失的键写进 note（不算违规）
        assert got["partial"]["verdict"] == AGREE
        assert "q" in got["partial"]["note"]
        # 断言键一个都对不上 ⇒ NO_ACTUAL（不是 DISAGREE）
        assert got["nokey"]["verdict"] == NO_ACTUAL
        assert got["skipped"]["verdict"] == NO_EXPECTATION
        assert rep["stats"]["conflict"] == 1
        assert rep["stats"]["corroborate"] == 0

        # 同向失败 ⇒ 独立佐证（不是新发现）
        case2 = _mk_case(tmp, "demo_case_y", {"bad": "FAIL"},
                         {"bad": {"a": "1"}})
        rep2 = check_case("demo", "case_y", case2, cov_path=cov, root=tmp)
        b = [i for i in rep2["items"] if i["name"] == "bad"][0]
        assert b["verdict"] == DISAGREE and b["corroborate"] is True
        assert b["conflict"] is False
        assert rep2["stats"]["conflict"] == 0


def test_norm_name_core_prefix():
    from expect_check import norm_name
    assert norm_name("cpu2::test_x") == "test_x"
    assert norm_name("CTRL::foo") == "foo"
    assert norm_name("plain") == "plain"


if __name__ == "__main__":
    fns = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for fn in fns:
        fn()
    print(f"ALL TESTS PASS ({len(fns)} tests)")
