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
SUMMARY_RE = re.compile(r"^\| \[([^\]]+)\]\(([^)]+)\) \| (\S.*?) \| (\d+)/(\d+) \|$")


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
    "passed": int, "total": int}}。"""
    rows = {}
    f = Path(session_dir) / "summary.md"
    if not f.exists():
        return rows
    for ln in f.read_text(encoding="utf-8").splitlines():
        m = SUMMARY_RE.match(ln.strip())
        if m:
            rows[m.group(1)] = {"report": m.group(2), "result": m.group(3),
                                "passed": int(m.group(4)), "total": int(m.group(5))}
    return rows


def update_summary(session_dir, key, result, passed, total, report_rel):
    """按 key（"<project>_<case>"）替换/插入一行并重写 summary.md。"""
    f = Path(session_dir) / "summary.md"
    rows = read_summary(session_dir)
    rows[key] = {"report": report_rel, "result": result,
                 "passed": passed, "total": total}
    lines = [f"# 810_driver_test 汇总（{Path(session_dir).name}）", "",
             "| 测试 | 结果 | 通过/总数 |", "|---|---|---|"]
    total_cases = len(rows)
    pass_cases = sum(1 for v in rows.values() if v["result"] == "PASS")
    for k in sorted(rows):
        v = rows[k]
        lines.append(f"| [{k}]({v['report']}) | {v['result']} | {v['passed']}/{v['total']} |")
    lines.append("")
    lines.append(f"**共 {total_cases} 个测试，通过 {pass_cases} 个，失败 {total_cases - pass_cases} 个**")
    f.write_text("\n".join(lines) + "\n", encoding="utf-8")
