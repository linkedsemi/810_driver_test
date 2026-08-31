#!/usr/bin/env python3
"""ztest 原生输出解析器（810_driver_test 公共模块）。

输入：串口采集行（原始，含 ANSI/回车）
输出：见 parse_ztest docstring。所有正则用 re.search（§10.5：
shell 提示符重绘会污染行首，不锚定 ^）。
"""
import re

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

_BOOT_RE = re.compile(r"ZT_BOOT (\S+)")
_RESULT_RE = re.compile(r"(PASS|FAIL|SKIP) - (\S+) in [\d.]+ seconds")
_INFO_RE = re.compile(r"INFO (\S+):\s*(.*)")
_EXEC_RE = re.compile(r"PROJECT EXECUTION (SUCCESSFUL|FAILED)")
_ASSERT_RE = re.compile(r"Assertion failed at\s+(.+?):\s*(.*)")


def strip_line(raw):
    """ANSI 剥离 + 去回车。"""
    return ANSI_RE.sub("", raw).rstrip("\r")


def _parse_kv(text):
    out = {}
    for part in text.split(","):
        if "=" in part:
            k, _, v = part.partition("=")
            out[k.strip()] = v.strip()
    return out


def parse_ztest(lines, ctrl_banners=(), tgt_banners=()):
    """解析一段采集行，返回 dict：

    {"boot_seen": bool, "role": "CTRL"|"TGT"|None,
     "tests": {name: {"result": "PASS|FAIL|SKIP", "reason": str}},
     "infos": {name: {k: v}},
     "execution": "SUCCESSFUL"|"FAILED"|None}

    reason 归因：zassert 的 "Assertion failed at f:l (func): msg" 打印在
    FAIL 行之前，解析器暂存断言行，遇到 FAIL 行时归到该项（§13.5）。
    """
    boot_seen = False
    role = None
    tests = {}
    infos = {}
    execution = None
    pending_assert = None

    for raw in lines:
        ln = strip_line(raw)
        if role is None:
            for b in ctrl_banners:
                if b in ln:
                    role = "CTRL"
            if role is None:
                for b in tgt_banners:
                    if b in ln:
                        role = "TGT"
        if _BOOT_RE.search(ln):
            boot_seen = True
            continue
        m = _RESULT_RE.search(ln)
        if m:
            st, name = m.group(1), m.group(2)
            entry = tests.setdefault(name, {"result": st, "reason": ""})
            entry["result"] = st
            if st == "FAIL" and pending_assert:
                entry["reason"] = pending_assert
            pending_assert = None
            continue
        m = _ASSERT_RE.search(ln)
        if m:
            pending_assert = f"{m.group(1).strip()}: {m.group(2).strip()}"
            continue
        m = _INFO_RE.search(ln)
        if m:
            infos.setdefault(m.group(1), {}).update(_parse_kv(m.group(2)))
            continue
        m = _EXEC_RE.search(ln)
        if m:
            execution = m.group(1)
    return {"boot_seen": boot_seen, "role": role,
            "tests": tests, "infos": infos, "execution": execution}
