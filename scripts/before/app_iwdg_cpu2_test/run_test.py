#!/usr/bin/env python3
"""app_iwdg_cpu2_test 双核回归入口（cpu1 常驻监督 + cpu2 被测核）。

判定（**本项目专属**，见 tests/COVERAGE.md §「判定口径」）：
    case PASS = cpu2(CTRL) 有 ZT_BOOT + 至少 1 个测试项 + 全部项 PASS +
                PROJECT EXECUTION SUCCESSFUL
             且 cpu1(TGT) 有 ZT_BOOT + 全部项 PASS + PROJECT EXECUTION SUCCESSFUL

    ⚠ 偏离 SPEC_REFERENCE §6.2.1 的通用双核判据（"同名测试项两核各自 PASS"）：
      本工程的被测对象 app_iwdg 只存在于 cpu2 域，两核测试项**结构上不可能同名**
      （cpu1 侧只有结构守卫项）。该偏离与 i3c_dualcore_test 的"双核例外"
      （§6.2.1）同源，属**待父会话确认**项。

⚠ cpu2 hold（0x4002200c / SYSC_SEC_CPU.APP_CPU_SRST）：单核路径需写
  `set {int}0x4002200c = 2` 按住 cpu2；**双核路径绝对不加**（cpu2 就是被测核）。
  加载统一走 case 的 test.sh -r → test_common.sh 的 load_and_run。
  本文件只在注释里出现该地址，不做写入。

case_reset（破坏性）不在本脚本范围内：走 case_reset/run_reset.sh。

归档：reports/<YYYY-MM-DD>-<idx>/<project>_<case>/<driver>_test_report.{md,json}
（公共 common/report_dir.py，SPEC_REFERENCE §6.4）。
"""

import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

import serial  # noqa: E402

SAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
from ztest_report import parse_ztest  # noqa: E402
from serial_archive import archive_serial  # noqa: E402
import process  # noqa: E402  —— L2 处理层（归档 → 判定 → 报告）
import acquire  # noqa: E402  —— L1 采集层（构建/烧录/采集/落归档）
from report_dir import allocate_session_dir, case_dir_for, update_summary  # noqa: E402

# ---- F1/F2/F3 判定框架收敛点（common/judge.py）----
from judge import (ZERO_EVIDENCE_NOTE, declared_names_from_dumper,  # noqa: E402
                   has_evidence, truncation_evidence)
from judge import (REPORT_TABLE_HEADER, REPORT_TABLE_SEP,  # noqa: E402
                   clip_cell, coverage_expected, format_report_header,
                   md_table_row, result_text, source_label)

# 已声明测试项（判"证据是否完整"的唯一依据，F1 红线）：
#   CTRL(cpu2) = 同工程 scripts/dump_results.py 的 TEST_NAMES_BY_CASE[case]（唯一权威表）
#   TGT(cpu1)  = 常驻监督核的结构守卫套件 app_iwdg_domain（链接序，SORT_BY_NAME）
DECLARED_DUMPER = Path(__file__).resolve().parent / "scripts" / "dump_results.py"
TGT_TEST_NAMES = [
    "test_cpu1_does_not_own_app_iwdg",
    "test_cpu1_reset_reason_not_app_iwdt",
]
CORE_PREFIX = {"CTRL": "cpu2::", "TGT": "cpu1::"}

DRIVER_NAME = "app_iwdg_cpu2_test"
REPORT_BASE = f"{DRIVER_NAME}_report"

# 角色 banner 取自本工程固件实际打印（见 tests/scripts/dbg_run.py 说明）
TGT_BANNERS = ("CPU1 APP_IWDG",)
CTRL_BANNERS = ("CPU2 APP_IWDG", "app_iwdg (cpu2)")

# 非 ztest 的破坏性 case：不在本脚本处理范围
DESTRUCTIVE_CASES = ("case_reset",)

ZTEST_CASES = ("case_non_destructive", "case_quick_enable", "case_quick_invalid")

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

BASH = ("C:/Program Files/Git/bin/bash.exe"
        if Path("C:/Program Files/Git/bin/bash.exe").exists() else "bash")


def log(msg):
    print(msg, flush=True)


