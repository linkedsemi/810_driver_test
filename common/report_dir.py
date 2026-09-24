#!/usr/bin/env python3
"""报告会话目录与 summary 管理（810_driver_test 公共模块）。

会话目录（2026-08-29 起）：reports/<YYYY-MM-DD>-<idx>
  idx = 当天第几次测试会话，从 1 累加（扫描已有目录取 max+1）。
  一次批量会话（regression/run_all.py）内全部 case 共用同一个会话目录；
  单独跑某个 case（run_*_tests.py 不带 --session-dir）时自动分配新会话目录。
目录结构：
  reports/2026-08-29-1/
    summary.md                                    # 最外层，行粒度 = case
    trng_test_case_default/trng_test_report.md    # 每 case 一个子文件夹
    i3c_dualcore_test_case_mixed_1m_6m25/i3c_dualcore_test_report.md
旧格式目录（reports/2026-08-29/，无 idx）不再产生，历史归档保持原样可读。
"""
import re
from pathlib import Path

SESSION_RE = re.compile(r"^(\d{4}-\d{2}-\d{2})-(\d+)$")
# O-12（2026-09-20）：通过/总数允许"未知"。run 脚本没留下 summary 行、也拿不到
# 计数时（如 build 失败提前 return），run_all 的兜底行**不得**写 `0/0` —— `0/0`
# 读起来像"测得 0 项、通过 0 项"，掩盖真相"这个 case 没有可信计数"。
SUMMARY_RE = re.compile(
    r"^\| \[([^\]]+)\]\(([^)]+)\) \| (\S.*?) \| (?:(\d+)/(\d+)|未知) \|$")

#: 结果列的**可视标记** —— 只标非 PASS，让 FAIL / ENV 行在一堆 PASS 里一眼可见。
#: **加在词后**（`FAIL ❌`）而不是前面：`PASS`/`FAIL` 在列里左对齐，表格不参差。
#: 纯装饰：`read_summary` 回读时由 `_RESULT_MARK_RE` 剥掉，**不参与任何判定语义**。
#: （加标记前 `FAIL` 行与 `PASS` 行长得一样，扫表时很难找 —— 2026-09-24 用户提出。）
_RESULT_MARKS = (("FAIL", " ❌"), ("ENV", " ⚠️"))
_MARKS_ALT = "|".join(re.escape(m.strip()) for _, m in _RESULT_MARKS)
#: 剥离结果列的标记 —— **前缀后缀都认**：后缀是现行渲染（`FAIL ❌`），前缀是
#: 2026-09-24 中途用过一版（`❌ FAIL`）且已写进过归档。两种都剥，`read_summary`
#: 才能同时读旧归档与现行文件（否则迁移期会把标记当结果词原样回写）。
_RESULT_MARK_RE = re.compile(rf"^\s*(?:{_MARKS_ALT})\s*|\s*(?:{_MARKS_ALT})$")


def _mark_result(result):
    """结果列的渲染文本：非 PASS 在词**后**加可视标记。"""
    r = str(result)
    for prefix, mark in _RESULT_MARKS:
        if r.startswith(prefix):
            return r + mark
    return r


def _num(s):
    """summary 计数列：纯数字 -> int；"未知"（None）-> None（O-12）。"""
    return int(s) if (s and s.isdigit()) else None


#: 汇总尾行：`**共 N 个测试，通过 A 个，失败 B 个[，环境中止 C 个]**`
_TAIL_RE = re.compile(r"^\*\*共 \d+ 个测试")


def _read_trailing_notes(f):
    """取 summary.md 中**尾行之后**的内容（如 `> 失败归因：…` 指针行）。

    `update_summary` 是整文件重写 ⇒ 不保留的话会**静默抹掉**别的模块写在末尾的注记
    （实证：2026-09-24 重跑一次 `update_summary` 就把 `failures_report` 写的
    失败归因指针行冲掉了，见 `OPEN_ITEMS O-38`）。

    只保留**尾行之后**的部分：标题、表格、尾行都由本模块按 `rows` 自己重建，
    不从这里取（否则会与新的 rows 冲突）。
    """
    if not f.exists():
        return []
    lines = f.read_text(encoding="utf-8").splitlines()
    for i, ln in enumerate(lines):
        if _TAIL_RE.match(ln):
            rest = lines[i + 1:]
            while rest and not rest[0].strip():
                rest.pop(0)
            while rest and not rest[-1].strip():
                rest.pop()
            return rest
    return []


