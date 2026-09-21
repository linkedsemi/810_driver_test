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


def _num(s):
    """summary 计数列：纯数字 -> int；"未知"（None）-> None（O-12）。"""
    return int(s) if (s and s.isdigit()) else None


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
            rows[m.group(1)] = {"report": m.group(2), "result": m.group(3),
                                "passed": _num(m.group(4)), "total": _num(m.group(5))}
    return rows


def update_summary(session_dir, key, result, passed, total, report_rel):
    """按 key（"<project>_<case>"）替换/插入一行并重写 summary.md。

    `passed`/`total` 传 None 表示"未知"（O-12：拿不到计数时写"未知"，不写 0/0）。

    尾行口径（F3）：与 `regression/run_all.py` 的控制台尾行对齐 —— 环境中止
    （`ENV-ABORT`，E-2 fail-closed 的产物）**单列**，不与"测试失败"混算。
    没有 ENV 行时尾行**逐字不变**（旧会话目录渲染结果与修改前完全相同）。
    """
    f = Path(session_dir) / "summary.md"
    rows = read_summary(session_dir)
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
        lines.append(f"| [{k}]({v['report']}) | {v['result']} | {cnt} |")
    lines.append("")
    lines.append(f"**共 {total_cases} 个测试，通过 {pass_cases} 个，失败 {fail_cases} 个"
                 + (f"，环境中止 {env_cases} 个" if env_cases else "") + "**")
    f.write_text("\n".join(lines) + "\n", encoding="utf-8")
