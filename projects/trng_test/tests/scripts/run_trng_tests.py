#!/usr/bin/env python3
"""TRNG single-core regression runner: build cases, flash, capture, evaluate,
archive reports (CLAUDE.md §12/§13).

单核工程版：从 i3c_dualcore_test 样板 run_i3c_tests.py 改造，去掉 controller 核。
每项测试只需 TGT 单 PASS 即过（单核无对侧核）。

流程（每 case）：
    1. 编译（west build -p auto，CLAUDE.md 构建规范）
    2. 烧录 + 串口采集（dbg_run.py 逻辑内联）
    3. token 解析（START/PASS/FAIL + INFO + SUMMARY + DONE）
    4. 判定（§13.5 可归因 reason；串口缺项用 dump_results.py --json 内存兜底，§12.3）
    5. 报告归档（§12）：reports/<YYYY-MM-DD>/trng_test_report.{md,json} + summary.md 索引行

Usage:
    python scripts/run_trng_tests.py                 # 全流程 + 归档
    python scripts/run_trng_tests.py --no-archive   # 只跑不改报告
    python scripts/run_trng_tests.py --skip-build   # 复用现有构建
    python scripts/run_trng_tests.py --case-only case_default  # 只跑指定 case
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

import serial
import serial.tools.list_ports as list_ports

# tests/scripts/run_trng_tests.py -> parents[2] = trng_test 工程根（注意不是 parents[1]）
SAMPLE_ROOT = Path(__file__).resolve().parents[2]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"   # 810_driver_test/reports/
GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
TARGET_ROM_START = "0x100c0000"
BOARD = "lsqsh_evb@2os/lsqsh/cpu1"
DRIVER_NAME = "trng_test"          # §12.4 summary 行 `<driver>_test` 前缀
REPORT_BASE = f"{DRIVER_NAME}_report"   # trng_test_report.{md,json}（避免 _test_test 重复）

TGT_BANNERS = ("CPU1 TRNG", "trng_test", "TRNG test")

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
TOKEN_RE = re.compile(r"TRNG_TEST:(TGT):(START|PASS|FAIL):(\w+)(?::(.*))?\s*")
INFO_RE = re.compile(r"TRNG_TEST:(?:TGT):INFO:(\w+):([\w.=,\-]+)\s*")
SUMMARY_RE = re.compile(r"TRNG_TEST:(?:TGT):SUMMARY:(.*)")
DONE_RE = re.compile(r"TRNG_TEST:DONE:(PASS|FAIL)")

# 判定状态：src 表示来源（token / mem / missing）
PASS, FAIL, MISSING, NOT_RUN = "PASS", "FAIL", "MISSING", "NOT_RUN"


def log(msg):
    print(msg, flush=True)


def build_case(case):
    """复刻 test_common.sh build_single_test：位置参数 src + -DDTC_OVERLAY_FILE=。"""
    build = SAMPLE_ROOT / "tests" / case / "build"
    src = SAMPLE_ROOT / "cpu1_trng_test"
    overlay = src / "app.overlay"
    try:
        proc = subprocess.run(
            ["west", "build", "-p", "auto", "-b", BOARD, "-d", build.as_posix(),
             src.as_posix(), f"-DDTC_OVERLAY_FILE={overlay.as_posix()}"],
            capture_output=True, text=True, timeout=600)
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
    """烧录并运行 run_secs 秒，干净退出（interrupt+detach+quit，§10.1）。"""
    cmd = [GDB, "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
           "-ex", "set confirm off", "-ex", "set pagination off",
           "-ex", f"file {elf}", "-ex", "reset", "-ex", "load",
           "-ex", f"set $pc={TARGET_ROM_START}", "-ex", "continue"]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        out, _ = proc.communicate(timeout=run_secs)
    except subprocess.TimeoutExpired:
        try:
            proc.stdin.write("\x03"); proc.stdin.flush(); time.sleep(1.0)
            proc.stdin.write("detach\nquit\n"); proc.stdin.flush()
            out, _ = proc.communicate(timeout=10)
        except Exception:
            proc.kill(); out, _ = proc.communicate()
    return out


def capture_case(case, tgt_uart=None, timeout=30):
    """烧录（两遍法）+ 串口采集，返回 (lines_by_port, gdb_ok)。"""
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


def parse_tokens(lines):
    """返回 (outcomes, done, infos, reasons)。

    outcomes[name] = "PASS|FAIL|MISSING"（TGT 单核，无 per-core dict）
    infos[name] = {var: value}（INFO token，§13.4 数值上报）
    """
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
            continue
    return outcomes, done, infos, reasons


def parse_expected_from_coverage():
    """从 COVERAGE.md 提取每项的预期数据（表格第 4 列）。"""
    cov = SAMPLE_ROOT / "tests" / "COVERAGE.md"
    if not cov.exists():
        return {}
    expected = {}
    for line in cov.read_text(encoding="utf-8").splitlines():
        m = re.match(r"\|\s*(\w+)\s*\|\s*([^|]*)\s*\|\s*([^|]*)\s*\|\s*([^|]*)\s*\|", line)
        if m:
            expected[m.group(1).strip()] = m.group(4).strip()
    return expected


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
    """单核判定：token 优先 → 内存兜底 → NOT_RUN。返回 (results, infos, reasons, notes)。"""
    outcomes, done, infos, reasons = parse_tokens(lines)
    results = {}
    notes = {}
    for name in test_names:
        st = outcomes.get(name, MISSING)
        if st == MISSING and mem_data is not None:
            mem = mem_data.get("TGT", {}).get(name)
            if mem == "PASS":
                st = PASS
                notes[name] = "src=mem"
                reasons.setdefault(name, mem_data.get("reasons", {}).get(name, ""))
            elif mem == "FAIL":
                st = FAIL
                notes[name] = "src=mem"
                reasons.setdefault(name, mem_data.get("reasons", {}).get(name, ""))
            elif mem == "NOT_RUN":
                st = NOT_RUN
                notes[name] = "src=mem(NOT_RUN)"
            else:
                notes[name] = "mem-unavailable"
        elif st == MISSING and mem_data is None:
            st = FAIL
            notes[name] = "mem-unavailable"
        else:
            notes[name] = "src=token"
        results[name] = st
    return results, infos, reasons, notes


def write_markdown_report(case, results, infos, reasons, notes, expected, date_dir, env_line):
    base = date_dir / f"{REPORT_BASE}.md"
    # 追加新 run 到同一天报告（首次运行创建）
    mode = "a" if base.exists() else "w"
    lines = []
    if mode == "w":
        lines += [
            "# TRNG 驱动测试报告",
            "",
            f"- 工程: {DRIVER_NAME}",
            f"- 日期: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
            f"- 板级: {BOARD}",
            f"- 环境: {env_line}",
            "",
            "## 测试项结果",
            "",
            "| 测试项 | 结果 | 预期(COVERAGE) | 实际(INFO) | 失败原因 | 来源 |",
            "|---|---|---|---|---|---|",
        ]
    lines.append(f"- Run @ {datetime.now().strftime('%H:%M:%S')} (case {case})")
    for name in results:
        st = results[name]
        if st == PASS:
            st_txt, reason = "✅ PASS", ""
        elif st == FAIL:
            st_txt, reason = "❌ FAIL", reasons.get(name, "")
        elif st == NOT_RUN:
            st_txt, reason = "⬜ NOT_RUN", ""
        else:
            st_txt, reason = "⚠️ " + st, reasons.get(name, "")
        info = infos.get(name, {}).get("value", "")
        exp = expected.get(name, "")
        lines.append(f"| {name} | {st_txt} | {exp} | {info} | {reason} | {notes.get(name, '')} |")
    lines.append("")
    base.parent.mkdir(parents=True, exist_ok=True)
    with open(base, "a" if mode == "a" else "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def write_json_report(case, results, infos, reasons, notes, date_dir, env_line):
    base = date_dir / f"{REPORT_BASE}.json"
    data = {
        "driver": DRIVER_NAME, "case": case, "board": BOARD,
        "env": env_line, "timestamp": datetime.now().isoformat(timespec="seconds"),
        "results": {name: st for name, st in results.items()},
        "infos": infos, "reasons": reasons, "notes": notes,
    }
    old = []
    if base.exists():
        try:
            old = json.loads(base.read_text(encoding="utf-8"))
            if not isinstance(old, list):
                old = []
        except json.JSONDecodeError:
            old = []
    old.append(data)
    base.write_text(json.dumps(old, ensure_ascii=False, indent=2), encoding="utf-8")


def update_summary(date_dir, results, env_line, case):
    """§12.4 summary.md 索引：按 `| trng_test |` 前缀替换行（含日期/结果/通过数/备注）。"""
    summary = date_dir / "summary.md"
    header = "| 工程 | 日期时间 | 结果 | 通过/运行 | 备注 |"
    passed = sum(1 for st in results.values() if st == PASS)
    ran = sum(1 for st in results.values() if st != NOT_RUN)
    row = (f"| {DRIVER_NAME} | {datetime.now().strftime('%Y-%m-%d %H:%M:%S')} "
           f"| {'PASS' if passed == ran and ran > 0 else 'FAIL'} "
           f"| {passed}/{ran} | env={env_line} |")
    if summary.exists():
        text = summary.read_text(encoding="utf-8").splitlines()
        idx = next((i for i, l in enumerate(text) if l.startswith(f"| {DRIVER_NAME} |")), None)
        if idx is not None:
            text[idx] = row
        else:
            text.append(row)
        summary.write_text("\n".join(text) + "\n", encoding="utf-8")
    else:
        summary.write_text(header + "\n" + "|---|---|---|---|---|\n" + row + "\n", encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-archive", action="store_true", help="只跑不改报告")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--case-only", default=None)
    ap.add_argument("--tgt-uart", default=None)
    ap.add_argument("--timeout", type=float, default=30)
    args = ap.parse_args()

    cases = [args.case_only] if args.case_only else ["case_default"]
    test_names = ["basic_16", "not_all_zero", "not_all_ff", "isr_busywait",
                  "large_1024", "invalid_args", "monobit_8k", "distinct_32",
                  "throughput_64k"]
    expected = parse_expected_from_coverage()
    env_line = f"tgt_uart={args.tgt_uart or 'auto'}, timeout={args.timeout}s"

    for case in cases:
        log(f"===== CASE {case} =====")
        if not args.skip_build:
            ok, err = build_case(case)
            if not ok:
                log(f"[run] BUILD FAILED: {err}")
                continue
        ports, ok, err = capture_case(case, args.tgt_uart, args.timeout)
        if not ok:
            log(f"[run] CAPTURE FAILED: {err}")
            continue
        lines = []
        for n, p in ports.items():
            text = "\n".join(p["lines"])
            if any(b in text for b in TGT_BANNERS):
                lines = p["lines"]
        if not lines:
            log(f"[run] WARNING: no TGT banner lines captured ({case})")

        mem = memory_backfill(case, test_names)
        if mem is None:
            log("[run] memory backfill: mem-unavailable (串口 token 缺失时按 §12.3 判 FAIL)")
        results, infos, reasons, notes = evaluate_case(case, lines, test_names, mem)

        log(f"--- {case} results ---")
        passed = ran = 0
        for name in test_names:
            st = results[name]
            if st == PASS:
                passed += 1
            if st != NOT_RUN:
                ran += 1
            tag = f"  {name:<20} {st}"
            if reasons.get(name):
                tag += f"  ({reasons[name]})"
            log(tag)
        done = "?" if not lines else "DONE" if DONE_RE.search("\n".join(lines)) else "no-DONE"
        log(f"-> {passed}/{ran} PASS, done={done}")

        if not args.no_archive:
            date_dir = REPORTS_ROOT / datetime.now().strftime("%Y-%m-%d")
            write_markdown_report(case, results, infos, reasons, notes, expected,
                                  date_dir, env_line)
            write_json_report(case, results, infos, reasons, notes, date_dir, env_line)
            update_summary(date_dir, results, env_line, case)
            log(f"[run] archived -> {date_dir / (REPORT_BASE + '.md')}")
        else:
            log("[run] --no-archive: 未写报告")

    log("===== DONE =====")


if __name__ == "__main__":
    main()
