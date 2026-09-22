#!/usr/bin/env python3
"""失败归因报告骨架生成器（L2 处理层；纯离线、不碰板子）。

动机（用户裁定 2026-09-21）：每轮回归跑完后，**失败项的分析要固化成
`reports/<会话>/failures/<driver>_<case>.md`，每个失败 case 一个文件**。
但若每次都让 agent 从零读遍报告与串口日志，成本高且格式会漂。

分两段（与仓库既有的分层哲学一致：确定性的活不该每次让 LLM 重做）：

  1. **骨架（本模块）** —— 从 `summary.md` + 各 case 报告里**机械抽取**已经
     存在的信息（工程 / case / 执行标记 / 内存兜底 / 结果 / 失败项名 / 来源 /
     期望 / 实际 / 失败原因）。零判断、可离线重放、可单测。
  2. **增量（agent）** —— 只补两件事：报告里为 `-` 的**实测值**（去
     `serial_*.log` 挖）+ **归因段**。这些位置一律以 `⚠️` 标出，便于 grep。

用法::

    python common/failures_report.py --session reports/2026-09-21-1
    python common/failures_report.py --session reports/2026-09-21-1 --force

默认**不覆盖已存在的文件**（agent 可能已填过内容）；`--force` 才重写。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

#: 汇总表行：`| [<名>](<链接>) | PASS|FAIL | <计数> |`
SUMMARY_ROW_RE = re.compile(
    r"^\|\s*\[(?P<name>[^\]]+)\]\((?P<link>[^)]+)\)\s*\|"
    r"\s*(?P<verdict>PASS|FAIL)\s*\|\s*(?P<count>[^|]+?)\s*\|\s*$")

#: 报告头部字段：`- 工程: xxx`
HEADER_RE = re.compile(r"^-\s*(?P<key>[^:：]+)[:：]\s*(?P<val>.*)$")

#: 未给出/空值：报告里大量用 `-` 占位
EMPTY_VALS = {"-", "", "—", "N/A"}

MISSING = "⚠️ 待补（报告未给，需查 `serial_*.log`）"
TODO = "⚠️ **待 agent 填写**"

#: summary.md 末尾的指针行前缀（本模块独占维护：每次生成时**替换**同名行）
POINTER_PREFIX = "> 失败归因："


def _sync_summary_pointer(session: Path, n_fail: int) -> None:
    """在 `summary.md` 末尾维护一行指向 `failures/` 的指针（幂等）。

    为什么由本模块做：`failures/` 是每轮回归的固定产物（用户 2026-09-21 裁定），
    但**summary.md 若不指向它，读者只看报告不会知道下面还有一层归因**。
    这一步是纯机械的，不该靠人记。

    ⚠️ 只在**末尾追加/替换该前缀行**，绝不触碰 case 行（那是
    `common/report_dir.py::update_summary` 的写域）—— 两者的写入面不重叠。
    """
    p = session / "summary.md"
    try:
        lines = p.read_text(encoding="utf-8").splitlines()
        lines = [ln for ln in lines if not ln.startswith(POINTER_PREFIX)]
        while lines and not lines[-1].strip():
            lines.pop()
        if n_fail:
            lines += ["", f"{POINTER_PREFIX}见 [`failures/`](failures/)"
                          f"（{n_fail} 个 FAIL case，每个一个文件；"
                          "`⚠️` 标记处待补）"]
        p.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="")
    except Exception as exc:              # noqa: BLE001 —— 指针行绝不改变判定
        print(f"[WARN] 无法维护 summary.md 指针行（不影响判定）: {exc}",
              file=sys.stderr)


def parse_summary(session: Path):
    """返回 `[(name, link, verdict, count), ...]`。"""
    text = (session / "summary.md").read_text(encoding="utf-8")
    rows = []
    for ln in text.splitlines():
        m = SUMMARY_ROW_RE.match(ln)
        if m:
            rows.append((m["name"], m["link"], m["verdict"], m["count"]))
    return rows


def parse_case_report(md: Path):
    """解析一份 per-case 报告 → `(header_dict, [(cells,), ...])`。

    `cells` 是失败行的**原始单元格**（去掉首尾空串），列义由报告自己决定 ——
    本模块不做列名假设，避免报告格式演进时静默错位。
    """
    header, fail_cells = {}, []
    in_table = False
    for ln in md.read_text(encoding="utf-8").splitlines():
        s = ln.strip()
        hm = HEADER_RE.match(s)
        if hm and not in_table:
            header[hm["key"].strip()] = hm["val"].strip()
            continue
        if not s.startswith("|"):
            # 表格结束（空行/新标题）；但 header 只认表格之前的部分
            if in_table:
                in_table = False
            continue
        cells = [c.strip() for c in s.strip("|").split("|")]
        if all(set(c) <= set("-: ") for c in cells):     # 分隔行
            continue
        if "结果" in cells or "测试项" in cells:          # 表头行
            in_table = True
            continue
        if in_table and any("FAIL" in c for c in cells):
            fail_cells.append(cells)
    if not header.get("case"):
        header["case"] = md.parent.name
    return header, fail_cells


def out_name(name: str) -> str:
    """`dma_test` + `case_default` → `dma_test_default.md`（驱动名打头）。"""
    driver, _, case = name.partition("_case_")
    return f"{driver}_{case}.md" if case else f"{name}.md"


def render(name, link, verdict, count, session: Path) -> str:
    case_dir = session / Path(link).parent
    report_md = case_dir / Path(link).name
    lines = [f"# {out_name(name)[:-3]} 失败分析", ""]
    lines += ["> 骨架由 `common/failures_report.py` 生成；**`⚠️` 标记处需 agent 补**"
              "（实测值去 `serial_*.log` 挖，归因段自行判断）。", ""]

    if not report_md.is_file():
        # 无 per-case 报告（如 prime 阶段零输出）——只剩日志可查
        lines += ["| 项 | 值 |", "|---|---|",
                  f"| case | `{name}` |",
                  f"| 报告 | 无 per-case 报告（本 case 未产出任何测试项） |",
                  f"| 结果 | {verdict} |", f"| 计数 | {count.strip()} |", ""]
        lines += ["## 失败项明细", "",
                  f"| 测试项 | 结果 | 期望 | 实际 | 失败原因 | 证据 |",
                  "|---|---|---|---|---|---|",
                  f"| （无） | {verdict} | — | — | 本 case 未产出任何测试项 | "
                  f"`logs/{name}.log` |", ""]
        lines += ["## 归因", "", TODO + "：本 case 连测试项都没有 —— 先判"
                  "「失败发生在 build / load / prime / capture 哪一步」，"
                  "再判是环境还是用例。", ""]
        lines += ["## 备注", "", TODO, ""]
        return "\n".join(lines)

    header, fails = parse_case_report(report_md)
    rel = f"../{Path(link).parent}/{report_md.name}"
    lines += ["| 项 | 值 |", "|---|---|"]
    for k in ("工程", "case", "执行标记", "内存兜底"):
        lines.append(f"| {k} | {header.get(k, '（未给出）')} |")
    lines.append(f"| 报告 | [{report_md.name}]({rel}) |")
    lines.append(f"| 结果 | {header.get('结果', verdict + ' ' + count.strip())} |")
    lines.append("")

    lines += ["## 失败项明细", ""]
    if not fails:
        # 空表有两种成因，处置完全不同，**不要混为一谈**：
        #   0/0  ⇒ 固件根本没产出测试项（未跑起来），要查日志与内存兜底字段；
        #   非 0 ⇒ 报告有项却无 FAIL 行，与判定矛盾，属框架/判定问题。
        zero_items = count.strip() in ("0/0", "未知", "")
        why = ("本 case **未产出任何测试项**（固件未跑起来 / 采集未拿到）"
               if zero_items else
               "报告里有测试项却**无 FAIL 行** —— 与判定矛盾，须人工核对")
        lines += ["| 测试项 | 结果 | 期望 | 实际 | 失败原因 | 证据 |",
                  "|---|---|---|---|---|---|",
                  f"| （无失败项可列） | {verdict} | — | — | {why} | "
                  f"`{rel}` / `logs/{name}.log` |", ""]
        if zero_items:
            lines += ["> ⚠️ **优先看上面的「内存兜底」字段** —— 它若写「哨兵未置位 / "
                      "invalid」，说明固件没跑到 `tr_init`（挂在早期初始化），"
                      "这本身就是一条定位线索，别只盯着串口 0 行。", ""]
    else:
        lines += ["| 测试项 | 结果 | 来源 | 期望 | 实际 | 失败原因 | 证据 |",
                  "|---|---|---|---|---|---|---|"]
        for c in fails:
            # 报告列序：测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因
            c = (c + [""] * 6)[:6]
            item, res, src, exp, act, reason = c
            fix = lambda v: MISSING if v.strip() in EMPTY_VALS else v
            lines.append(f"| {item} | {res} | {src} | {fix(exp)} | {fix(act)} | "
                         f"{fix(reason)} | ⚠️ 待补 |")
        lines.append("")

    lines += ["## 归因", "", TODO + "：这些失败项指向什么 —— 驱动 / 用例 / 环境 / "
              "数据未采集无法判定。**无法判定就明写「无法判定」并列出还缺什么证据**。", ""]
    lines += ["## 备注", "", TODO + "（可选：与上次回归的差异、KNOWN_ISSUES 编号等）", ""]
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="失败归因报告骨架生成器")
    ap.add_argument("--session", required=True, help="报告会话目录，如 reports/2026-09-21-1")
    ap.add_argument("--out-dir", default="failures", help="输出子目录名（默认 failures）")
    ap.add_argument("--force", action="store_true", help="覆盖已存在的文件（默认跳过）")
    args = ap.parse_args(argv)

    session = Path(args.session)
    if not (session / "summary.md").is_file():
        print(f"[ERROR] 找不到 {session / 'summary.md'}", file=sys.stderr)
        return 2

    rows = parse_summary(session)
    if not rows:
        print("[ERROR] summary.md 里没解析到任何 case 行（格式变了？）", file=sys.stderr)
        return 2

    out_dir = session / args.out_dir
    out_dir.mkdir(exist_ok=True)
    made = skipped = 0
    for name, link, verdict, count in rows:
        if verdict != "FAIL":
            continue
        dst = out_dir / out_name(name)
        if dst.exists() and not args.force:
            print(f"[skip] {dst.name}（已存在；--force 可覆盖）")
            skipped += 1
            continue
        dst.write_text(render(name, link, verdict, count, session), encoding="utf-8")
        print(f"[make] {dst.name}")
        made += 1

    total_fail = sum(1 for r in rows if r[2] == "FAIL")
    _sync_summary_pointer(session, total_fail)
    print(f"\n共 {len(rows)} 个 case，FAIL {total_fail} 个；"
          f"生成 {made} 个骨架，跳过 {skipped} 个 → {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
