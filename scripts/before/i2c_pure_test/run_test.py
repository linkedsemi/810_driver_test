#!/usr/bin/env python3
"""I2C pure-test dual-core one-button regression agent (ztest-native).

Runs all cases under 810_driver_test/projects/i2c_pure_test/tests/case_*/
on the connected controller/target UARTs, parses the ztest-native output
(common/ztest_report.parse_ztest) and produces an archived Markdown + JSON
report under 810_driver_test/reports/<YYYY-MM-DD>-<idx>/ (SPEC_REFERENCE.md §6.4).

双核工程 ztest 原生化版：每核固件跑自己的 ztest 套件，逐项结果 = 同名
测试项双核 PASS；case 判定 = 双核 execution 均 "SUCCESSFUL" + 全部项双核
PASS。完成判定（提前杀 gdb 的静默判据）= 本固件 ZT_BOOT 已见的端口上出现
PROJECT EXECUTION（他方固件无 ZT_BOOT，其标记一律忽略，KNOWN_ISSUES.md #30）。

Archiving behavior (SPEC_REFERENCE.md §6.4):
    - Report written to reports/<date>-<idx>/<driver>_<case>/i2c_pure_test_report.{md,json}
    - Session summary.md index updated (per-case row replaced, §12.4)
    - ZT_INFO (§13.4) parsed into the "actual" column of the report
    - Missing UART items backfilled from memory via dump_results.py --json
      (§12.3: token wins, memory fills gaps, both missing -> FAIL)

Environment:
    I3C_CTRL_UART          Controller UART port (e.g. COM15)
    I3C_TGT_UART           Target UART port (e.g. COM16)
    GDB_SERVER_HOST        GDB server host (default: localhost)
    GDB_SERVER_PORT        GDB server port (default: 1025)
    I3C_CASE_TIMEOUT       Seconds to wait for one case to finish (default: 60)

Usage:
    python run_test.py
    python run_test.py --case case_pure_i2c_400k
    python run_test.py --case ... --session-dir reports/2026-08-29-3
"""

import argparse
import json
import os
import re
import shutil
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import subprocess
import tempfile
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple

try:
    import serial
    import serial.tools.list_ports
except ImportError as exc:
    print(f"ERROR: pyserial is required: {exc}", file=sys.stderr)
    print("Install with: pip install pyserial", file=sys.stderr)
    sys.exit(1)


# tests/run_test.py -> parents[1] = i2c_pure_test 工程根
SAMPLE_ROOT = Path(__file__).resolve().parents[1]
# 归档根：810_driver_test/reports/（会话目录 <YYYY-MM-DD>-<idx> 由 report_dir 分配）
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

# 公共解析/归档层（Task 1/2 产物，路径同 trng 样板；REPORTS_ROOT / "common"
# 是计划笔误——公共模块在 810_driver_test/common/）
sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
from ztest_report import parse_ztest   # noqa: E402
from serial_archive import archive_serial  # noqa: E402
import process  # noqa: E402  —— L2 处理层（归档 → 判定 → 报告）
import acquire  # noqa: E402  —— L1 采集层（构建/烧录/采集/落归档）
from report_dir import (allocate_session_dir, case_dir_for,  # noqa: E402
                        update_summary)

# ---- F1/F2/F3 判定框架收敛点（common/judge.py）----
from judge import (MEM_ONLY_NOTE, exec_conflict_note, exec_label, has_evidence,  # noqa: E402
                   mem_only_evidence, mem_state_line, mem_state_of_payload,
                   truncation_evidence)
from judge import (REPORT_TABLE_HEADER, REPORT_TABLE_SEP,  # noqa: E402
                   clip_cell, coverage_expected, format_report_header,
                   md_table_row, result_text, source_label)

DRIVER_NAME = "i2c_pure_test"          # §12.4 summary 行 `<driver>_test` 前缀
REPORT_BASE = f"{DRIVER_NAME}_report"  # i2c_pure_test_report.{md,json}（避免 _test_test 重复）

