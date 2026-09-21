#!/usr/bin/env python3
"""mbedtls single-core regression runner: build cases, flash, capture, evaluate,
archive reports (SPEC_REFERENCE.md §6.4/§6.5).

单核工程版：从 otbn_test 样板 run_test.py 改造（§6.2 判据/单核版），
硬件流程保留交互式单遍烧录（reset → load → 等 "Transfer rate" → set $pc →
continue，§10.24/§10.25）+ 写包提速（§10.23）+ DebugServer 自愈重试（§10.22）。

固件为纯 ztest（ztest_glue）：结果判定以 ztest 原生逐项输出为数据源
（common/ztest_report.parse_ztest），完成标记 = "PROJECT EXECUTION
SUCCESSFUL/FAILED"；串口缺项用 dump_results.py --json 内存兜底（§12.3）。

单核判定（§6）：本核全项 `PASS - <name>` + 本核 `PROJECT EXECUTION
SUCCESSFUL`。测试名按各 case 自己的 elf 链接序现场推导（dump_results.
unit_test_names，§10.27）——条件编译使 3 个 case 测试集不同，禁止硬编码。

流程（每 case）：
    1. 编译（west build -p auto + CONF_FILE=prj.conf;case.conf，Kconfig 路由）
    2. 烧录 + 串口采集（交互式单遍，dbg_run.py 逻辑内联）
    3. 解析（parse_ztest：ZT_BOOT / PASS|FAIL - <name> / INFO / PROJECT EXECUTION）
    4. 判定（§13.5 可归因 reason；串口缺项用 dump_results.py --json 内存兜底，§12.3）
    5. 报告归档（§12）：会话目录 reports/<date>-<idx>/mbedtls_test_<case>/
       mbedtls_test_report.{md,json} + summary.md 索引行（common/report_dir.py）

Usage:
    python run_test.py                 # 3 个 case 全流程 + 归档
    python run_test.py --case case_otbn_ecc   # 只跑指定 case
    python run_test.py --no-archive    # 只跑不改报告
    python run_test.py --skip-build    # 复用现有构建
"""
import argparse
import json
import os
import tempfile
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

# tests/run_test.py -> parents[1] = mbedtls_test 工程根
SAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"   # 810_driver_test/reports/
GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
# 单核一律 1os 构建，cpu1 入口 = 0x10000000（readelf 实测）；
# 2os 构建入口 0x100c0000，需显式 TARGET_ROM_START=0x100c0000 环境变量覆盖
# （不得静默改地址，§10 规范）
TARGET_ROM_START = os.environ.get("TARGET_ROM_START", "0x10000000")
BOARD = "lsqsh_evb@1os/lsqsh/cpu1"   # 单核固件一律 1os 编译测试
DRIVER_NAME = "mbedtls_test"         # §12.4 summary 行 `<driver>_test` 前缀
REPORT_BASE = f"{DRIVER_NAME}_report"   # mbedtls_test_report.{md,json}
FW_SRC = SAMPLE_ROOT / "cpu1_mbedtls_test"
CASES = [
    "case_sw_baseline",
    "case_otbn_ecc",
    "case_hw_otbn_mix",
    "case_hw_all",
    "case_otbn_hash",
    "case_otbn_rsa",
    "case_otbn_all",
    "case_otbn_sha256_hw_sm3",
    "case_sw_hw_mix",
    "case_all_on_max",
]

sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
sys.path.insert(0, str(SAMPLE_ROOT / "tests" / "scripts"))
from ztest_report import parse_ztest   # noqa: E402
from serial_archive import archive_serial  # noqa: E402
import process  # noqa: E402  —— L2 处理层（归档 → 判定 → 报告）
import acquire  # noqa: E402  —— L1 采集层（构建/烧录/采集/落归档）
from report_dir import (allocate_session_dir, case_dir_for,  # noqa: E402
                        update_summary)