def evaluate(case_name, ctrl_lines, tgt_lines):
    ctrl = parse_ztest(ctrl_lines, ctrl_banners=CTRL_BANNERS, tgt_banners=TGT_BANNERS)
    tgt = parse_ztest(tgt_lines, ctrl_banners=CTRL_BANNERS, tgt_banners=TGT_BANNERS)

    items = []
    for name, v in sorted(ctrl["tests"].items()):
        items.append({"name": f"cpu2::{name}", "result": v.get("result"),
                      "reason": v.get("reason", "")})
    for name, v in sorted(tgt["tests"].items()):
        items.append({"name": f"cpu1::{name}", "result": v.get("result"),
                      "reason": v.get("reason", "")})

    ctrl_items = [i for i in items if i["name"].startswith("cpu2::")]
    tgt_items = [i for i in items if i["name"].startswith("cpu1::")]

    ctrl_ok = (ctrl["boot_seen"] and ctrl["execution"] == "SUCCESSFUL"
               and len(ctrl_items) >= 1
               and all(i["result"] == "PASS" for i in ctrl_items))
    # 零证据守卫（2026-09-18）：`all([]) == True` —— cpu1 一个测试项都没捕到时
    # 不得判 tgt_ok（判据见 common/judge.py::has_evidence）。
    tgt_ok = (tgt["boot_seen"] and tgt["execution"] == "SUCCESSFUL"
              and has_evidence(tgt_items)
              and all(i["result"] == "PASS" for i in tgt_items))

    if not ctrl["boot_seen"]:
        items.append({"name": "cpu2::boot", "result": "FAIL",
                      "reason": "未收到 ZT_BOOT（load 后无日志 / 固件未跑）"})
    elif not has_evidence(ctrl_items):
        items.append({"name": "cpu2::evidence", "result": "FAIL",
                      "reason": ZERO_EVIDENCE_NOTE})
    if not tgt["boot_seen"]:
        items.append({"name": "cpu1::boot", "result": "FAIL",
                      "reason": "未收到 ZT_BOOT（load 后无日志 / 固件未跑）"})
    elif not has_evidence(tgt_items):
        items.append({"name": "cpu1::evidence", "result": "FAIL",
                      "reason": ZERO_EVIDENCE_NOTE})

    # F1（2026-09-18）：case_timeout 到点时 ztest 的 PROJECT EXECUTION 常还没打出来
    # （2026-09-18-1/5 实测：cpu2 本体 ~2.7s 就跑完、全项 PASS，却因窗口到点判 FAIL）。
    # **窗口到点本身不是失败** —— 只有"证据不全"才是。截断接受 = 该核 ZT_BOOT 已见 +
    # 已声明测试项全部有确定结果（无 MISSING）+ 全 PASS + 仅缺收尾标记；任一已声明项
    # 无证据 ⇒ 维持 FAIL（红线）。判定谓词收敛自 common/judge.py。
    ctrl_declared = declared_names_from_dumper(DECLARED_DUMPER, case_name)
    core_items = {"CTRL": ctrl_items, "TGT": tgt_items}
    ctrl_trunc = tgt_trunc = False
    for _core, _declared, _boot, _exec in (
            ("CTRL", ctrl_declared, ctrl["boot_seen"], ctrl["execution"]),
            ("TGT", TGT_TEST_NAMES, tgt["boot_seen"], tgt["execution"])):
        _pfx = CORE_PREFIX[_core]
        _status = {i["name"][len(_pfx):]: i["result"] for i in core_items[_core]}
        _ev_ok, _missing = truncation_evidence(_exec, _declared, _status)
        _trunc = (_ev_ok and _boot and bool(core_items[_core])
                  and all(i["result"] == "PASS" for i in core_items[_core]))
        if _core == "CTRL":
            ctrl_trunc = _trunc
        else:
            tgt_trunc = _trunc
    if ctrl_trunc:
        ctrl_ok = True
    if tgt_trunc:
        tgt_ok = True

    return {
        "case": case_name,
        "result": "PASS" if (ctrl_ok and tgt_ok) else "FAIL",
        "ctrl_execution": ("TRUNCATED_AFTER_ALL_ITEMS" if ctrl_trunc
                           else (ctrl["execution"] or "UNKNOWN")),
        "tgt_execution": ("TRUNCATED_AFTER_ALL_ITEMS" if tgt_trunc
                          else (tgt["execution"] or "UNKNOWN")),
        "items": items,
        "infos": {**{f"cpu2::{k}": v for k, v in ctrl["infos"].items()},
                  **{f"cpu1::{k}": v for k, v in tgt["infos"].items()}},
    }


