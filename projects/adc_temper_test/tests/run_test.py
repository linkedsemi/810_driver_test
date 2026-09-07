#!/usr/bin/env python3
"""ADC temperature sensor single-core regression runner."""
import argparse
import json
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

SAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
from ztest_report import parse_ztest
from report_dir import allocate_session_dir, case_dir_for, update_summary

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
TARGET_ROM_START = "0x100c0000"
BOARD = "lsqsh_evb@2os/lsqsh/cpu1"
DRIVER_NAME = "adc_temper_test"
REPORT_BASE = f"{DRIVER_NAME}_report"


def log(msg):
    print(msg, flush=True)


def _find_git_bash():
    for p in [r"C:\Program Files\Git\bin\bash.exe",
              r"C:\Program Files (x86)\Git\bin\bash.exe"]:
        if Path(p).exists():
            return p
    return "bash"


BASH = _find_git_bash()
TESTS_DIR = SAMPLE_ROOT / "tests"


def _find_tgt_uart():
    # Prefer CH342 Serial-B (target uart1) over Serial-A
    for p in list_ports.comports():
        desc = (p.description or "").lower()
        if "ch342" in desc and "serial-b" in desc:
            return p.device
    for p in list_ports.comports():
        desc = (p.description or "").lower()
        if "ch342" in desc or "ch34" in desc:
            return p.device
    ports = [p.device for p in list_ports.comports()
             if "32bf" not in (p.hwid or "").lower()
             and "cklink" not in (p.description or "").lower()]
    return ports[0] if ports else None