# ---- F1/F2/F3 判定框架收敛点（common/judge.py）----
from judge import (MEM_ONLY_NOTE, mem_only_evidence,
                   EXEC_UNKNOWN, MEM_OK, exec_label, mem_state_line,  # noqa: E402
                   mem_state_of_payload, missing_evidence,
                   missing_evidence_note, truncation_evidence)
from judge import (REPORT_TABLE_HEADER, REPORT_TABLE_SEP,  # noqa: E402
                   clip_cell, coverage_expected, format_report_header,
                   md_table_row, result_text, source_label)
from dump_results import unit_test_names   # noqa: E402  （链接序 TEST_NAMES）

TGT_BANNERS = ("CPU1 MBEDTLS", "mbedtls_test", "MBEDTLS test")

DEBUGSERVER_EXE = r"C:\uaer_app\debug_server\bin\DebugServerConsole.exe"
# ztest 原生逐项输出的完成/活动标记（parse_ztest 数据源）
ZT_BOOT_RE = re.compile(r"ZT_BOOT TGT")
EXEC_RE = re.compile(r"PROJECT EXECUTION (SUCCESSFUL|FAILED)")
ZTEST_ACTIVITY_RE = re.compile(r"(ZT_BOOT|START - | PASS - | FAIL - | SKIP - "
                               r"|PROJECT EXECUTION|Assertion failed)")


ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

# 判定状态：src 表示来源（token / mem / missing）
PASS, FAIL, MISSING, NOT_RUN = "PASS", "FAIL", "MISSING", "NOT_RUN"


def log(msg):
    print(msg, flush=True)


def parse_expected_from_coverage():
    """§6.5.2 预期数据（COVERAGE.md 表「预期数据」列）—— 解析收敛在 common/judge.py。

    只认列头含 `预期数据` 的表；转义竖线与裸竖线都不再串列（旧实现用
    `[^|]*` 解析，COVERAGE 里的转义竖线被截成半个单元格，见 §1.4）。"""
    return coverage_expected(SAMPLE_ROOT / "tests" / "COVERAGE.md")


def evaluate_case(lines, test_names, mem_data):
    """单核判定：ztest 原生输出优先 → 内存兜底 → NOT_RUN（§12.3）。

    返回 (results, infos, reasons, notes, execution)。"""
    p = parse_ztest(lines, tgt_banners=TGT_BANNERS)
    results, notes, reasons = {}, {}, {}
    for name in test_names:
        t = p["tests"].get(name)
        st = t["result"] if t else MISSING
        note = "src=token"
        if t and t["reason"]:
            reasons[name] = t["reason"]
        if st == MISSING and mem_data is not None:
            mem = mem_data.get("TGT", {}).get(name)
            if mem == "PASS":
                st, note = PASS, "src=mem"
                reasons.setdefault(name, mem_data.get("reasons", {}).get(name, ""))
            elif mem == "FAIL":
                st, note = FAIL, "src=mem"
                reasons.setdefault(name, mem_data.get("reasons", {}).get(name, ""))
            elif mem == "NOT_RUN":
                st, note = NOT_RUN, "src=mem(NOT_RUN)"
            else:
                note = "mem-unavailable"
        elif st == MISSING and mem_data is None:
            st, note = FAIL, "mem-unavailable"
        results[name] = st
        notes[name] = note
    return results, p["infos"], reasons, notes, p["execution"]