CTRL_BANNERS = ("CPU2 I2C_PURE_CTRL", "i2c_pure_master_test")
TGT_BANNERS = ("CPU1 I2C_PURE_TGT", "i2c_pure_slave_test")

# 测试项顺序必须与 ztest_unit_test 段链接序一致（链接器 SORT_BY_NAME → 测试名
# 字典序，§10.27）。当前仅一项，顺序由 nm <elf> | grep ztest_unit_test 核实：
#   z_ztest_unit_test__i2c_pure_ctrl__i2c_wave / z_ztest_unit_test__i2c_pure_tgt__i2c_wave
TEST_NAMES = ["i2c_wave"]


GDB = shutil.which("riscv64-unknown-elf-gdb") or "riscv64-unknown-elf-gdb"
DEBUGSERVER_EXE = r"C:\uaer_app\debug_server\bin\DebugServerConsole.exe"


TESTS_DIR = SAMPLE_ROOT / "tests"


def parse_expected_from_coverage():
    """§6.5.2 预期数据（COVERAGE.md 表「预期数据」列）—— 解析收敛在 common/judge.py。

    只认列头含 `预期数据` 的表；转义竖线与裸竖线都不再串列（旧实现用
    `[^|]*` 解析，COVERAGE 里的转义竖线被截成半个单元格，见 §1.4）。"""
    return coverage_expected(TESTS_DIR / "COVERAGE.md")


def find_cases(filter_name: Optional[str] = None) -> List[Path]:
    cases = sorted(p for p in TESTS_DIR.iterdir() if p.is_dir() and p.name.startswith("case_"))
    if filter_name:
        cases = [p for p in cases if p.name == filter_name]
    return cases


