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
import re
import subprocess
import sys
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

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
TARGET_ROM_START = "0x100c0000"
BOARD = "lsqsh_evb@2os/lsqsh/cpu1"
DRIVER_NAME = "rtc_ls_test"
REPORT_BASE = f"{DRIVER_NAME}_report"

TGT_BANNERS = ("CPU1 RTC", "rtc_test", "RTC test")

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
TOKEN_RE = re.compile(r"RTC_TEST:(TGT):(START|PASS|FAIL):(\w+)(?::(.*))?\s*")
INFO_RE = re.compile(r"RTC_TEST:(?:TGT):INFO:(\w+):([\w.=,\-]+)\s*")
DONE_RE = re.compile(r"RTC_TEST:DONE:(PASS|FAIL)")

PASS, FAIL, MISSING, NOT_RUN = "PASS", "FAIL", "MISSING", "NOT_RUN"

TEST_NAMES = [
    "set_get_roundtrip", "time_advances", "invalid_set_args", "alarm_set_get",
    "alarm_fires", "alarm_invalid_args",
    "calibration_roundtrip", "calibration_negative", "calibration_null_arg",
    "calendar_rollover",
]


def log(msg):
    print(msg, flush=True)


def build_case(case):
    build = SAMPLE_ROOT / "tests" / case / "build" / "target"
    src = SAMPLE_ROOT / "cpu1_rtc_test"
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
    cmd = [GDB, "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
           "-ex", "set confirm off", "-ex", "set pagination off",
           "-ex", f"file {elf}", "-ex", "reset",
           "-ex", "set memory-write-packet-size 16384",
           "-ex", "set remotewritesize 16384",
           "-ex", "load",
           "-ex", f"set $pc={TARGET_ROM_START}",
           "-ex", "continue"]
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


def capture_case(case, tgt_uart=None, timeout=150):
    build = SAMPLE_ROOT / "tests" / case / "build" / "target"
    elf = (build / "zephyr" / "zephyr.elf").as_posix()
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
    cov = SAMPLE_ROOT / "tests" / "COVERAGE.md"
    if not cov.exists():
        return {}
    expected = {}
    for line in cov.read_text(encoding="utf-8").splitlines():
        m = re.match(r"\|\s*(\w+)\s*\|\s*([^|]*)\|\s*([^|]*)\|\s*([^|]*)\|\s*([^|]*)\|", line)
        if m:
            expected[m.group(1).strip()] = m.group(4).strip()
    return expected


def memory_backfill(case):
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


def evaluate_case(lines, mem_data):
    outcomes, done, infos, reasons = parse_tokens(lines)
    results, notes = {}, {}
    for name in TEST_NAMES:
        st = outcomes.get(name, MISSING)
        if st == MISSING and mem_data is not None:
            mem = mem_data.get("TGT", {}).get(name)
            if mem == "PASS":
                st, notes[name] = PASS, "src=mem"
            elif mem == "FAIL":
                st, notes[name] = FAIL, "src=mem"
                reasons.setdefault(name, mem_data.get("reasons", {}).get(name, ""))
            elif mem == "NOT_RUN":
                st, notes[name] = NOT_RUN, "src=mem(NOT_RUN)"
            else:
                st, notes[name] = FAIL, "mem-unavailable"
        elif st == MISSING and mem_data is None:
            st, notes[name] = FAIL, "mem-unavailable"
        else:
            notes[name] = "src=token"
        results[name] = st
    return results, done, infos, reasons, notes


def write_markdown_report(results, infos, reasons, notes, expected, date_dir, env_line):
    base = date_dir / f"{REPORT_BASE}.md"
    lines = [
        "# RTC (rtc_ls) 驱动测试报告",
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
    for name in TEST_NAMES:
        st = results[name]
        if st == PASS:
            st_txt, reason = "PASS", ""
        elif st == FAIL:
            st_txt, reason = "FAIL", reasons.get(name, "")
        elif st == NOT_RUN:
            st_txt, reason = "NOT_RUN", ""
        else:
            st_txt, reason = st, reasons.get(name, "")
        info = infos.get(name, {}).get("value", "")
        exp = expected.get(name, "")
        lines.append(f"| {name} | {st_txt} | {exp} | {info} | {reason} | {notes.get(name, '')} |")
    base.parent.mkdir(parents=True, exist_ok=True)
    base.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_json_report(results, infos, reasons, notes, date_dir, env_line):
    base = date_dir / f"{REPORT_BASE}.json"
    data = {
        "driver": DRIVER_NAME, "board": BOARD,
        "env": env_line, "timestamp": datetime.now().isoformat(timespec="seconds"),
        "results": results, "infos": infos, "reasons": reasons, "notes": notes,
    }
    base.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


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

    expected = parse_expected_from_coverage()
    env_line = f"tgt_uart={args.tgt_uart or 'auto'}, timeout={args.timeout}s"
    date_str = datetime.now().strftime("%Y-%m-%d")
    n_fail = 0

    log(f"===== CASE {args.case} =====")
    if not args.skip_build:
        ok, err = build_case(args.case)
        if not ok:
            log(f"[run] BUILD FAILED: {err}")
            return 1

    ports, ok, err = capture_case(args.case, args.tgt_uart, args.timeout)
    if not ok:
        log(f"[run] CAPTURE FAILED: {err}")
        return 1

    lines = []
    for p in ports.values():
        text = "\n".join(p["lines"])
        if any(b in text for b in TGT_BANNERS):
            lines = p["lines"]
    if not lines:
        log("[run] WARNING: no TGT banner lines captured")

    mem = memory_backfill(args.case)
    results, done, infos, reasons, notes = evaluate_case(lines, mem)

    log(f"--- {args.case} results ---")
    passed = ran = 0
    for name in TEST_NAMES:
        st = results[name]
        if st == PASS:
            passed += 1
        if st != NOT_RUN:
            ran += 1
        tag = f"  {name:<28} {st}"
        if reasons.get(name):
            tag += f"  ({reasons[name]})"
        log(tag)
    log(f"-> {passed}/{ran} PASS, done={done or 'no-DONE'}")

    case_ok = (passed == ran and ran == len(TEST_NAMES) and done == "PASS")
    if not case_ok:
        n_fail = 1

    if not args.no_archive:
        session_dir = Path(args.session_dir) if args.session_dir else \
            allocate_session_dir(REPORTS_ROOT, date_str)
        case_dir = case_dir_for(session_dir, DRIVER_NAME, args.case)
        write_markdown_report(results, infos, reasons, notes, expected, case_dir, env_line)
        write_json_report(results, infos, reasons, notes, case_dir, env_line)
        update_summary(session_dir, f"{DRIVER_NAME}_{args.case}",
                       "PASS" if case_ok else "FAIL", passed, ran,
                       f"{case_dir.name}/{REPORT_BASE}.md")
        log(f"[run] archived -> {case_dir / (REPORT_BASE + '.md')}")

    log("===== DONE =====")
    return n_fail


if __name__ == "__main__":
    sys.exit(main())