def write_markdown_report(case, results, infos, reasons, notes, expected, execution,
                          date_dir, env_line, mem_line="", missing=None,
                          case_ok=None, passed=0, ran=0):
    """§6.4.2 覆盖写：同一天多次回归只保留 latest（对比历史用其他日期）。

    列序 / 表头 / 单元格转义统一走 common/judge.py（REPORT_TABLE_HEADER +
    md_table_row），与全家族逐字一致；「期望」列超长截断（完整值见 tests/COVERAGE.md）。
    """
    base = date_dir / f"{REPORT_BASE}.md"
    if case_ok is None:   # 兜底：调用方未传判定结果时不猜 PASS
        case_ok = (execution == "SUCCESSFUL" and ran > 0 and passed == ran)
    lines = [
        '# mbedtls 测试报告',
        "",
        *format_report_header(
            DRIVER_NAME, case, BOARD,
            date=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            env=env_line, execution=execution, mem_line=mem_line,
            result=f"{'PASS' if case_ok else 'FAIL'} ({passed}/{ran})"),
        *([""] + ["> " + missing_evidence_note(missing)] if missing else []),
        "",
        "## 测试项结果",
        "",
        REPORT_TABLE_HEADER,
        REPORT_TABLE_SEP,
    ]
    for name in results:
        st = results[name]
        if st == PASS or st == NOT_RUN:
            reason = ""
        else:
            reason = reasons.get(name, "")
        info = ", ".join(f"{k}={v}" for k, v in infos.get(name, {}).items()) or "-"
        lines.append(md_table_row([
            name, result_text(st), source_label(notes.get(name, "")),
            clip_cell(expected.get(name, "") or "-"), info, reason]))
    lines.append("")
    lines.append(f"## 汇总\n\nOVERALL: {'PASS' if case_ok else 'FAIL'}\n")
    base.parent.mkdir(parents=True, exist_ok=True)
    base.write_text("\n".join(lines), encoding="utf-8")


def write_json_report(case, results, infos, reasons, notes, execution,
                      case_dir, env_line, mem_state=MEM_OK, mem_note="",
                      missing=None):
    """§12.1 覆盖写：同一天多次回归只保留 latest run。"""
    base = case_dir / f"{REPORT_BASE}.json"
    data = {
        "driver": DRIVER_NAME, "case": case, "board": BOARD,
        "env": env_line, "execution": execution,
        "mem_state": mem_state, "mem_note": mem_note,
        "missing_evidence": missing or [],
        "timestamp": datetime.now().isoformat(timespec="seconds"),
        "results": {name: st for name, st in results.items()},
        "infos": infos, "reasons": reasons, "notes": notes,
    }
    base.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def judge_case(results, passed, ran, marker, declared):
    """F1 判定（收敛自 common/judge.py）。**常规路径逐字等于改动前公式**：

        case_ok = (passed == ran and ran > 0 and execution == "SUCCESSFUL")

    新增的只有"截断接受"分支：收尾标记缺失时，只要**所有已声明测试项**都已拿到
    确定结果（token 或 mem，无 MISSING）且全 PASS ⇒ 判 PASS，并把 execution 标成
    TRUNCATED_AFTER_ALL_ITEMS（报告里一眼能看出"截断但全项已出结果"）。
    ★ 红线：任一已声明项**无证据** ⇒ 维持 FAIL，不得被截断接受放过。
    ★ R4（2026-09-18 冻结轮）：**常规路径**同样要求已声明项都有确定结果 ——
    收尾标记齐全但存在 NOT_RUN（该项从未执行）⇒ 判 FAIL，理由是"证据缺失（非驱动失败）"。
    返回 {"ok": bool, "exec": str, "truncated": bool, "missing": [name, ...]}。
    """
    # 常规路径也要求"已声明项都有确定结果"（★ 2026-09-18 冻结轮 R4）：
    # 收尾标记齐全却有项**从未执行**（NOT_RUN，tr_record 没被调用）是**自相矛盾**
    # 的状态 —— 报告会声称一次全项通过，而其中若干项根本没有证据。与"零证据判过"
    # "截断接受放过 NOT_RUN"是同一类缺陷：换个位置，同样的谎还能说。
    # ★ SKIP 是主动跳过 = 确定结果，不计入 missing_evidence。
    _missing = missing_evidence(declared, results)
    ok = (passed == ran and ran > 0 and marker == "SUCCESSFUL" and not _missing)
    trunc_ok, _m2 = truncation_evidence(marker, declared, results)
    truncated = False
    if not ok and trunc_ok and ran > 0 and passed == ran:
        ok, truncated = True, True
    return {"ok": ok, "exec": exec_label(marker, truncated), "truncated": truncated,
            "missing": _missing}