def evaluate_case(case_name: str, ctrl_lines: List[str], tgt_lines: List[str],
                  test_names: List[str], mem_data: Optional[Dict]) -> Dict:
    """双核判定（ztest 原生）：两路 lines 各 parse_ztest 后合并。

    - 测试项判定 = 同名项双核 PASS（CTRL+TGT）；单核缺失走 §12.3 内存兜底
    - case 判定 = 双核 execution 均 "SUCCESSFUL" + 全部项双核 PASS
    - ctrl_done/tgt_done 字段 = 两核 execution 值（ztest 无 DONE token，
      PROJECT EXECUTION 是套件完成标记）

    返回结构保持双核样板形状：{name, result, ctrl_done, tgt_done,
    mem_unavailable, tests:[{name,ctrl,tgt,src,info,reason,result}]}。
    """
    ctrl_p = parse_ztest(ctrl_lines, ctrl_banners=CTRL_BANNERS,
                         tgt_banners=TGT_BANNERS)
    tgt_p = parse_ztest(tgt_lines, ctrl_banners=CTRL_BANNERS,
                        tgt_banners=TGT_BANNERS)
    per_core = {"CTRL": ctrl_p, "TGT": tgt_p}

    tests = []
    case_pass = True
    ev_srcs = []   # 每项×每核的证据来源（R7「批量必须靠 token」判据用）
    for name in test_names:
        statuses: Dict[str, str] = {}
        srcs: Dict[str, str] = {}
        for core in ("CTRL", "TGT"):
            t = per_core[core]["tests"].get(name)
            st = t["result"] if t else "MISSING"
            src = "token"
            # §12.3 内存兜底：token 缺失的项用内存值补（来源=mem）
            if st == "MISSING" and mem_data is not None and core in mem_data:
                if name in mem_data[core]:
                    st = mem_data[core][name]
                    src = "mem"
            if st == "MISSING":
                src = "missing"
            statuses[core] = st
            srcs[core] = src
            ev_srcs.append(src)
        test_pass = statuses["CTRL"] == "PASS" and statuses["TGT"] == "PASS"
        if not test_pass:
            case_pass = False

        # INFO 数值拼接（实际列）："CTRL:k=v,k=v; TGT:..."，无 INFO 留空
        info_parts = []
        for core in ("CTRL", "TGT"):
            kv = ", ".join(f"{k}={v}"
                           for k, v in sorted(per_core[core]["infos"].get(name, {}).items()))
            if kv:
                info_parts.append(f"{core}:{kv}")
        # reason 合并：双核 token reason + 内存 reason（§13.5 可归因格式）
        reason_parts = []
        for core in ("CTRL", "TGT"):
            t = per_core[core]["tests"].get(name)
            if t and t["reason"]:
                reason_parts.append(f"{core}:{t['reason']}")
        if mem_data is not None:
            mr = mem_data.get("reasons", {}).get(name)
            if mr:
                reason_parts.append(f"mem:{mr}")

        # 来源：任一核用内存兜底 → mem；任一核缺失 → missing；否则 token
        if "missing" in srcs.values():
            src = "missing"
        elif "mem" in srcs.values():
            src = "mem"
        else:
            src = "token"

        tests.append({
            "name": name,
            "ctrl": statuses["CTRL"],
            "tgt": statuses["TGT"],
            "src": src,
            "info": "; ".join(info_parts),
            "reason": "; ".join(reason_parts),
            "result": "PASS" if test_pass else "FAIL",
        })

    ctrl_done = ctrl_p["execution"]
    tgt_done = tgt_p["execution"]
    # 零证据守卫（2026-09-18；同日按裁定**收紧一档**）：
    #   ① tests 为空 ⇒ 一个测试项都没有 ⇒ 不得判 PASS。
    #   ② **某一核整体没有确定结果**（该核全部项既非 PASS 也非 FAIL —— 串口未捕到
    #      **且** 内存兜底也没给出值）⇒ 不得因"另一核完好"而判 PASS。
    #      理由：那一核根本没跑 / 跑崩了且兜底也读不出，两者都不是可容忍的丢行；
    #      丢行本该由 §12.3 内存兜底补回（兜底给出确定结果 ⇒ 不算零证据，仍可 PASS）。
    # ★ 边界：**只堵整核零证据**；部分项缺失仍走 incomplete 容忍（原设计不变）。
    # ★ 与"测坏了"（显式 FAIL token）**分开表述**：这是"没测到"，理由串不混。
    _gaps = []
    for _core in ("CTRL", "TGT"):
        _sts = [t[_core.lower()] for t in tests]
        if _sts and not any(s in ("PASS", "FAIL") for s in _sts):
            _gaps.append(_core)
    _gap_note = ""
    if _gaps:
        _gap_note = ("证据缺失（非驱动失败）：" + "、".join(
            f"{c} 核全部 {len(tests)} 项无确定结果（串口未捕到 + 内存兜底无结果）"
            for c in _gaps) + " ⇒ 不得因另一核完好判 PASS")
    if (not has_evidence(tests) or _gaps or ctrl_done != "SUCCESSFUL" or tgt_done != "SUCCESSFUL"):
        case_pass = False
    # F1/F2（2026-09-18）：见 common/judge.py。
    # F1：收尾标记未到时，只要**全部已声明测试项在两核都有确定结果**（无 MISSING）
    #     且全 PASS ⇒ 判 PASS 并标注 TRUNCATED_AFTER_ALL_ITEMS；任一已声明项在
    #     任一核无证据 ⇒ 维持 FAIL（红线）。
    # F2：mem_state 三态（ok / invalid / no_sentinel_symbol）进报告，读者不用翻日志。
    _status = {}
    for _t in tests:
        if _t["ctrl"] == "MISSING" or _t["tgt"] == "MISSING":
            _status[_t["name"]] = "MISSING"
        elif _t["ctrl"] == "PASS" and _t["tgt"] == "PASS":
            _status[_t["name"]] = "PASS"
        else:
            _status[_t["name"]] = _t["result"]
    _trunc_ok, _missing = truncation_evidence(ctrl_done, test_names, _status)
    truncated = False
    if (not case_pass and _trunc_ok and tgt_done != "FAILED" and tests
            and not _gaps and all(_t["result"] == "PASS" for _t in tests)):
        case_pass = True
        truncated = True
    _mem_state, _mem_note = mem_state_of_payload(mem_data)
    # R6（2026-09-19）：相反收尾标记并存 ⇒ parse_ztest 已 fail-closed 归成 FAILED；
    # 这里把"矛盾"本身显式写进报告（读者要能一眼看出是矛盾，而不是笼统 FAIL）。
    _conf_seen = sorted(set(ctrl_p.get("exec_seen", [])) | set(tgt_p.get("exec_seen", [])))
    _conf = bool(ctrl_p.get("exec_conflict") or tgt_p.get("exec_conflict"))
    _conf_note = exec_conflict_note(_conf_seen) if _conf else ""

    # R7（2026-09-20）批量必须靠 token：**全项**证据都来自 mem 兜底 ⇒ 不得判 PASS。
    # 判据与理由串的唯一来源 = common/judge.py（mem_only_evidence / MEM_ONLY_NOTE）；
    # 理由串挂在 mem_note 上 ⇒ 控制台 / .md / .json 三处同源可见。
    if mem_only_evidence(ev_srcs):
        case_pass = False
        _mem_note = ((_mem_note + "；") if _mem_note else "") + MEM_ONLY_NOTE

    return {
        "name": case_name,
        "result": "PASS" if case_pass else "FAIL",
        "ctrl_done": exec_label(ctrl_done, truncated,
                                 conflict=bool(ctrl_p.get("exec_conflict"))),
        "tgt_done_conflict": bool(tgt_p.get("exec_conflict")),
        "exec_conflict": _conf,
        "exec_conflict_note": _conf_note,
        "tgt_done": tgt_done,
        "mem_unavailable": mem_data is None,
        "mem_state": _mem_state,
        "zero_evidence": not has_evidence(tests),
        "evidence_gap": _gaps,
        "evidence_note": _gap_note,
        "mem_note": _mem_note,
        "tests": tests,
    }


