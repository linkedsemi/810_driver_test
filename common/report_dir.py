#!/usr/bin/env python3
"""报告会话目录与 summary 管理（810_driver_test 公共模块）。

会话目录按**运行类型**分两处（2026-11-04 起）：
  - 全量回归（regression/run_all.py）：reports/<YYYY-MM-DD>-<idx>/
  - 单模块测试（run_test.py 单独运行）  ：reports/module/<YYYY-MM-DD>-<idx>/
  idx = 当天该类型第几次会话，从 1 累加（各类型独立扫描取 max+1）。
  一次批量会话内全部 case 共用同一个会话目录；单独跑某个 case（不带
  --session-dir）时自动分配 module 会话目录。
每个会话目录写 session_meta.json（kind / project(s) / git_sha），summary 标题
据此显示“全量/单模块”。历史目录（无 meta）保持原样可读。
目录结构：
  reports/2026-08-29-1/                    # 全量
    summary.md
    trng_test_case_default/trng_test_report.md
  reports/module/2026-08-29-1/             # 单模块
    summary.md
    trng_test_case_default/trng_test_report.md
旧格式目录（reports/2026-08-29/，无 idx）不再产生，历史归档保持原样可读。
"""
import datetime
import functools
import json
import re
import subprocess
from pathlib import Path

KIND_MODULE = "module"
KIND_FULL = "full"
KINDS = (KIND_MODULE, KIND_FULL)
MODULE_SUBDIR = "module"
SESSION_META = "session_meta.json"
META_SCHEMA = 1

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


def _kind_root(reports_root, kind):
    """会话根目录：full -> reports/；module -> reports/module/。"""
    root = Path(reports_root)
    if kind == KIND_FULL:
        return root
    if kind == KIND_MODULE:
        return root / MODULE_SUBDIR
    raise ValueError(f"unknown report kind: {kind!r}（只接受 {KINDS}）")


def plan_session_dir(reports_root, date_str, kind=KIND_MODULE):
    """返回下一个会话目录 Path；**不创建目录、不写 meta**（dry-run 用）。"""
    root = _kind_root(reports_root, kind)
    max_idx = 0
    if root.exists():
        for d in root.glob(f"{date_str}-*"):
            m = SESSION_RE.match(d.name)
            if m:
                max_idx = max(max_idx, int(m.group(2)))
    return root / f"{date_str}-{max_idx + 1}"


@functools.lru_cache(maxsize=1)
def _git_sha():
    """当前 810_driver_test 仓库的 12 位短 sha；取不到返回 None。"""
    repo = Path(__file__).resolve().parents[1]
    try:
        out = subprocess.run(
            ["git", "-C", str(repo), "rev-parse", "--short=12", "HEAD"],
            capture_output=True, text=True, timeout=2)
        sha = out.stdout.strip()
        return sha if out.returncode == 0 and sha else None
    except (OSError, subprocess.SubprocessError):
        return None


def _write_session_meta(session_dir, kind, project=None, projects=None):
    """写 session_meta.json；失败静默（不得影响回归主流程）。"""
    meta = {
        "schema": META_SCHEMA,
        "kind": kind,
        "session": Path(session_dir).name,
        "created": datetime.datetime.now().isoformat(timespec="seconds"),
        "repo": "810_driver_test",
        "git_sha": _git_sha(),
        "project": project,
        "projects": sorted({p for p in (projects or []) if p}),
    }
    try:
        (Path(session_dir) / SESSION_META).write_text(
            json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    except OSError:
        pass


def read_session_meta(session_dir):
    """读 session_meta.json；缺失/损坏返回 {}（绝不抛异常）。"""
    try:
        meta = json.loads((Path(session_dir) / SESSION_META).read_text(encoding="utf-8"))
        return meta if isinstance(meta, dict) else {}
    except (OSError, ValueError):
        return {}


def session_kind(session_dir):
    """meta.kind 优先；否则父目录名为 module 判 module，其余判 full。"""
    kind = read_session_meta(session_dir).get("kind")
    if kind in KINDS:
        return kind
    return KIND_MODULE if Path(session_dir).parent.name == MODULE_SUBDIR else KIND_FULL


def register_session_project(session_dir, project):
    """把 project 记入 meta.projects（幂等）；历史全量目录（无 meta）不写。"""
    if not project:
        return
    session = Path(session_dir)
    meta = read_session_meta(session)
    if not meta:
        if session.parent.name != MODULE_SUBDIR:
            return
        meta = {
            "schema": META_SCHEMA,
            "kind": KIND_MODULE,
            "session": session.name,
            "created": datetime.datetime.now().isoformat(timespec="seconds"),
            "repo": "810_driver_test",
            "git_sha": _git_sha(),
            "project": project,
            "projects": [project],
        }
    else:
        projects = meta.get("projects")
        if not isinstance(projects, list):
            projects = []
        if project not in projects:
            projects.append(project)
        meta["projects"] = projects
        # 只有单模块会话记录“主工程”；全量会话保持 project=null（projects 列表已由
        # run_all 预填），否则第一条 case 的工程名会被误写成整批的主工程。
        if meta.get("kind") == KIND_MODULE and not meta.get("project"):
            meta["project"] = project
    try:
        (session / SESSION_META).write_text(
            json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    except OSError:
        pass


def allocate_session_dir(reports_root, date_str, kind=KIND_MODULE, *,
                         project=None, projects=None):
    """创建并返回下一个会话目录；module -> <root>/module/<date>-<idx>，
    full -> <root>/<date>-<idx>。同时写 session_meta.json（失败静默）。"""
    session = plan_session_dir(reports_root, date_str, kind)
    session.mkdir(parents=True, exist_ok=True)
    _write_session_meta(session, kind, project=project, projects=projects)
    return session


def case_dir_for(session_dir, project, case):
    """返回 <session>/<project>_<case>/ 并创建之；顺带登记 project 到 meta。"""
    d = Path(session_dir) / f"{project}_{case}"
    d.mkdir(parents=True, exist_ok=True)
    register_session_project(session_dir, project)
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


def _summary_title(session_dir):
    """summary 标题：module -> “单模块”，full（有 meta）-> “全量”，历史无 meta 原样。"""
    name = Path(session_dir).name
    meta = read_session_meta(session_dir)
    kind = meta.get("kind")
    if kind == KIND_MODULE:
        return f"# 810_driver_test 汇总（单模块 {name}）"
    if kind == KIND_FULL:
        return f"# 810_driver_test 汇总（全量 {name}）"
    if Path(session_dir).parent.name == MODULE_SUBDIR:
        return f"# 810_driver_test 汇总（单模块 {name}）"
    return f"# 810_driver_test 汇总（{name}）"


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
    lines = [_summary_title(session_dir), "",
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