# ---- L2 处理层规格（common/process.py）：只声明**本工程特有**的接入点 ----
# 判定族特有：ztest token 优先 → 内存兜底 → R4 完整度守卫 → R7「批量必须靠 token」。
# 契约：每工程暴露 `l2_spec(facts=None, test_names=None)`；`facts` = **未归档的采集事实**
# （此处 `tgt_uart` / `timeout` 只用于报告「环境」行 —— 它们不进 L0 归档）；
# `test_names` = 本 case 的测试项清单（本工程按该 case 的 elf 链接序现场推导，§10.27）。
def _declared_test_names(facts):
    """**逐 case** 的测试项表：由该 case 的 ELF 链接序导出（条件编译使 3 个 case
    测试集不同，禁止硬编码）。离线（`--l2-direct`）只给 facts 时用它兜底。
    """
    elf = (SAMPLE_ROOT / "tests" / (facts.get("case") or "")
           / "build" / "zephyr" / "zephyr.elf")
    return list(unit_test_names(elf.as_posix()))


def l2_spec(facts=None, test_names=None):
    facts = facts or {}
    test_names = test_names if test_names is not None else _declared_test_names(facts)
    env_line = (f"tgt_uart={facts.get('tgt_uart') or 'auto'}, "
                f"timeout={facts.get('timeout')}s")
    expected = parse_expected_from_coverage()

    def _judge(ci):
        mem = ci.mem
        mem_state, mem_note = mem_state_of_payload(mem)
        results, infos, reasons, notes, execution = evaluate_case(
            ci.lines["TGT"], test_names, mem)
        passed = sum(1 for n in test_names if results[n] == PASS)
        ran = sum(1 for n in test_names if results[n] != NOT_RUN)
        mem_line_before = mem_state_line(mem_state, mem_note)
        # R7：全项证据都来自 mem ⇒ 不得判 PASS；理由串挂 mem_note（三处同源）
        mem_only = mem_only_evidence(notes.values())
        if mem_only:
            mem_note = ((mem_note + "；") if mem_note else "") + MEM_ONLY_NOTE
        verdict = judge_case(results, passed, ran, execution, test_names)
        return {"case": ci.case, "mem_none": mem is None,
                "mem_state": mem_state, "mem_note": mem_note,
                "mem_line": mem_state_line(mem_state, mem_note),
                "mem_line_before": mem_line_before, "mem_only": mem_only,
                "results": results, "infos": infos, "reasons": reasons,
                "notes": notes, "execution": execution, "passed": passed,
                "ran": ran, "verdict": verdict,
                "exec_report": verdict["exec"], "missing": verdict["missing"],
                "case_ok": verdict["ok"] and not mem_only}

    def _console(jl):
        b = jl[0]
        if b["mem_none"]:
            log("[run] memory backfill: mem-unavailable (串口 token 缺失时按 §12.3 判 FAIL)")
        log(f"[run] {b['mem_line_before']}")
        log(f"--- {b['case']} results ---")
        for name in test_names:
            tag = f"  {name:<24} {b['results'][name]}"
            if b["reasons"].get(name):
                tag += f"  ({b['reasons'][name]})"
            log(tag)
        log(f"-> {b['passed']}/{b['ran']} PASS, execution={b['execution']}")
        if b["mem_only"]:
            log("[run] " + MEM_ONLY_NOTE)
        if b["verdict"]["truncated"]:
            log("[run] NOTE: 执行窗口截断，但**全部已声明测试项都已出结果** ⇒ 判 PASS"
                f"（execution={b['exec_report']}）")
        if b["missing"]:
            log("[run] " + missing_evidence_note(b["missing"]))

    def _render_md(jl, cdir):
        b = jl[0]
        write_markdown_report(b["case"], b["results"], b["infos"], b["reasons"],
                              b["notes"], expected, b["exec_report"], cdir, env_line,
                              b["mem_line"], b["missing"], b["case_ok"],
                              b["passed"], b["ran"])
        return cdir / f"{REPORT_BASE}.md"

    def _render_json(jl, cdir):
        b = jl[0]
        write_json_report(b["case"], b["results"], b["infos"], b["reasons"],
                          b["notes"], b["exec_report"], cdir, env_line,
                          b["mem_state"], b["mem_note"], b["missing"])
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
    ap.add_argument("--no-archive", action="store_true", help="只跑不改报告")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--case-only", default=None, help="只跑指定 case（默认 3 个全跑）")
    ap.add_argument("--case", default=None, help=argparse.SUPPRESS)
    ap.add_argument("--session-dir", default=None)
    ap.add_argument("--tgt-uart", default=None)
    ap.add_argument("--timeout", type=float, default=600,
                    help="单 case 采集窗口硬上限（含软件 RSA-2048 keygen，勿小于 300）")
    args = ap.parse_args()
    if args.case and not args.case_only:
        args.case_only = args.case
    cases = [args.case_only] if args.case_only else CASES
    date_str = datetime.now().strftime("%Y-%m-%d")
    session_dir = None   # 延迟分配：原始串口归档与报告共用同一会话目录

    acquire.clean_gdb_residue(style="otbn")   # §6.2-6：回归前按需清连接残留
    n_fail = 0   # rc 语义：0=全部 case PASS / 1=有 FAIL（run_all 批量判定用）

    # L1 采集规格（common/acquire.py）：watch 驱动（交互式 gdb + 完成标记提前收尾）
    spec = acquire.PortsDictSpec(
        driver=DRIVER_NAME, root=SAMPLE_ROOT, tests_dir=SAMPLE_ROOT / "tests", case=args.case,
        board=BOARD, tgt_uart=args.tgt_uart, timeout=args.timeout,
        target_rom_start=TARGET_ROM_START, skip_build=True,
        archive=not args.no_archive, script=__file__,
        src_dir="cpu1_mbedtls_test", build_subdir="", build_progress="always",
        build_timeout=900, build_conf=True,
        driver_kind="watch", stall_s=300, retry_on_no_boot=True,
        tgt_banners=TGT_BANNERS,
        mem_dumper=str(SAMPLE_ROOT / "tests" / "scripts" / "dump_results.py"),
        session=lambda: session_dir or (
            Path(args.session_dir) if args.session_dir
            else allocate_session_dir(REPORTS_ROOT, date_str)))

    for case in cases:
        log(f"===== CASE {case} =====")
        if not args.skip_build:
            ok, err = acquire.west_build(spec, case)
            if not ok:
                log(f"[run] BUILD FAILED: {err}")
                n_fail += 1
                continue
        elf = SAMPLE_ROOT / "tests" / case / "build" / "zephyr" / "zephyr.elf"
        if not Path(elf).exists():
            log(f"[run] ELF NOT BUILT: {elf}")
            n_fail += 1
            continue
        test_names = unit_test_names(elf.as_posix())
        log(f"[run] {len(test_names)} tests (link order): {', '.join(test_names)}")

        # L2 规格：expected / env_line 由 l2_spec() 内部构造（facts = 未归档采集事实）
        _L2_SPEC = l2_spec({"tgt_uart": args.tgt_uart, "timeout": args.timeout},
                           test_names)
        # L1 采集（common/acquire.py）：烧录采集（含 §10.22 自愈重试）→ L0 归档 → mem
        acq = acquire.ports_dict_case(spec, case)
        if not acq.ok:
            log(f"[run] CAPTURE FAILED（环境问题，已自愈重试）: {acq.err}")
            n_fail += 1
            continue
        session_dir = acq.session

        # L1→L2 边界（L0 归档形状）：采集行按归档形状归一；mem 兜底是 L1 侧
        # 产物（读目标内存，不进归档）⇒ 作为独立入参交给 L2（common/process.py）。
        outcome = process.run(_L2_SPEC, case=case, lines=acq.lines, mem=acq.mem,
                              session=session_dir, write=not args.no_archive,
                              case_label=args.case)
        if args.no_archive:
            log("[run] --no-archive: 未写报告")
        if outcome.rc != 0:
            n_fail += 1

    log("===== DONE =====")
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