def print_report(results: List[Dict]) -> None:
    print("\n" + "=" * 80)
    print("I2C Dual-Core Regression Report")
    print("=" * 80)

    overall = "PASS"
    for case in results:
        print(f"\n## {case['name']}: {case['result']}")
        if case["mem_unavailable"]:
            print("  (mem-unavailable: 内存兜底未生效，MISSING 项按 FAIL 判定)")
        print("  " + mem_state_line(case.get("mem_state", ""),
                                    case.get("mem_note", "")))
        if case.get("evidence_gap"):
            print("  " + case.get("evidence_note", ""))
        if case.get("exec_conflict"):
            print("  " + case.get("exec_conflict_note", ""))
        print(f"| Test | Controller | Target | Result | Source | INFO(actual) | Reason |")
        print(f"|------|------------|--------|--------|--------|--------------|--------|")
        for t in case["tests"]:
            print(f"| {t['name']} | {t['ctrl']} | {t['tgt']} | {t['result']} "
                  f"| {t['src']} | {t['info']} | {t['reason']} |")
        print(f"EXECUTION: CTRL={case['ctrl_done']}, TGT={case['tgt_done']}")
        if case["result"] != "PASS":
            overall = "FAIL"

    print("\n" + "=" * 80)
    print(f"OVERALL: {overall}")
    print("=" * 80)


