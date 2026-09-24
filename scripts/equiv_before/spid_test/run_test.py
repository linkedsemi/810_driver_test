#!/usr/bin/env python3
"""TPM SPI slave (PFR/TPM) single-core regression runner: build case, flash, capture, evaluate,
archive reports (SPEC_REFERENCE.md §6.4/§6.5).

单核工程版：从 trng_test（单核样板）run_test.py 改造，无 controller 核。
每项测试只需 TGT 单 PASS 即过（单核无对侧核）。

流程（每 case）：
    1. 编译（west build -p auto，CLAUDE.md 构建规范）
    2. 烧录 + 串口采集（与 dbg_run.py 同款两遍烧录法）
    3. ztest 原生输出解析（common/ztest_report.parse_ztest）
    4. 判定（串口缺项用 dump_results.py --json 内存兜底，§12.3）
    5. 报告归档（§12）：reports/<YYYY-MM-DD>-<idx>/spid_test/<driver>_test_report.{md,json} + summary.md 索引行

Usage:
    python run_test.py                 # 全流程 + 归档
    python run_test.py --no-archive   # 只跑不改报告
    python run_test.py --skip-build   # 复用现有构建
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

# tests/run_test.py -> parents[1] = spid_test 工程根
SAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"   # 810_driver_test/reports/

sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
from ztest_report import parse_ztest   # noqa: E402
from serial_archive import archive_serial  # noqa: E402
import process  # noqa: E402  —— L2 处理层（归档 → 判定 → 报告）
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

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
# 用户需求#8：单核一律 1os 构建，cpu1 入口 = 0x10000000（readelf 实测）；
# 2os 构建入口 0x100c0000，需显式 TARGET_ROM_START=0x100c0000 环境变量覆盖
TARGET_ROM_START = os.environ.get("TARGET_ROM_START", "0x10000000")
BOARD = "lsqsh_evb@1os/lsqsh/cpu1"   # 用户需求#8：单核固件一律 1os 编译测试
DRIVER_NAME = "spid_test"               # §12.4 summary 行 `<driver>_test` 前缀
REPORT_BASE = f"{DRIVER_NAME}_report"

TGT_BANNERS = ("CPU1 SPID", "spid_test")

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

PASS, FAIL, MISSING, NOT_RUN, SKIP = "PASS", "FAIL", "MISSING", "NOT_RUN", "SKIP"

TEST_NAMES = [
    "callback_register", "cold_reset_idempotent", "cold_reset_state",
    "device_ready", "init_register_modes", "instance_isolation",
    "interrupt_register", "intr_state_clear_idiom", "master_send_tpm_frame",
    "register_stability",
]


def log(msg):
    print(msg, flush=True)


def build_case(case):
    """复刻 test_common.sh build_single_test：位置参数 src + -DDTC_OVERLAY_FILE=。"""
    build = SAMPLE_ROOT / "tests" / case / "build" / "target"
    src = SAMPLE_ROOT / "cpu1_spid_test"
    overlay = src / "app.overlay"
    try:
        proc = subprocess.run(
            ["west", "build", "-p", "auto", "-b", BOARD, "-d", build.as_posix(),
             src.as_posix(), f"-DDTC_OVERLAY_FILE={overlay.as_posix()}"],
            capture_output=True, text=True, timeout=900)
    except subprocess.TimeoutExpired:
        return False, "build timeout"
    if proc.returncode != 0:
        return False, proc.stdout[-2000:] + proc.stderr[-2000:]
    return True, ""


def reader(ser, lines, stop):
    buf = b""
    while not stop.is_set():
        try:
            chunk = ser.read(256)
        except serial.SerialException:
            break
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = ANSI_RE.sub("", line.decode("utf-8", "replace").rstrip("\r"))
            lines.append(line)


def discover_ports(explicit=None):
    ports = list(explicit) if explicit else []
    if ports:
        return ports
    for p in list_ports.comports():
        hwid = (p.hwid or "").lower()
        desc = (p.description or "").lower()
        if "32bf" in hwid or "cklink" in desc:
            continue
        ports.append(p.device)
    if not ports:
        sys.exit("ERROR: no candidate COM port found (all ports are CKLink?)")
    log(f"[run] candidate UART ports: {', '.join(ports)}")
    return ports


def gdb_flash_once(elf, run_secs):
    """烧录并运行 run_secs 秒，干净退出（interrupt+detach+quit）。

    用户需求#7：单核测试按住 cpu2 复位（APP_CPU_SRST=0x4002200c，
    W1S/W1C：写 2=保持复位、写 1=释放、写 0 无效果），防他方 RAM 残留
    镜像串扰采集；跑完不释放（双核流程自会写 1 释放）。
    """
    cmd = [GDB, "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
           "-ex", "set confirm off", "-ex", "set pagination off",
           "-ex", f"file {elf}", "-ex", "reset",
           "-ex", "monitor flash erase_address 0x10000000 0x10000",
           "-ex", "set memory-write-packet-size 16384",
           "-ex", "set remotewritesize 16384",
           "-ex", "load",
           "-ex", f"set $pc={TARGET_ROM_START}",
           "-ex", "set {int}0x4002200c = 2",
           "-ex", "continue"]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    # ★ 不能用 communicate()：它会关闭 proc.stdin，之后再写 "\x03" 必抛 ValueError
    # → 优雅中断 (interrupt+detach+quit) 直接退化成 proc.kill() → DebugServer 连接残留
    # （qsh-debug §2.3）。改 drain 线程持续排空 stdout， stdin 全程保持可写，\x03 才真发得出去。
    _gdb_lines = []

    def _drain_stdout():
        try:
            for _ln in proc.stdout:
                _gdb_lines.append(_ln)
        except Exception:
            pass
    _drain_t = threading.Thread(target=_drain_stdout, daemon=True)
    _drain_t.start()
    try:
        proc.wait(timeout=run_secs)
    except subprocess.TimeoutExpired:
        try:
            proc.stdin.write("\x03")   # interrupt: 暂停目标
            proc.stdin.flush()
            time.sleep(1.0)
            proc.stdin.write("detach\nquit\n")
            proc.stdin.flush()
            proc.wait(timeout=10)
        except Exception:
            proc.kill()                # 兜底（正常路径不走到）
            proc.wait()
    _drain_t.join(timeout=2)
    out = "".join(_gdb_lines)
    return out


def capture_case(case, tgt_uart=None, timeout=30):
    """烧录（两遍法）+ 串口采集，返回 (lines_by_port, gdb_ok, err)。"""
    build = SAMPLE_ROOT / "tests" / case / "build"
    elf = (build / "target" / "zephyr" / "zephyr.elf").as_posix()
    if not Path(elf).exists():
        return None, False, f"not built: {elf}"

    names = discover_ports([p for p in (tgt_uart,) if p])
    ports = {n: {"lines": [], "stop": threading.Event()} for n in names}

    def start_capture():
        sers = {}
        for n in names:
            try:
                sers[n] = serial.Serial(n, 115200, timeout=0.1)
            except serial.SerialException as e:
                log(f"[run] WARNING: cannot open {n}: {e}")
        threads = []
        for n, ser in sers.items():
            t = threading.Thread(target=reader, args=(ser, ports[n]["lines"], ports[n]["stop"]), daemon=True)
            t.start(); threads.append(t)
        return sers, threads

    # 两遍烧录法（I-cache 规避）；prime 轮不开串口
    out = gdb_flash_once(elf, 20)
    if "Loading section" not in out:
        return None, False, "prime flash failed"
    time.sleep(1)

    sers, threads = start_capture()
    out = gdb_flash_once(elf, timeout)
    time.sleep(0.3)
    for n in ports:
        ports[n]["stop"].set()
    for t in threads:
        t.join(timeout=1)
    for ser in sers.values():
        ser.close()

    ok = "Loading section" in out
    if not ok:
        return ports, False, "capture flash failed"
    return ports, True, ""


def parse_expected_from_coverage():
    """§6.5.2 预期数据（COVERAGE.md 表「预期数据」列）—— 解析收敛在 common/judge.py。

    只认列头含 `预期数据` 的表；转义竖线与裸竖线都不再串列（旧实现用
    `[^|]*` 解析，COVERAGE 里的转义竖线被截成半个单元格，见 §1.4）。"""
    return coverage_expected(SAMPLE_ROOT / "tests" / "COVERAGE.md")


def memory_backfill(case, test_names):
    """dump_results.py --json 读内存兜底（§12.3）。失败返回 None → mem-unavailable。"""
    script = SAMPLE_ROOT / "tests" / "scripts" / "dump_results.py"
    try:
        proc = subprocess.run([sys.executable, str(script), "--case", case, "--json"],
                              capture_output=True, text=True, timeout=60)
    except (subprocess.TimeoutExpired, OSError):
        return None
    if proc.returncode != 0:
        return None
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError:
        return None


def evaluate_case(case, lines, test_names, mem_data):
    """单核判定：ztest 原生输出优先 → 内存兜底 → NOT_RUN（§12.3）。"""
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
            if mem == "PASS":
                st, note = PASS, "src=mem"
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
        '# TPM SPI slave (PFR/TPM) 驱动测试报告',
        "",
        *format_report_header(
            DRIVER_NAME, case, BOARD,
            date=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            env=env_line, execution=execution, mem_line=mem_line,
            result=f"{'PASS' if case_ok else 'FAIL'} ({passed}/{ran})"),
        '- 接线: tpm_spis1(PI04-07) <-> spi1(PA00-PA02+CS) 外部 4 线跳线（master 发帧用例）；其余零接线',
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
        ran = sum(1 for n in TEST_NAMES if results[n] not in (NOT_RUN, SKIP))
        n_skip = sum(1 for n in TEST_NAMES if results[n] == SKIP)
        mem_line_before = mem_state_line(mem_state, mem_note)
        # R7：全项证据都来自 mem ⇒ 不得判 PASS；理由串挂 mem_note（三处同源）
        mem_only = mem_only_evidence(notes.values())
        if mem_only:
            mem_note = ((mem_note + "；") if mem_note else "") + MEM_ONLY_NOTE
        verdict = judge_case(results, passed, ran, execution, TEST_NAMES)
        return {"case": ci.case, "mem_none": mem is None,
                "mem_state": mem_state, "mem_note": mem_note,
                "mem_line": mem_state_line(mem_state, mem_note),
                "mem_line_before": mem_line_before, "mem_only": mem_only,
                "results": results, "infos": infos, "reasons": reasons,
                "notes": notes, "execution": execution, "passed": passed,
                "ran": ran, "n_skip": n_skip, "verdict": verdict,
                "exec_report": verdict["exec"], "missing": verdict["missing"],
                "case_ok": verdict["ok"] and not mem_only}

    def _console(jl):
        b = jl[0]
        if b["mem_none"]:
            log("[run] memory backfill: mem-unavailable (串口 token 缺失时按 §12.3 判 FAIL)")
        log(f"[run] {b['mem_line_before']}")
        log(f"--- {b['case']} results ---")
        for name in TEST_NAMES:
            tag = f"  {name:<24} {b['results'][name]}"
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

    for case in cases:
        log(f"===== CASE {case} =====")
        if not args.skip_build:
            ok, err = build_case(case)
            if not ok:
                log(f"[run] BUILD FAILED: {err}")
                n_fail += 1
                continue
        ports, ok, err = capture_case(case, args.tgt_uart, args.timeout)
        if not ok:
            log(f"[run] CAPTURE FAILED: {err}")
            n_fail += 1
            continue
        lines = []
        for n, p in ports.items():
            text = "\n".join(p["lines"])
            if any(b in text for b in TGT_BANNERS):
                lines = p["lines"]
        if not lines:
            log(f"[run] WARNING: no TGT banner lines captured ({case})")

        # L1→L2 边界（L0 归档形状）：采集行按归档形状归一；mem 兜底是 L1 侧
        # 产物（读目标内存，不进归档）⇒ 作为独立入参交给 L2（common/process.py）。
        lines_by_core = {"TGT": lines}
        if not args.no_archive:
            session_dir = session_dir or (
                Path(args.session_dir) if args.session_dir
                else allocate_session_dir(REPORTS_ROOT, date_str))
            archive_serial(case_dir_for(session_dir, DRIVER_NAME, case), case,
                           lines_by_core, board=BOARD,
                           ports={"TGT": args.tgt_uart}, timeout=args.timeout,
                           script=__file__)
        mem = memory_backfill(case, test_names)
        outcome = process.run(_L2_SPEC, case=case, lines=lines_by_core, mem=mem,
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
