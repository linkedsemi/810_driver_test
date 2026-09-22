#!/usr/bin/env python3
"""RTC (rtc_ls) single-core regression runner: build, flash, capture, evaluate, archive.

流程（每 case）：
    1. west build -p auto
    2. GDB 两遍烧录 + 串口采集
    3. RTC_TEST token 解析（START/PASS/FAIL/INFO/DONE）
    4. dump_results.py --json 内存兜底（§12.3）
    5. 归档到 reports/<YYYY-MM-DD>-<idx>/rtc_ls_test_case_default/

Usage:
    python run_test.py
    python run_test.py --no-archive
    python run_test.py --skip-build
    python run_test.py --tgt-uart COM24 --timeout 150
"""
import argparse
import json
import os
import re
import subprocess
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import threading
import time
from datetime import datetime
from pathlib import Path

import serial
import serial.tools.list_ports as list_ports

SAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
from report_dir import allocate_session_dir, case_dir_for, update_summary  # noqa: E402
from serial_archive import archive_serial  # noqa: E402
import process  # noqa: E402  —— L2 处理层（归档 → 判定 → 报告）
import acquire  # noqa: E402  —— L1 采集层（构建/烧录/采集/落归档）

# ---- F1/F2/F3 判定框架收敛点（common/judge.py）----
from judge import (MEM_ONLY_NOTE, mem_only_evidence,
                   EXEC_UNKNOWN, MEM_OK, exec_label, mem_state_line,  # noqa: E402
                   mem_state_of_payload, truncation_evidence)
# ---- R8（2026-09-21）: 批量路径下 mem 取到的项一律不算证据 ----
from judge import (MEM_NOEVID, MEM_WEAK_ITEM_NOTE, evidence_gaps,  # noqa: E402
                   mem_noevid_source, mem_noevid_value, mem_weak_note)
from judge import (REPORT_TABLE_HEADER, REPORT_TABLE_SEP,  # noqa: E402
                   clip_cell, coverage_expected, format_report_header,
                   md_table_row, result_text, source_label)

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
# 用户需求#8：单核一律 1os 构建，cpu1 入口 = 0x10000000（readelf 实测）；
# 2os 构建入口 0x100c0000，需显式 TARGET_ROM_START=0x100c0000 环境变量覆盖
TARGET_ROM_START = os.environ.get("TARGET_ROM_START", "0x10000000")
BOARD = "lsqsh_evb@1os/lsqsh/cpu1"   # 用户需求#8：单核固件一律 1os 编译测试
DRIVER_NAME = "rtc_ls_test"
REPORT_BASE = f"{DRIVER_NAME}_report"

TGT_BANNERS = ("CPU1 RTC", "rtc_test", "RTC test")

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
TOKEN_RE = re.compile(r"RTC_TEST:(TGT):(START|PASS|FAIL):(\w+)(?::(.*))?\s*")
# 固件实际形态：`RTC_TEST:TGT:INFO:<测试项>:<kv 文本>`，kv 文本本身**含 `:`**
# （`alarm_fires` → `fire1:triggered=1,pending=0,now=2026-09-01 08:00:07`；
#  `alarm_cb` → `callback triggered`；`calendar_rollover` → `jan30_to_31:set=…`），
# 且含空格与中文日期 ⇒ 值取"冒号之后到行尾（去尾空白）"，字符类按实测集取。
INFO_RE = re.compile(r"RTC_TEST:(?:TGT):INFO:(\w+):([\w.=,\-: ]+?)\s*$")
DONE_RE = re.compile(r"RTC_TEST:DONE:(PASS|FAIL)")

PASS, FAIL, MISSING, NOT_RUN = "PASS", "FAIL", "MISSING", "NOT_RUN"

TEST_NAMES = [
    "set_get_roundtrip", "time_advances", "invalid_set_args", "alarm_set_get",
    "alarm_fires", "alarm_invalid_args",
    "calibration_roundtrip", "calibration_negative", "calibration_null_arg",
    "calendar_rollover",
    "invalid_set_args_ext", "leap_year_dates", "alarm_supported_fields",
    "update_callback_notsup", "drift_measure",
]


def log(msg):
    print(msg, flush=True)