def write_markdown_report(results: List[Dict], expected: Dict[str, str],
                          report_dir: Path, driver_name: str) -> None:
    """§6.4.2 报告：列序 / 表头 / 单元格转义统一走 common/judge.py。

    与全家族逐字一致（REPORT_TABLE_HEADER）；「期望」列超长截断（完整值见
    tests/COVERAGE.md）。多 case 时表头字段取聚合值，逐 case 明细见下方小节。
    """
    now = datetime.now()
    all_tests = [t for c in results for t in c["tests"]]
    passed = sum(1 for t in all_tests if t["result"] == "PASS")
    failed = sum(1 for t in all_tests if t["result"] == "FAIL")
    overall = "PASS" if all(r["result"] == "PASS" for r in results) else "FAIL"
    single = len(results) == 1
    names = "、".join(c["name"] for c in results) or "-"
    if single:
        exec_txt = (f"CTRL={results[0].get('ctrl_done')}, "
                    f"TGT={results[0].get('tgt_done')}")
        mem_txt = mem_state_line(results[0].get("mem_state", ""),
                                 results[0].get("mem_note", ""))
    else:
        exec_txt = "; ".join(
            f"{c['name']}: CTRL={c.get('ctrl_done')}, TGT={c.get('tgt_done')}"
            for c in results)
        mem_txt = "; ".join(
            f"{c['name']}: " + mem_state_line(c.get("mem_state", ""),
                                              c.get("mem_note", ""))
            for c in results)

    lines = [
        f"# {driver_name} 测试报告",
        "",
        *format_report_header(
            driver_name, names, "lsqsh_evb@2os",
            date=f"{now:%Y-%m-%d %H:%M:%S}",
            env="见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在"
                " COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）",
            execution=exec_txt, mem_line=mem_txt,
            result=f"{overall} ({passed}/{len(all_tests)}"
                   + (f"；失败 {failed}" if failed else "") + ")"),
        "",
    ]
    for case in results:
        lines.append(f"## {case['name']} ({case['result']})")
        lines.append("")
        if case.get("mem_unavailable"):
            lines.append("> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定")
        if case.get("zero_evidence") or not case.get("tests"):
            lines.append("> 零证据: 未捕获到任何测试项 ⇒ 不得判 PASS")
        if case.get("evidence_gap"):
            lines.append("> " + case.get("evidence_note", ""))
        if case.get("exec_conflict"):
            lines.append("> " + case.get("exec_conflict_note", ""))
        lines.append("")
        lines.append(REPORT_TABLE_HEADER)
        lines.append(REPORT_TABLE_SEP)
        for t in case.get("tests", []):
            lines.append(md_table_row([
                t["name"], result_text(t["result"]), source_label(t.get("src")),
                clip_cell(expected.get(t["name"], "") or "-"),
                t.get("info") or "-", t.get("reason", "")]))
        lines.append("")
    lines.append(f"## 汇总\n\nOVERALL: {overall}\n")

    path = report_dir / f"{REPORT_BASE}.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(f"\nMarkdown report written to: {path}")


def write_json_report(results: List[Dict], path: Path) -> None:
    overall = "PASS" if all(r["result"] == "PASS" for r in results) else "FAIL"
    report = {
        "timestamp": datetime.now().isoformat(),
        "board": "lsqsh_evb@2os",
        "overall": overall,
        "cases": results,
    }
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"JSON report written to: {path}")


def list_serial_ports() -> List[str]:
    return [p.device for p in serial.tools.list_ports.comports()]