def _restart_debugserver():
    subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                   capture_output=True)
    time.sleep(2)
    ds = Path(r"C:\uaer_app\debug_server\bin\DebugServerConsole.exe")
    subprocess.Popen([str(ds)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(30):
        time.sleep(1)
        try:
            s = serial.Serial("COM99", 115200, timeout=0.1)
            s.close()
        except Exception:
            pass
        try:
            out = subprocess.run(["netstat", "-ano"], capture_output=True, text=True, timeout=10).stdout
            for line in out.splitlines():
                if ":1025" in line and "LISTENING" in line:
                    log("[ENV] DebugServer ready")
                    return
        except Exception:
            pass
    log("[ENV] WARNING: DebugServer may not be ready")


def _clean_gdb_residue():
    subprocess.run(["taskkill", "/F", "/IM", "riscv64-unknown-elf-gdb.exe"],
                   capture_output=True)
    time.sleep(1)


def clean_gdb_residue():
    _clean_gdb_residue()


def restart_debugserver():
    _restart_debugserver()
    time.sleep(5)


def _kill_proc_group(proc):
    try:
        import signal
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                       capture_output=True)
    except Exception:
        proc.kill()
    try:
        proc.wait(timeout=5)
    except Exception:
        proc.kill()


def _spawn_test_run(case_dir, build_dir):
    test_sh = case_dir / "test.sh"
    env = dict(__import__("os").environ)
    env["SKIP_LOAD"] = "y"
    env["AGENT_RUN"] = "y"
    env["I3C_TEST_ROOT"] = str(SAMPLE_ROOT).replace("\\", "/")
    proc = subprocess.Popen(
        [BASH, str(test_sh).replace("\\", "/"), "-r"],
        cwd=str(SAMPLE_ROOT).replace("\\", "/"),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    lines = []
    def _reader():
        for line in proc.stdout:
            lines.append(line.rstrip("\r\n"))
            print(line, end="", flush=True)
    t = threading.Thread(target=_reader, daemon=True)
    t.start()
    return proc, lines, t


class SerialCapture:
    def __init__(self, port, baud=115200):
        self.port = port
        self.baud = baud
        self.lines = []
        self._stop = threading.Event()
        self._thread = None
        self._ser = None
        self.error = None

    def start(self):
        try:
            self._ser = serial.Serial(self.port, self.baud, timeout=0.1)
        except Exception as exc:
            self.error = f"failed to open {self.port}: {exc}"
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        while not self._stop.is_set():
            try:
                data = self._ser.readline()
            except Exception:
                break
            if data:
                line = data.decode("utf-8", errors="replace").rstrip("\r\n")
                self.lines.append((time.time(), line))

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2.0)
        if self._ser and self._ser.is_open:
            self._ser.close()

    @property
    def text(self):
        return "\n".join(l for _, l in self.lines)


def run_case(case_name, tgt_uart, timeout=60):
    build_dir = TESTS_DIR / case_name / "build" / "target"
    elf = build_dir / "zephyr" / "zephyr.elf"
    if not elf.exists():
        log(f"[ERROR] ELF not found: {elf}")
        return None

    cap = SerialCapture(tgt_uart)
    cap.start()
    if cap.error:
        log(f"[ERROR] {cap.error}")
        return None

    proc, proc_lines, thread = _spawn_test_run(TESTS_DIR / case_name, build_dir)

    start = time.time()
    last_activity = start
    while time.time() - start < timeout:
        if proc.poll() is not None:
            break
        time.sleep(0.3)

    _kill_proc_group(proc)
    thread.join(timeout=3.0)
    cap.stop()

    return {
        "case": case_name,
        "ctrl_lines": [],
        "tgt_lines": cap.lines,
        "proc_lines": proc_lines,
        "timed_out": (time.time() - start >= timeout),
    }


def evaluate_case(case_name, tgt_lines):
    view = parse_ztest(
        [l for _, l in tgt_lines],
        ctrl_banners=[], tgt_banners=["TGT"]
    )
    tests = view.get("tests", {})
    items = [{"name": n, "result": v.get("result"), "reason": v.get("reason", "")}
             for n, v in tests.items()]
    execution = view.get("execution")
    all_pass = execution == "SUCCESSFUL" and all(i["result"] == "PASS" for i in items)
    return {"case": case_name, "result": "PASS" if all_pass else "FAIL", "items": items}


def print_report(results):
    print("\n--- ADC temper test results ---")
    for r in results:
        status = r.get("result", "UNKNOWN")
        items = r.get("items", [])
        passed = sum(1 for i in items if i.get("result") == "PASS")
        total = len(items)
        print(f"## {r['case']}: {status} ({passed}/{total})")
        for item in items:
            name = item.get("name", "?")
            res = item.get("result", "?")
            reason = item.get("reason", "")
            print(f"  {name}: {res} {reason}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", default="case_default")
    parser.add_argument("--tgt-uart", default=None)
    parser.add_argument("--session-dir", default=None)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--no-archive", action="store_true")
    parser.add_argument("--case-timeout", type=int, default=60)
    args = parser.parse_args()

    tgt_uart = args.tgt_uart or _find_tgt_uart()
    if not tgt_uart:
        log("ERROR: no target UART found")
        return 1
    log(f"Target UART: {tgt_uart}")

    date_str = datetime.now().strftime("%Y-%m-%d")
    session = Path(args.session_dir) if args.session_dir else allocate_session_dir(REPORTS_ROOT, date_str)
    logs = session / "logs"
    logs.mkdir(parents=True, exist_ok=True)

    if not args.skip_build:
        log(f"[BUILD] {args.case}")
        build_dir = TESTS_DIR / args.case / "build" / "target"
        test_sh = TESTS_DIR / args.case / "test.sh"
        env = dict(__import__("os").environ)
        env["I3C_TEST_ROOT"] = str(SAMPLE_ROOT).replace("\\", "/")
        rc = subprocess.call([BASH, str(test_sh).replace("\\", "/")], cwd=str(TESTS_DIR / args.case).replace("\\", "/"), env=env)
        if rc != 0:
            log(f"[ERROR] build failed rc={rc}")
            return 1

    raw = run_case(args.case, tgt_uart, args.case_timeout)
    if raw is None:
        return 1

    result = evaluate_case(args.case, raw["tgt_lines"])
    print_report([result])

    if not args.no_archive:
        cdir = case_dir_for(session, DRIVER_NAME, args.case)
        cdir.mkdir(parents=True, exist_ok=True)
        report_json = cdir / f"{REPORT_BASE}.json"
        report_json.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
        log(f"[run] archived -> {report_json}")

    return 0 if result.get("result") == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
