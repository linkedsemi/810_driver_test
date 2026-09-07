#!/usr/bin/env python3
"""MBOX dual-core regression runner (ztest-native).

Runs mbox_test cases on the connected controller/target UARTs, parses the
ztest-native output (common/ztest_report.parse_ztest) and produces an
archived JSON report under 810_driver_test/reports/<YYYY-MM-DD>-<idx>/.

双核工程：controller (cpu2, uart5) 和 target (cpu1, uart1) 各自运行测试，
结果需要两核都 PASS 才算通过。

Environment:
    I3C_CTRL_UART          Controller UART port (e.g. COM15)
    I3C_TGT_UART           Target UART port (e.g. COM16)
    GDB_SERVER_HOST        GDB server host (default: localhost)
    GDB_SERVER_PORT        GDB server port (default: 1025)
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

SAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
from ztest_report import parse_ztest
from report_dir import allocate_session_dir, case_dir_for, update_summary

GDB = shutil.which("riscv64-unknown-elf-gdb") or "riscv64-unknown-elf-gdb"
GDB_HOST = os.environ.get("GDB_SERVER_HOST", "127.0.0.1")
GDB_PORT = os.environ.get("GDB_SERVER_PORT", "1025")
TARGET_ROM_START = "0x100c0000"
DRIVER_NAME = "mbox_test"
REPORT_BASE = f"{DRIVER_NAME}_report"

# Banner patterns for role identification
CTRL_BANNERS = ["CTRL", "cpu2", "controller"]
TGT_BANNERS = ["TGT", "cpu1", "target"]


def log(msg):
    print(msg, flush=True)


def _find_git_bash():
    """Find Git Bash, avoiding WSL bash."""
    candidates = [
        os.environ.get("GIT_BASH"),
        shutil.which("bash"),
        r"C:\Program Files\Git\bin\bash.exe",
        r"C:\Program Files\Git\usr\bin\bash.exe",
        r"C:\Program Files (x86)\Git\bin\bash.exe",
    ]
    for c in candidates:
        if not c or not Path(c).exists():
            continue
        low = c.lower()
        if "system32" in low or "windowsapps" in low:
            continue
        return c
    return "bash"


BASH = _find_git_bash()
TESTS_DIR = SAMPLE_ROOT / "tests"


def _find_ctrl_uart():
    """Find controller UART (MCP2200 or CH342 Serial-A)."""
    for p in list_ports.comports():
        desc = (p.description or "").lower()
        if "mcp2200" in desc:
            return p.device
    for p in list_ports.comports():
        desc = (p.description or "").lower()
        if "ch342" in desc and "serial-a" in desc:
            return p.device
    return None


def _find_tgt_uart():
    """Find target UART (CH342 Serial-B preferred)."""
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


def _netstat_1025():
    """Check netstat for port 1025 status."""
    rows = []
    try:
        out = subprocess.run(["netstat", "-ano"], capture_output=True,
                             text=True, timeout=15).stdout
        for line in out.splitlines():
            if ":1025" not in line:
                continue
            parts = line.split()
            if len(parts) >= 5 and parts[0] == "TCP":
                rows.append((parts[3], parts[4], parts[1]))
    except Exception:
        pass
    return rows


def _kill_residual_gdb():
    """Kill residual gdb processes holding port 1025."""
    try:
        pids = set()
        for state, local_pid, _ in _netstat_1025():
            if state == "ESTABLISHED" and local_pid.isdigit():
                pids.add(local_pid)
        for pid in pids:
            ti = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/FO", "CSV"],
                                capture_output=True, text=True, timeout=15).stdout
            if "gdb" in ti.lower():
                subprocess.run(["taskkill", "/F", "/PID", pid],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=15)
    except Exception:
        pass


def _restart_debugserver():
    """Restart DebugServer and wait for ready."""
    subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
    time.sleep(1)
    ds = Path(r"C:\uaer_app\debug_server\bin\DebugServerConsole.exe")
    if not ds.exists():
        log(f"[ENV] WARNING: DebugServer not found at {ds}")
        return
    subprocess.Popen([str(ds)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(30):
        time.sleep(1)
        for state, pid, _ in _netstat_1025():
            if state == "LISTENING":
                log("[ENV] DebugServer ready")
                return
    log("[ENV] WARNING: DebugServer may not be ready")


def _clean_gdb_residue():
    """Kill residual gdb processes."""
    _kill_residual_gdb()
    subprocess.run(["taskkill", "/F", "/IM", "riscv64-unknown-elf-gdb.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
    time.sleep(1)


def clean_gdb_residue():
    _clean_gdb_residue()


def restart_debugserver():
    _restart_debugserver()
    time.sleep(5)


def _kill_proc_group(proc):
    """Kill process and its children."""
    try:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
    except Exception:
        proc.kill()
    try:
        proc.wait(timeout=5)
    except Exception:
        proc.kill()


class SerialCapture:
    """Capture serial output in a background thread."""

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


def _spawn_test_run(case_dir, build_dir):
    """Spawn test.sh -r in agent mode."""
    test_sh = case_dir / "test.sh"
    env = dict(os.environ)
    env["SKIP_LOAD"] = "y"
    env["AGENT_RUN"] = "y"
    env["I3C_TEST_ROOT"] = str(SAMPLE_ROOT)
    proc = subprocess.Popen(
        [BASH, str(test_sh), "-r"],
        cwd=str(case_dir),
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


def run_case(case_name, ctrl_uart, tgt_uart, timeout=120):
    """Run a single case on dual-core setup.

    Returns dict with ctrl_lines, tgt_lines, proc_lines, timed_out.
    """
    build_dir = TESTS_DIR / case_name / "build"
    target_elf = build_dir / "target" / "zephyr" / "zephyr.elf"
    controller_elf = build_dir / "controller" / "zephyr" / "zephyr.elf"

    if not target_elf.exists():
        log(f"[ERROR] Target ELF not found: {target_elf}")
        return None
    if not controller_elf.exists():
        log(f"[ERROR] Controller ELF not found: {controller_elf}")
        return None

    # Start capturing from both UARTs
    ctrl_cap = SerialCapture(ctrl_uart)
    tgt_cap = SerialCapture(tgt_uart)
    ctrl_cap.start()
    tgt_cap.start()

    if ctrl_cap.error:
        log(f"[ERROR] {ctrl_cap.error}")
        return None
    if tgt_cap.error:
        log(f"[ERROR] {tgt_cap.error}")
        return None

    proc, proc_lines, thread = _spawn_test_run(TESTS_DIR / case_name, build_dir)

    start = time.time()
    last_activity = start
    while time.time() - start < timeout:
        if proc.poll() is not None:
            break
        # Check for early completion (PROJECT EXECUTION seen on both cores)
        ctrl_text = ctrl_cap.text
        tgt_text = tgt_cap.text
        if ("PROJECT EXECUTION" in ctrl_text and "PROJECT EXECUTION" in tgt_text):
            # Both cores completed, wait a bit for final output
            time.sleep(1)
            break
        time.sleep(0.3)

    _kill_proc_group(proc)
    thread.join(timeout=3.0)
    ctrl_cap.stop()
    tgt_cap.stop()

    return {
        "case": case_name,
        "ctrl_lines": ctrl_cap.lines,
        "tgt_lines": tgt_cap.lines,
        "proc_lines": proc_lines,
        "timed_out": (time.time() - start >= timeout),
    }


def evaluate_case(case_name, ctrl_lines, tgt_lines):
    """Evaluate dual-core test results using parse_ztest.

    - Test item result = same item PASS on both cores (CTRL + TGT)
    - Case result = CTRL execution "SUCCESSFUL" + all items dual-core PASS
    """
    ctrl_p = parse_ztest(
        [l for _, l in ctrl_lines],
        ctrl_banners=CTRL_BANNERS, tgt_banners=TGT_BANNERS
    )
    tgt_p = parse_ztest(
        [l for _, l in tgt_lines],
        ctrl_banners=CTRL_BANNERS, tgt_banners=TGT_BANNERS
    )

    # Merge test items from both cores
    ctrl_tests = ctrl_p.get("tests", {})
    tgt_tests = tgt_p.get("tests", {})
    all_items = {}
    for name, v in ctrl_tests.items():
        all_items[name] = {"ctrl": v.get("result", "MISSING"), "tgt": "MISSING"}
    for name, v in tgt_tests.items():
        if name in all_items:
            all_items[name]["tgt"] = v.get("result", "MISSING")
        else:
            all_items[name] = {"ctrl": "MISSING", "tgt": v.get("result", "MISSING")}

    # Determine per-item result (both cores must PASS)
    items = []
    case_pass = True
    for name, statuses in all_items.items():
        both_pass = statuses["ctrl"] == "PASS" and statuses["tgt"] == "PASS"
        result = "PASS" if both_pass else "FAIL"
        if not both_pass:
            case_pass = False
        items.append({
            "name": name,
            "result": result,
            "ctrl_result": statuses["ctrl"],
            "tgt_result": statuses["tgt"],
        })

    # Case execution: CTRL must be SUCCESSFUL
    ctrl_exec = ctrl_p.get("execution", "UNKNOWN")
    tgt_exec = tgt_p.get("execution", "UNKNOWN")

    if ctrl_exec != "SUCCESSFUL":
        case_pass = False

    return {
        "case": case_name,
        "result": "PASS" if case_pass else "FAIL",
        "ctrl_execution": ctrl_exec,
        "tgt_execution": tgt_exec,
        "items": items,
    }


def print_report(results):
    """Print summary report to console."""
    print("\n--- MBOX dual-core test results ---")
    for r in results:
        status = r.get("result", "UNKNOWN")
        items = r.get("items", [])
        passed = sum(1 for i in items if i.get("result") == "PASS")
        total = len(items)
        ctrl_exec = r.get("ctrl_execution", "?")
        tgt_exec = r.get("tgt_execution", "?")
        print(f"## {r['case']}: {status} ({passed}/{total}) "
              f"[CTRL={ctrl_exec}, TGT={tgt_exec}]")
        for item in items:
            name = item.get("name", "?")
            res = item.get("result", "?")
            ctrl_res = item.get("ctrl_result", "?")
            tgt_res = item.get("tgt_result", "?")
            print(f"  {name}: {res} (ctrl={ctrl_res}, tgt={tgt_res})")


def main():
    parser = argparse.ArgumentParser(description="MBOX dual-core regression runner")
    parser.add_argument("--case", default="case_mbox_basic",
                        help="Test case directory name")
    parser.add_argument("--ctrl-uart", default=None,
                        help="Controller UART port (e.g. COM15)")
    parser.add_argument("--tgt-uart", default=None,
                        help="Target UART port (e.g. COM16)")
    parser.add_argument("--session-dir", default=None,
                        help="Session directory for reports")
    parser.add_argument("--skip-build", action="store_true",
                        help="Skip build step")
    parser.add_argument("--no-archive", action="store_true",
                        help="Don't archive results")
    parser.add_argument("--case-timeout", type=int, default=120,
                        help="Timeout per case in seconds")
    args = parser.parse_args()

    # Resolve UART ports
    ctrl_uart = args.ctrl_uart or os.environ.get("I3C_CTRL_UART") or _find_ctrl_uart()
    tgt_uart = args.tgt_uart or os.environ.get("I3C_TGT_UART") or _find_tgt_uart()

    if not ctrl_uart:
        log("ERROR: no controller UART found (use --ctrl-uart or I3C_CTRL_UART)")
        return 1
    if not tgt_uart:
        log("ERROR: no target UART found (use --tgt-uart or I3C_TGT_UART)")
        return 1

    log(f"Controller UART: {ctrl_uart}")
    log(f"Target UART: {tgt_uart}")

    date_str = datetime.now().strftime("%Y-%m-%d")
    session = Path(args.session_dir) if args.session_dir else allocate_session_dir(REPORTS_ROOT, date_str)
    logs = session / "logs"
    logs.mkdir(parents=True, exist_ok=True)

    # Build if needed
    if not args.skip_build:
        log(f"[BUILD] {args.case} (both cores)")
        build_dir = TESTS_DIR / args.case / "build"
        test_sh = TESTS_DIR / args.case / "test.sh"
        env = dict(os.environ)
        env["I3C_TEST_ROOT"] = str(SAMPLE_ROOT)
        rc = subprocess.call([BASH, str(test_sh)], cwd=str(TESTS_DIR / args.case), env=env)
        if rc != 0:
            log(f"[ERROR] build failed rc={rc}")
            return 1

    # Run case
    raw = run_case(args.case, ctrl_uart, tgt_uart, args.case_timeout)
    if raw is None:
        return 1

    # Evaluate results
    result = evaluate_case(args.case, raw["ctrl_lines"], raw["tgt_lines"])
    print_report([result])

    # Archive results
    if not args.no_archive:
        cdir = case_dir_for(session, DRIVER_NAME, args.case)
        cdir.mkdir(parents=True, exist_ok=True)
        report_json = cdir / f"{REPORT_BASE}.json"
        report_json.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
        log(f"[run] archived -> {report_json}")

    return 0 if result.get("result") == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