# ---- L2 处理层规格（common/process.py）：只声明**本工程特有**的接入点 ----
# 本工程判定族特有（D3）：双核同名项 + CTRL 收尾标记 + TGT 结构性无收尾标记的例外
# 规则、mem 兜底、R6 矛盾标记、R7 token 守卫 —— 全在 evaluate_case 里，原样保留。
# 归档按核分文件（serial_CTRL.log / serial_TGT.log）⇒ L2 直接用两个核的行。
# **聚合形态**：一次跑多个 case，但只写**一份**报告（results 列表整体交给渲染）。
# 契约：每工程暴露 l2_spec(facts=None, test_names=None)；facts = 未归档采集事实。
def l2_spec(facts=None, test_names=None):
    test_names = test_names or TEST_NAMES
    expected = parse_expected_from_coverage()

    def _judge(ci):
        return evaluate_case(ci.case, ci.lines.get("CTRL", []),
                             ci.lines.get("TGT", []), test_names, ci.mem)

    def _render_md(jl, cdir):
        write_markdown_report(jl, expected, cdir, DRIVER_NAME)
        return cdir / f"{REPORT_BASE}.md"

    def _render_json(jl, cdir):
        write_json_report(jl, cdir / f"{REPORT_BASE}.json")
        return cdir / f"{REPORT_BASE}.json"

    return process.ProcessSpec(
        project=DRIVER_NAME, driver=DRIVER_NAME, report_base=REPORT_BASE,
        judge=_judge, render_md=_render_md, render_json=_render_json,
        console=lambda jl: print_report(jl),
        status=lambda j: j["result"],
        summary=lambda jl: (
            "PASS" if all(x["result"] == "PASS" for x in jl) else "FAIL",
            sum(1 for c in jl for t in c["tests"] if t["result"] == "PASS"),
            sum(1 for c in jl for t in c["tests"] if t["result"] != "NOT_RUN")),
        log_archived=lambda jp, mp: print(f"[run] archived -> {mp}"),
    )