def parse_tokens(lines):
    outcomes, infos, reasons = {}, {}, {}
    done = None
    for ln in lines:
        m = DONE_RE.search(ln)
        if m:
            done = m.group(1)
            continue
        m = TOKEN_RE.search(ln)
        if m:
            _, kind, name, tail = m.groups()
            if kind in ("PASS", "FAIL"):
                outcomes[name] = kind
                if kind == "FAIL" and tail:
                    reasons[name] = tail.strip()
            continue
        m = INFO_RE.search(ln)
        if m:
            name, kv = m.groups()
            infos.setdefault(name, {})["value"] = kv
    return outcomes, done, infos, reasons


def parse_expected_from_coverage():
    """§6.5.2 预期数据（COVERAGE.md 表「预期数据」列）—— 解析收敛在 common/judge.py。

    只认列头含 `预期数据` 的表；转义竖线与裸竖线都不再串列（旧实现用
    `[^|]*` 解析，COVERAGE 里的转义竖线被截成半个单元格，见 §1.4）。"""
    return coverage_expected(SAMPLE_ROOT / "tests" / "COVERAGE.md")


def evaluate_case(lines, mem_data):
    outcomes, done, infos, reasons = parse_tokens(lines)
    results, notes = {}, {}
    for name in TEST_NAMES:
        st = outcomes.get(name, MISSING)
        if st == MISSING and mem_data is not None:
            mem = mem_data.get("TGT", {}).get(name)
            # R8（2026-09-21，用户裁定 2026-09-20）：**mem 取到的项一律不算证据**。
            # 取值保留给报告（来源列 `mem(非证据:X)`，人可查），但逐项结果落
            # MEM_NOEVID ⇒ 不进 passed / 不进 ran / 计入 missing_evidence。
            if mem in ("PASS", "FAIL", "NOT_RUN"):
                st, notes[name] = MEM_NOEVID, mem_noevid_source(mem)
                if mem == "FAIL":
                    reasons.setdefault(name, mem_data.get("reasons", {}).get(name, ""))
            else:
                st, notes[name] = FAIL, "mem-unavailable"
        elif st == MISSING and mem_data is None:
            st, notes[name] = FAIL, "mem-unavailable"
        else:
            notes[name] = "src=token"
        results[name] = st
    return results, done, infos, reasons, notes


def write_markdown_report(results, infos, reasons, notes, expected, date_dir,
                          env_line, execution=EXEC_UNKNOWN, mem_line="",
                          case=None, case_ok=None, passed=0, ran=0):
    """§6.4.2 覆盖写：同一天多次回归只保留 latest。

    列序 / 表头 / 单元格转义统一走 common/judge.py（REPORT_TABLE_HEADER +
    md_table_row），与全家族逐字一致；「期望」列超长截断（完整值见 tests/COVERAGE.md）。
    """
    base = date_dir / f"{REPORT_BASE}.md"
    if case_ok is None:   # 兜底：调用方未传判定结果时不猜 PASS
        case_ok = (execution == "SUCCESSFUL" and ran > 0 and passed == ran)
    lines = [
        '# RTC (rtc_ls) 驱动测试报告',
        "",
        *format_report_header(
            DRIVER_NAME, case or "-", BOARD,
            date=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            env=env_line, execution=execution, mem_line=mem_line,
            result=f"{'PASS' if case_ok else 'FAIL'} ({passed}/{ran})"),
        "",
        "## 测试项结果",
        "",
        REPORT_TABLE_HEADER,
        REPORT_TABLE_SEP,
    ]
    for name in TEST_NAMES:
        st = results[name]
        info = infos.get(name, {}).get("value", "") or "-"
        lines.append(md_table_row([
            name, result_text(st), source_label(notes.get(name, "")),
            clip_cell(expected.get(name, "") or "-"), info,
            (MEM_WEAK_ITEM_NOTE + (f"；{reasons[name]}" if reasons.get(name) else ""))
            if st == MEM_NOEVID else
            (reasons.get(name, "") if st not in (PASS, NOT_RUN) else "")]))
    lines.append("")
    lines.append(f"## 汇总\n\nOVERALL: {'PASS' if case_ok else 'FAIL'}\n")
    base.parent.mkdir(parents=True, exist_ok=True)
    base.write_text("\n".join(lines), encoding="utf-8")