def print_report(results):
    print("\n--- app_iwdg_cpu2_test 双核测试结果 ---")
    for r in results:
        items = r.get("items", [])
        passed = sum(1 for i in items if i.get("result") == "PASS")
        print(f"## {r['case']}: {r['result']} ({passed}/{len(items)}) "
              f"[CTRL(cpu2)={r.get('ctrl_execution')}, TGT(cpu1)={r.get('tgt_execution')}]")
        for it in items:
            extra = f"  reason: {it['reason']}" if it.get("reason") else ""
            print(f"  {it['name']:<56} {it.get('result')}{extra}")


def write_markdown_report(cdir, result):
    """SPEC_REFERENCE §6.4.2：与 json 同源（items[] 渲染），覆盖写。

    列序 / 表头 / 单元格转义统一走 common/judge.py（REPORT_TABLE_HEADER +
    md_table_row），与全家族逐字一致；本工程无内存兜底（来源恒为 token）。
    """
    base = cdir / f"{REPORT_BASE}.md"
    items = result.get("items", [])
    passed = sum(1 for it in items if it.get("result") == "PASS")
    total = len(items)
    status = result.get("result") or "FAIL"
    _core_exec = ", ".join(f"{k.upper()}={result[k]}"
                           for k in ("ctrl_execution", "tgt_execution") if result.get(k))
    lines = [
        f"# {DRIVER_NAME} 测试报告",
        "",
        *format_report_header(
            DRIVER_NAME, result.get("case", ""),
            "lsqsh_evb@2os（cpu1 target / cpu2 controller）",
            date=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            env="无外部接线（IWDG 为片内外设，见 810_driver_test/README.md 接线表）",
            execution=result.get("execution", "UNKNOWN"),
            mem_line="内存兜底: 不适用（本工程无内存兜底）",
            result=f"{status} ({passed}/{total})"),
        *([f"- 核执行标记: {_core_exec}"] if _core_exec else []),
        "",
        "## 测试项结果",
        "",
        REPORT_TABLE_HEADER,
        REPORT_TABLE_SEP,
    ]
    infos = result.get("infos", {})
    for it in items:
        name = it.get("name", "?")
        kv = infos.get(name) or infos.get(name.replace("cpu2::", "").replace("cpu1::", ""))
        actual = ", ".join(f"{k}={v}" for k, v in kv.items()) if kv else "-"
        lines.append(md_table_row([
            name, result_text(it.get("result", "?")), "token",
            "-", actual, it.get("reason", "")]))
    lines.append("")
    lines.append(f"## 汇总\n\nOVERALL: {status}\n")
    base.write_text("\n".join(lines), encoding="utf-8")
    return base


# ---- L2 处理层规格（common/process.py）：只声明**本工程特有**的接入点 ----
# 判定族特有（本项目专属口径，见文件头）：两核各自 boot + 全部项 PASS + 收尾标记，
# 且两核测试项**结构上不可能同名**（cpu1 侧只有结构守卫项）。渲染= write_markdown_report
# / print_report；落盘 / summary / rc 走 process.py 缺省（与本文件既有写法逐字一致）。
# 契约：每工程暴露 `l2_spec(facts=None)`；`facts` = **未归档的采集事实**
# （此处 `timed_out` / `case_timeout`：窗口到点只是报告注释项，不参与判定）。
def l2_spec(facts=None):
    def _judge(ci):
        result = evaluate(ci.case, ci.lines["CTRL"], ci.lines["TGT"])
        if ci.facts.get("timed_out"):
            # F1（2026-09-18）：窗口到点只是"为什么可能缺收尾标记"的**注释**，不再是
            # 判 FAIL 的理由 —— 判 FAIL 的唯一依据是"有测试项没有证据"（见 evaluate）。
            result["items"].append({
                "name": "run::timeout", "result": "NOTE",
                "reason": f"case_timeout={ci.facts.get('case_timeout')}s 到点截断（非失败）"})
        return result

    return process.ProcessSpec(
        project=DRIVER_NAME, driver=DRIVER_NAME, report_base=REPORT_BASE,
        judge=_judge,
        render_md=lambda jl, cdir: write_markdown_report(cdir, jl[0]),
        console=lambda jl: print_report(jl),
    )