def main() -> int:
    # 行缓冲输出：批量回归跑 ~10min 级，块缓冲会让 [BUILD]/[RUN] 进度
    # 全程不可见（误判为"卡死"）
    try:
        sys.stdout.reconfigure(line_buffering=True)
        sys.stderr.reconfigure(line_buffering=True)
    except Exception:
        pass

    parser = argparse.ArgumentParser(description="I2C dual-core regression agent (ztest-native)")
    parser.add_argument("--case", help="Run a single case (default: all case_* dirs)")
    parser.add_argument("--ctrl-uart", help="Controller UART port")
    parser.add_argument("--tgt-uart", help="Target UART port")
    parser.add_argument("--session-dir", default=None,
                        help="归档会话目录（reports/<date>-<idx> 或任意路径）；"
                             "不带则按当天自动分配新会话目录")
    parser.add_argument(
        "--case-timeout",
        type=int,
        default=int(os.environ.get("I3C_CASE_TIMEOUT", "60")),
        help="Seconds per case run (default: 60; 仅兜底——正常由 10s 静默判据提前结束)",
    )
    parser.add_argument(
        "--no-archive",
        action="store_true",
        help="Print report to console only, skip reports/ archiving",
    )
    parser.add_argument(
        "--build-only",
        action="store_true",
        help="Build all cases and exit",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Assume images are already built",
    )
    args = parser.parse_args()
    ctrl_port = args.ctrl_uart or os.environ.get("I3C_CTRL_UART")
    tgt_port = args.tgt_uart or os.environ.get("I3C_TGT_UART")

    if not ctrl_port or not tgt_port:
        print("ERROR: I3C_CTRL_UART and I3C_TGT_UART must be set or passed via --ctrl-uart/--tgt-uart", file=sys.stderr)
        ports = list_serial_ports()
        if ports:
            print("Available serial ports:", ", ".join(ports), file=sys.stderr)
        else:
            print("No serial ports detected.", file=sys.stderr)
        return 1

    cases = find_cases(args.case)
    if not cases:
        print(f"ERROR: no cases found in {TESTS_DIR}", file=sys.stderr)
        return 1

    print(f"Cases: {[c.name for c in cases]}")
    print(f"Controller UART: {ctrl_port}")
    print(f"Target UART: {tgt_port}")
    print(f"Case timeout: {args.case_timeout}s")
    session_dir = None   # 延迟分配：原始串口归档与报告共用同一会话目录
    # ★ 必须在 `_DUAL` 的 session lambda **之前**求值：lambda 闭包捕获本变量，
    # 而 dual_run() 在 L1 采集成功后立刻调用它（早于本函数尾部）。带
    # `--session-dir` 时 `session_dir or (...)` 短路，这条路径被掩盖过。
    date_str = datetime.now().strftime("%Y-%m-%d")

    # Build phase.
    if not args.skip_build:
        bash = acquire.find_git_bash("filtered")
        for case_dir in cases:
            try:
                acquire.build_via_test_sh_raise(case_dir, bash)
            except RuntimeError as exc:
                print(f"ERROR: {exc}", file=sys.stderr)
                return 1

    if args.build_only:
        print("\nBuild-only requested; skipping run phase.")
        return 0

    test_names = TEST_NAMES
    # L2 规格：expected 由 l2_spec() 内部构造
    _L2_SPEC = l2_spec({"ctrl_uart": ctrl_port, "tgt_uart": tgt_port}, test_names)
    print(f"Tests per case: {test_names}")

    # L1 采集规格（common/acquire.py）：两遍烧录 + 双核采集 + 静默判完成 + §10.22 自愈重试
    _DUAL = acquire.DualRunSpec(
        driver=DRIVER_NAME, root=SAMPLE_ROOT, tests_dir=SAMPLE_ROOT / "tests",
        board="lsqsh_evb@2os", ctrl_uart=ctrl_port, tgt_uart=tgt_port,
        case_timeout=args.case_timeout, archive=not args.no_archive,
        script=__file__, banner_key="I2C_PURE_TEST:", announce=False,
        abort_on_load_fail=False,
        ctrl_banners=CTRL_BANNERS, tgt_banners=TGT_BANNERS,
        # 单出口（SPEC_REFERENCE §6.9.3/§6.9.4）：本工程从核是 ztest 型，逐项
        # 输出经 `tc_util_user_override.h` 换到 RLOG；主核转发时加
        # `I2C_PURE_TEST:TGT:` 核信封 ⇒ L1 据此分拣回 serial_TGT.log。
        # 信封是中继侧新加的**壳**（D1 行本来没有前缀）⇒ strip=True，使归档
        # 与"从核本来自己打"逐行等价。
        relay_prefixes=("I2C_PURE_TEST",),
        relay_strip=True,
        session=lambda: session_dir or (
            Path(args.session_dir) if args.session_dir
            else allocate_session_dir(REPORTS_ROOT, date_str)))

    # Run phase: preflight -> each case: L1（clean → 烧录采集 → 落归档 → mem）-> L2 judge
    acquire.preflight_gdb(style="d3")
    results: List[Dict] = []
    for case_dir in cases:
        acq = acquire.dual_run(_DUAL, case_dir)
        if acq.rc:
            return acq.rc
        if not acq.ok:
            return 1
        session_dir = acq.session
        # L1→L2 边界（L0 归档形状）：两核原始行**分核**归一；mem 兜底是 L1 侧产物
        # （读目标内存，不进归档）⇒ 作为独立入参交给 L2（common/process.py）。
        results.append(process.judge(_L2_SPEC, process.L2Input(
            case=case_dir.name, lines=acq.lines, mem=acq.mem)))

    print_report(results)

    if args.no_archive:
        print("\n--no-archive: skipping reports/ archiving.")
        return 0 if all(r["result"] == "PASS" for r in results) else 1

    # Archive (§12)：会话目录 reports/<date>-<idx>/，每 case 子目录
    # <driver>_<case>/<driver>_report.{md,json} + summary.md 索引行
    # （落盘 / 汇总走 common/process.py::emit，与既有写法逐字一致）。
    # date_str 已在 `_DUAL` 之前求值（同一天、同一进程内取值不变），此处不重复赋值。
    session_dir = session_dir or (
            Path(args.session_dir) if args.session_dir
        else allocate_session_dir(REPORTS_ROOT, date_str))
    case_label = args.case or (cases[0].name if len(cases) == 1 else "all_cases")
    process.emit(_L2_SPEC, results, session_dir, case_label=case_label)
    return process.rc(_L2_SPEC, results)


if __name__ == "__main__":
    sys.exit(main())