def allocate_session_dir(reports_root, date_str):
    """返回 reports_root/<date_str>-<max_idx+1> 并创建之。"""
    reports = Path(reports_root)
    reports.mkdir(parents=True, exist_ok=True)
    max_idx = 0
    for d in reports.glob(f"{date_str}-*"):
        m = SESSION_RE.match(d.name)
        if m:
            max_idx = max(max_idx, int(m.group(2)))
    session = reports / f"{date_str}-{max_idx + 1}"
    session.mkdir(parents=True, exist_ok=True)
    return session


def case_dir_for(session_dir, project, case):
    """返回 <session>/<project>_<case>/ 并创建之。"""
    d = Path(session_dir) / f"{project}_{case}"
    d.mkdir(parents=True, exist_ok=True)
    return d


def read_summary(session_dir):
    """读 summary.md 已有表格行 → {key: {"report": str, "result": str,
    "passed": int|None, "total": int|None}}（None = "未知"，见 SUMMARY_RE）。"""
    rows = {}
    f = Path(session_dir) / "summary.md"
    if not f.exists():
        return rows
    for ln in f.read_text(encoding="utf-8").splitlines():
        m = SUMMARY_RE.match(ln.strip())
        if m:
            # 剥掉结果列的可视标记（`_mark_result` 加的）；**旧文件无标记 → 原样返回**，
            # 故 read_summary 对历史归档保持向后兼容。
            rows[m.group(1)] = {"report": m.group(2),
                                "result": _RESULT_MARK_RE.sub("", m.group(3)),
                                "passed": _num(m.group(4)), "total": _num(m.group(5))}
    return rows


def update_summary(session_dir, key, result, passed, total, report_rel):
    """按 key（"<project>_<case>"）替换/插入一行并重写 summary.md。

    `passed`/`total` 传 None 表示"未知"（O-12：拿不到计数时写"未知"，不写 0/0）。

    尾行口径（F3）：与 `regression/run_all.py` 的控制台尾行对齐 —— 环境中止
    （`ENV-ABORT`，E-2 fail-closed 的产物）**单列**，不与"测试失败"混算。

    结果列（2026-09-24 起）：非 PASS 行加可视标记（`_mark_result`），让 FAIL / ENV
    在一堆 PASS 里一眼可见。**纯渲染，回读时剥掉**；PASS 行不加标记（保持干净）。
    计数列与尾行**逐字不变**。

    末尾注记：本函数整文件重写，但会**保留尾行之后的注记**（`_read_trailing_notes`），
    如 `failures_report` 写的失败归因指针行 —— 否则会被静默抹掉（O-38）。
    """
    f = Path(session_dir) / "summary.md"
    rows = read_summary(session_dir)
    notes = _read_trailing_notes(f)      # ⚠️ 必须在覆写**之前**取，否则就没了
    rows[key] = {"report": report_rel, "result": result,
                 "passed": passed, "total": total}
    lines = [f"# 810_driver_test 汇总（{Path(session_dir).name}）", "",
             "| 测试 | 结果 | 通过/总数 |", "|---|---|---|"]
    total_cases = len(rows)
    pass_cases = sum(1 for v in rows.values() if v["result"] == "PASS")
    # 环境中止 = E-2 fail-closed 写下的 "ENV-ABORT"（前缀判定，容忍将来细分）
    env_cases = sum(1 for v in rows.values() if str(v["result"]).startswith("ENV"))
    # 其余非 PASS 一律计入"失败"（与旧口径一致，故未识别的状态仍按失败报，不静默吞）
    fail_cases = total_cases - pass_cases - env_cases
    for k in sorted(rows):
        v = rows[k]
        cnt = (f"{v['passed']}/{v['total']}"
               if v["passed"] is not None and v["total"] is not None else "未知")
        lines.append(f"| [{k}]({v['report']}) | {_mark_result(v['result'])} | {cnt} |")
    lines.append("")
    lines.append(f"**共 {total_cases} 个测试，通过 {pass_cases} 个，失败 {fail_cases} 个"
                 + (f"，环境中止 {env_cases} 个" if env_cases else "") + "**")
    if notes:
        # 保留别的模块写在末尾的注记（如 `failures_report` 的失败归因指针行）。
        # 本函数是整文件重写 ⇒ 不显式保留就等于静默删除（O-38）。
        lines.append("")
        lines.extend(notes)
    f.write_text("\n".join(lines) + "\n", encoding="utf-8")