def write_json_report(results, infos, reasons, notes, date_dir, env_line,
                      execution=EXEC_UNKNOWN, mem_state=MEM_OK, mem_note=""):
    base = date_dir / f"{REPORT_BASE}.json"
    data = {
        "driver": DRIVER_NAME, "board": BOARD,
        "env": env_line, "timestamp": datetime.now().isoformat(timespec="seconds"),
        "execution": execution, "mem_state": mem_state, "mem_note": mem_note,
        "results": results, "infos": infos, "reasons": reasons, "notes": notes,
    }
    base.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def judge_case(results, passed, ran, marker, declared):
    """F1 判定（收敛自 common/judge.py）。**常规路径逐字等于改动前公式**：

        case_ok = (passed == ran and ran == len(TEST_NAMES) and done == "PASS")

    新增的只有"截断接受"分支：DONE 标记缺失时，只要所有已声明测试项都已出结果
    且全 PASS ⇒ PASS 并标注 TRUNCATED_AFTER_ALL_ITEMS。任一已声明项无证据 ⇒
    维持 FAIL（红线）。返回 {"ok": bool, "exec": str, "truncated": bool}。
    """
    ok = (passed == ran and ran == len(declared) and marker == "PASS")
    trunc_ok, _missing = truncation_evidence(marker, declared, results,
                                             good_marker="PASS", bad_marker="FAIL")
    truncated = False
    if not ok and trunc_ok and ran == len(declared) and passed == ran:
        ok, truncated = True, True
    return {"ok": ok, "exec": exec_label(marker, truncated, good_marker="PASS"),
            "truncated": truncated}