def main():
    ap = argparse.ArgumentParser(description="app_iwdg_cpu2_test dual-core regression runner")
    ap.add_argument("--case", default="case_non_destructive")
    ap.add_argument("--ctrl-uart", default=None, help="cpu2 控制台（uart5）")
    ap.add_argument("--tgt-uart", default=None, help="cpu1 控制台（uart1）")
    ap.add_argument("--session-dir", default=None)
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--no-archive", action="store_true")
    # case_timeout = 单轮（load + run）的**采集窗口**总预算，不是"固件跑完需要多久"。
    # 双核路径每次 run_pass 都要走 test.sh -r → load_and_run 的两个独立 gdb 会话
    # （A: load cpu1 ELF；B: restore cpu2 bin + continue），gdb B 带 continue 且
    # RUN_TIMEOUT=0 ⇒ 进程**永不自行退出**，窗口到点由本脚本 taskkill ⇒ 窗口多长就
    # 采多长。2026-09-18-1 实测：60s 窗口在最后一项之后、ztest 收尾标记之前到点，
    # 三个 case 全部命中 run::timeout（而本体用例其实已在 cpu2 启动后 ~2.7s 跑完，
    # cpu2 末条时间戳 00:00:02.702，且已打印 "TESTSUITE app_iwdg succeeded"）。
    # 取值依据 = 同类双核工程 mbox_test 的 120s（同款 test.sh -r + gdb continue 链路），
    # **只调本工程默认值，不动公共层**（其余单核工程走 --timeout 语义，不受影响）。
    ap.add_argument("--case-timeout", type=int, default=120)
    args = ap.parse_args()
    ctrl_uart = args.ctrl_uart or os.environ.get("I3C_CTRL_UART")
    tgt_uart = args.tgt_uart or os.environ.get("I3C_TGT_UART")
    if not ctrl_uart or not tgt_uart:
        log("ERROR: --ctrl-uart/--tgt-uart required（双核脚本不支持自动发现，"
            "SPEC_REFERENCE §6.4.5）")
        return 1

    log(f"Controller UART (cpu2): {ctrl_uart}")
    log(f"Target UART (cpu1):     {tgt_uart}")

    session = None   # 延迟分配：原始串口归档与报告共用同一会话目录

    # L1 采集（common/acquire.py）：构建 → prime 轮（丢弃）→ capture 轮 → 分核归一 → 落归档
    acq = acquire.iwdg(acquire.IwdgSpec(
        driver=DRIVER_NAME, root=SAMPLE_ROOT, tests_dir=SAMPLE_ROOT / "tests", case=args.case,
        board="lsqsh_evb@2os", ctrl_uart=ctrl_uart, tgt_uart=tgt_uart,
        case_timeout=args.case_timeout, skip_build=args.skip_build,
        archive=not args.no_archive, script=__file__,
        # 单出口（SPEC_REFERENCE §6.9.3/§6.9.4）：cpu1 是 ztest 型从核，逐项
        # 输出经 `tc_util_user_override.h` 换到 RLOG；cpu2 转发时加
        # `APP_IWDG_TEST:TGT:` 核信封 ⇒ L1 据此分拣回 serial_TGT.log。
        # 信封是中继侧新加的**壳** ⇒ strip=True，使归档与从核本来自己打逐行等价。
        relay_prefixes=("APP_IWDG_TEST",),
        relay_strip=True,
        session=lambda: session or (
            Path(args.session_dir) if args.session_dir
            else allocate_session_dir(REPORTS_ROOT,
                                      datetime.now().strftime("%Y-%m-%d")))))
    if not acq.ok:
        return 1
    session = acq.session

    # L1→L2 边界（L0 归档形状）：两核原始行**分核**归一；`timed_out` 是未归档的
    # 采集事实（facts）⇒ 只作报告注释项，不参与判定。
    return process.run(l2_spec(), case=args.case, lines=acq.lines,
                       session=session, write=not args.no_archive,
                       facts=acq.facts).rc


if __name__ == "__main__":
    sys.exit(main())
