#!/usr/bin/env python3
"""TRNG single-core regression runner: build cases, flash, capture, evaluate,
archive reports (SPEC_REFERENCE.md §6.4/§6.5).

单核工程版：从 i3c_dualcore_test 样板 run_i3c_tests.py 改造，去掉 controller 核。
每项测试只需 TGT 单 PASS 即过（单核无对侧核）。

流程（每 case）：
    1. 编译（west build -p auto，CLAUDE.md 构建规范）
    2. 烧录 + 串口采集（dbg_run.py 逻辑内联）
    3. token 解析（START/PASS/FAIL + INFO + SUMMARY + DONE）
    4. 判定（§13.5 可归因 reason；串口缺项用 dump_results.py --json 内存兜底，§12.3）
    5. 报告归档（§12）：reports/<YYYY-MM-DD>/trng_test_report.{md,json} + summary.md 索引行

Usage:
    python run_test.py                 # 全流程 + 归档
    python run_test.py --no-archive   # 只跑不改报告
    python run_test.py --skip-build   # 复用现有构建
    python run_test.py --case-only case_default  # 只跑指定 case
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import threading
import time
from datetime import datetime
from pathlib import Path

import serial
import serial.tools.list_ports as list_ports

# tests/run_test.py -> parents[1] = trng_test 工程根
SAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"   # 810_driver_test/reports/

sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
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
DRIVER_NAME = "trng_test"          # §12.4 summary 行 `<driver>_test` 前缀
REPORT_BASE = f"{DRIVER_NAME}_report"   # trng_test_report.{md,json}（避免 _test_test 重复）

TGT_BANNERS = ("CPU1 TRNG", "trng_test", "TRNG test")

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

# 判定状态：src 表示来源（token / mem / missing）
PASS, FAIL, MISSING, NOT_RUN, SKIP = "PASS", "FAIL", "MISSING", "NOT_RUN", "SKIP"

# 测试项清单：**必须与 scripts/dump_results.py 的 TEST_NAMES 逐字同序**（§1.8），
# 且按 ztest_unit_test 段的**链接序**（SORT_BY_NAME）排列 —— mem 兜底的 idx 对齐
# 靠它，顺序错了会张冠李戴/假 MISSING。核对命令：
#   nm --numeric-sort <elf> | grep _ztest_unit_test__trng__
# 2026-09-18（P1a 后）：固件实有 15 项（原列表只列了 P1a 之前的 9 项，
# 导致报告漏 6 项、失败项 reason 无处归）；已按上述 nm 实测链接序补全。
TEST_NAMES = [
    # suite trng0 (APB ring-oscillator) — link order first: '0' sorts before '_'
    "apb_basic_16", "apb_distinct_32", "apb_invalid_args", "apb_isr_busywait",
    "apb_large_1024", "apb_monobit_8k", "apb_not_all_ff", "apb_not_all_zero",
    "apb_throughput",
    # suite trng (DesignWare)
    "autocorr_8k", "basic_16", "block_freq_8k", "concurrent_get",
    "distinct_32", "invalid_args", "isr_busywait", "isr_nonblock",
    "large_1024", "long_seq_1m", "monobit_8k", "not_all_ff",
    "not_all_zero", "runs_8k", "throughput_64k",
]


def log(msg):
    print(msg, flush=True)


def parse_expected_from_coverage():
    """§6.5.2 预期数据（COVERAGE.md 表「预期数据」列）—— 解析收敛在 common/judge.py。

    只认列头含 `预期数据` 的表；转义竖线与裸竖线都不再串列（旧实现用
    `[^|]*` 解析，COVERAGE 里的转义竖线被截成半个单元格，见 §1.4）。"""
    return coverage_expected(SAMPLE_ROOT / "tests" / "COVERAGE.md")


def evaluate_case(case, lines, test_names, mem_data):
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
        if st == SKIP:
            # ztest_test_skip()：用例因前置条件不满足而主动跳过。
            # 归为 SKIP：**不计入 ran**（与 NOT_RUN 同理），不影响 case_ok，
            # 报告里单独标注 skipped=N。2026-09-18：此前 SKIP 被算进分母
            # （分母虚高、通过率被系统性低估），与 spi_test/i2cf_test 统一。
            results[name] = SKIP
            notes[name] = "src=token(skip)"
            continue
        if st == MISSING and mem_data is not None:
            mem = mem_data.get("TGT", {}).get(name)
            # R8（2026-09-21，用户裁定 2026-09-20）：**mem 取到的项一律不算证据**。
            # 取值保留给报告（来源列 `mem(非证据:X)`，人可查），但逐项结果落
            # MEM_NOEVID ⇒ 不进 passed / 不进 ran / 计入 missing_evidence，
            # 由既有的"无证据"通道兜住（截断接受也不再放行）。
            if mem in ("PASS", "FAIL", "NOT_RUN"):
                st, note = MEM_NOEVID, mem_noevid_source(mem)
                if mem != "NOT_RUN":
                    reasons.setdefault(name, mem_data.get("reasons", {}).get(name, ""))
            else:
                note = "mem-unavailable"
        elif st == MISSING and mem_data is None:
            st, note = FAIL, "mem-unavailable"
        results[name] = st
        notes[name] = note
    return results, p["infos"], reasons, notes, p["execution"]


def write_markdown_report(case, results, infos, reasons, notes, expected, date_dir,
                          env_line, execution=EXEC_UNKNOWN, mem_line="",
                          missing=None, case_ok=None, passed=0, ran=0):
    """§6.4.2 覆盖写：同一天多次回归只保留 latest（对比历史用其他日期）。

    列序 / 表头 / 单元格转义统一走 common/judge.py（REPORT_TABLE_HEADER +
    md_table_row），与全家族逐字一致；「期望」列超长截断（完整值见 tests/COVERAGE.md）。
    """
    base = date_dir / f"{REPORT_BASE}.md"
    if case_ok is None:   # 兜底：调用方未传判定结果时不猜 PASS
        case_ok = (execution == "SUCCESSFUL" and ran > 0 and passed == ran)
    lines = [
        '# TRNG 驱动测试报告',
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
        elif st == SKIP:
            reason = '用例主动跳过（ztest_test_skip）；不计入分母'
        elif st == MEM_NOEVID:
            # R8：mem 取到的项 —— 理由串必须与"驱动/测试失败"分开表述
            reason = MEM_WEAK_ITEM_NOTE + (
                f"；{reasons[name]}" if reasons.get(name) else "")
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


def write_json_report(case, results, infos, reasons, notes, date_dir, env_line,
                      execution=EXEC_UNKNOWN, mem_state=MEM_OK, mem_note="",
                      missing=None):
    """§12.1 覆盖写：同一天多次回归只保留 latest run。"""
    base = date_dir / f"{REPORT_BASE}.json"
    data = {
        "driver": DRIVER_NAME, "case": case, "board": BOARD,
        "env": env_line, "timestamp": datetime.now().isoformat(timespec="seconds"),
        "execution": execution, "mem_state": mem_state, "mem_note": mem_note,
        "missing_evidence": missing or [],
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
# 契约：每工程暴露 `l2_spec(facts=None)`；`facts` = **未归档的采集事实**
# （此处 `tgt_uart` / `timeout` 只用于报告「环境」行 —— 它们不进 L0 归档）。
def l2_spec(facts=None):
    facts = facts or {}
    env_line = (f"tgt_uart={facts.get('tgt_uart') or 'auto'}, "
                f"timeout={facts.get('timeout')}s")
    expected = parse_expected_from_coverage()

    def _judge(ci):
        mem = ci.mem
        mem_state, mem_note = mem_state_of_payload(mem)
        results, infos, reasons, notes, execution = evaluate_case(
            ci.case, ci.lines["TGT"], TEST_NAMES, mem)
        passed = sum(1 for n in TEST_NAMES if results[n] == PASS)
        # R8：mem 非证据项不计入 ran（分子分母两端同时摘掉）
        ran = sum(1 for n in TEST_NAMES
                  if results[n] not in (NOT_RUN, SKIP, MEM_NOEVID))
        n_skip = sum(1 for n in TEST_NAMES if results[n] == SKIP)
        mem_line_before = mem_state_line(mem_state, mem_note)
        # R7：全项证据都来自 mem ⇒ 不得判 PASS；理由串挂 mem_note（三处同源）
        mem_only = mem_only_evidence(notes.values())
        if mem_only:
            mem_note = ((mem_note + "；") if mem_note else "") + MEM_ONLY_NOTE
        # R8（2026-09-21，用户裁定 2026-09-20）：mem 取到的项**一律不算证据**。
        # 无证据项按成因拆开：真"没测到"（missing）与"mem 有值但不算证据"
        # （mem_noevid）—— 判定效果相同（都进 missing_evidence ⇒ 不得判 PASS），
        # 但理由串必须分开，不能把 mem 有值说成"token 与内存兜底都没有值"。
        _missing, _mem_noevid = evidence_gaps(TEST_NAMES, results)
        _weak = mem_weak_note(
            [f"{n}(mem={mem_noevid_value(notes.get(n, ''))})" for n in _mem_noevid])
        if _weak:
            mem_note = ((mem_note + "；") if mem_note else "") + _weak
        verdict = judge_case(results, passed, ran, execution, TEST_NAMES)
        return {"case": ci.case, "mem_none": mem is None,
                "mem_state": mem_state, "mem_note": mem_note,
                "mem_line": mem_state_line(mem_state, mem_note),
                "mem_line_before": mem_line_before, "mem_only": mem_only,
                "results": results, "infos": infos, "reasons": reasons,
                "notes": notes, "execution": execution, "passed": passed,
                "ran": ran, "n_skip": n_skip, "verdict": verdict,
                "exec_report": verdict["exec"], "missing": _missing,
                "case_ok": verdict["ok"] and not mem_only}

    def _console(jl):
        b = jl[0]
        if b["mem_none"]:
            log("[run] memory backfill: mem-unavailable (串口 token 缺失时按 §12.3 判 FAIL)")
        log(f"[run] {b['mem_line_before']}")
        log(f"--- {b['case']} results ---")
        for name in TEST_NAMES:
            tag = f"  {name:<20} {b['results'][name]}"
            if b["reasons"].get(name):
                tag += f"  ({b['reasons'][name]})"
            log(tag)
        log(f"-> {b['passed']}/{b['ran']} PASS, skipped={b['n_skip']}, "
            f"execution={b['execution']}")
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
                              b["notes"], expected, cdir, env_line,
                              b["exec_report"], b["mem_line"], b["missing"],
                              b["case_ok"], b["passed"], b["ran"])
        return cdir / f"{REPORT_BASE}.md"

    def _render_json(jl, cdir):
        b = jl[0]
        write_json_report(b["case"], b["results"], b["infos"], b["reasons"],
                          b["notes"], cdir, env_line, b["exec_report"],
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
    ap.add_argument("--case-only", default=None)
    ap.add_argument("--case", default="case_default")
    ap.add_argument("--session-dir", default=None)
    ap.add_argument("--tgt-uart", default=None)
    ap.add_argument("--timeout", type=float, default=30)
    args = ap.parse_args()
    cases = [args.case_only] if args.case_only else [args.case]
    test_names = TEST_NAMES
    # L2 规格：expected / env_line 由 l2_spec() 内部构造（facts = 未归档采集事实）
    _L2_SPEC = l2_spec({"tgt_uart": args.tgt_uart, "timeout": args.timeout})
    date_str = datetime.now().strftime("%Y-%m-%d")
    session_dir = None   # 延迟分配：原始串口归档与报告共用同一会话目录
    n_fail = 0   # rc 语义：0=全部 case PASS / 1=有 FAIL（run_all 批量判定用）

    # L1 采集规格（common/acquire.py）：本工程的构建/采集形态差异**全在这几个字段**
    spec = acquire.PortsDictSpec(
        driver=DRIVER_NAME, root=SAMPLE_ROOT, tests_dir=SAMPLE_ROOT / "tests", case=args.case,
        board=BOARD, tgt_uart=args.tgt_uart, timeout=args.timeout,
        target_rom_start=TARGET_ROM_START, skip_build=args.skip_build,
        archive=not args.no_archive, script=__file__,
        src_dir="cpu1_trng_test", build_subdir="target",
        build_progress="always", build_timeout=600,
        build_conf=False, pre_erase=False, hold_cpu2=True,
        session=lambda: session_dir or (
            Path(args.session_dir) if args.session_dir
            else allocate_session_dir(REPORTS_ROOT, date_str)))

    for case in cases:
        log(f"===== CASE {case} =====")
        # L1 采集（common/acquire.py）：构建 → 烧录采集 → 落 L0 归档 → mem 兜底
        acq = acquire.ports_dict_case(spec, case)
        if not acq.ok:
            log(f"[run] BUILD FAILED: {acq.err}" if acq.phase == "build"
                else f"[run] CAPTURE FAILED: {acq.err}")
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