# ---- L2 处理层规格（common/process.py）：只声明**本工程特有**的接入点 ----
# 判定族特有：自定义 token `RTC_TEST:TGT:(PASS|FAIL):<name>` + `RTC_TEST:DONE:` 收尾
# （非 ztest 原生）。契约：每工程暴露 `l2_spec(facts=None, test_names=None)`；
# `facts` = **未归档的采集事实**（此处 tgt_uart / timeout 只用于报告「环境」行）。
def l2_spec(facts=None, test_names=None):
    facts = facts or {}
    test_names = test_names or TEST_NAMES
    env_line = (f"tgt_uart={facts.get('tgt_uart') or 'auto'}, "
                f"timeout={facts.get('timeout')}s")
    expected = parse_expected_from_coverage()

    def _judge(ci):
        mem = ci.mem
        mem_state, mem_note = mem_state_of_payload(mem)
        mem_line_before = mem_state_line(mem_state, mem_note)
        results, done, infos, reasons, notes = evaluate_case(ci.lines["TGT"], mem)
        passed = sum(1 for n in test_names if results[n] == PASS)
        # R8：mem 非证据项不计入 ran（分子分母两端同时摘掉）
        ran = sum(1 for n in test_names
                  if results[n] not in (NOT_RUN, MEM_NOEVID))
        mem_only = mem_only_evidence(notes.values())
        if mem_only:
            mem_note = ((mem_note + "；") if mem_note else "") + MEM_ONLY_NOTE
        # R8（2026-09-21，用户裁定 2026-09-20）：mem 取到的项**一律不算证据**。
        # 无证据项按成因拆开：真"没测到"（missing）与"mem 有值但不算证据"
        # （mem_noevid）—— 判定效果相同（都进 missing_evidence ⇒ 不得判 PASS），
        # 但理由串必须分开，不能把 mem 有值说成"token 与内存兜底都没有值"。
        _missing, _mem_noevid = evidence_gaps(test_names, results)
        _weak = mem_weak_note(
            [f"{n}(mem={mem_noevid_value(notes.get(n, ''))})" for n in _mem_noevid])
        if _weak:
            mem_note = ((mem_note + "；") if mem_note else "") + _weak
        verdict = judge_case(results, passed, ran, done, test_names)
        return {"case": ci.case, "done": done, "mem_state": mem_state,
                "mem_note": mem_note,
                "mem_line": mem_state_line(mem_state, mem_note),
                "mem_line_before": mem_line_before, "mem_only": mem_only,
                "results": results, "infos": infos, "reasons": reasons,
                "notes": notes, "passed": passed, "ran": ran, "verdict": verdict,
                "exec_report": verdict["exec"], "missing": _missing,
                "case_ok": verdict["ok"] and not mem_only}

    def _console(jl):
        b = jl[0]
        log(f"[run] {b['mem_line_before']}")
        log(f"--- {b['case']} results ---")
        for name in test_names:
            tag = f"  {name:<28} {b['results'][name]}"
            if b["reasons"].get(name):
                tag += f"  ({b['reasons'][name]})"
            log(tag)
        log(f"-> {b['passed']}/{b['ran']} PASS, done={b['done'] or 'no-DONE'}")
        if b["mem_only"]:
            log("[run] " + MEM_ONLY_NOTE)
        if b["verdict"]["truncated"]:
            log("[run] NOTE: 执行窗口截断，但**全部已声明测试项都已出结果** ⇒ 判 PASS"
                f"（execution={b['exec_report']}）")

    def _render_md(jl, cdir):
        b = jl[0]
        write_markdown_report(b["results"], b["infos"], b["reasons"], b["notes"],
                              expected, cdir, env_line, b["exec_report"],
                              b["mem_line"], b["case"], b["case_ok"],
                              b["passed"], b["ran"])
        return cdir / f"{REPORT_BASE}.md"

    def _render_json(jl, cdir):
        b = jl[0]
        write_json_report(b["results"], b["infos"], b["reasons"], b["notes"],
                          cdir, env_line, b["exec_report"], b["mem_state"],
                          b["mem_note"])
        return cdir / f"{REPORT_BASE}.json"

    return process.ProcessSpec(
        project=DRIVER_NAME, driver=DRIVER_NAME, report_base=REPORT_BASE,
        judge=_judge, console=_console,
        render_md=_render_md, render_json=_render_json,
        status=lambda j: "PASS" if j["case_ok"] else "FAIL",
        summary=lambda jl: ("PASS" if jl[0]["case_ok"] else "FAIL",
                            jl[0]["passed"], jl[0]["ran"]),
        log_archived=lambda jp, mp: log(f"[run] archived -> {mp}"),
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-archive", action="store_true")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--case", default="case_default")
    ap.add_argument("--session-dir", default=None)
    ap.add_argument("--tgt-uart", default=None)
    ap.add_argument("--timeout", type=float, default=150,
                    help="GDB continue 秒数（calendar_rollover 约需 90s+）")
    args = ap.parse_args()
    # L2 规格：expected / env_line 由 l2_spec() 内部构造（facts = 未归档采集事实）
    _L2_SPEC = l2_spec({"tgt_uart": args.tgt_uart, "timeout": args.timeout})
    date_str = datetime.now().strftime("%Y-%m-%d")
    session_dir = None   # 延迟分配：原始串口归档与报告共用同一会话目录
    n_fail = 0

    # L1 采集规格（common/acquire.py）：本工程的构建/采集形态差异**全在这几个字段**
    spec = acquire.PortsDictSpec(
        driver=DRIVER_NAME, root=SAMPLE_ROOT, tests_dir=SAMPLE_ROOT / "tests", case=args.case,
        board=BOARD, tgt_uart=args.tgt_uart, timeout=args.timeout,
        target_rom_start=TARGET_ROM_START, skip_build=args.skip_build,
        archive=not args.no_archive, script=__file__,
        src_dir="cpu1_rtc_test", build_subdir="target",
        build_progress="always", build_timeout=600,
        pre_erase=False, hold_cpu2=False,
        session=lambda: session_dir or (
            Path(args.session_dir) if args.session_dir
            else allocate_session_dir(REPORTS_ROOT, date_str)))

    log(f"===== CASE {args.case} =====")
    # L1 采集（common/acquire.py）：构建 → 烧录采集 → 落 L0 归档 → mem 兜底
    acq = acquire.ports_dict(spec)
    if not acq.ok:
        log(f"[run] BUILD FAILED: {acq.err}" if acq.phase == "build"
            else f"[run] CAPTURE FAILED: {acq.err}")
        return 1
    session_dir = acq.session

    # L1→L2 边界（L0 归档形状）：采集行按归档形状归一；mem 兜底是 L1 侧产物
    # （读目标内存，不进归档）⇒ 作为独立入参交给 L2（common/process.py）。
    outcome = process.run(_L2_SPEC, case=args.case, lines=acq.lines, mem=acq.mem,
                          session=session_dir, write=not args.no_archive)
    n_fail = outcome.rc

    log("===== DONE =====")
    return n_fail


if __name__ == "__main__":
    sys.exit(main())
