#!/usr/bin/env python3
"""
I3C dual-core one-button regression agent.

Runs all timing cases under 810_driver_test/i3c_dualcore_test/tests/case_*/
on the connected controller/target UARTs, parses the machine-readable UART tokens,
and produces a Markdown report plus i3c_test_report.json.

Environment:
    I3C_CTRL_UART          Controller UART port (e.g. COM3, /dev/ttyUSB0)
    I3C_TGT_UART           Target UART port (e.g. COM4, /dev/ttyUSB1)
    GDB_SERVER_HOST        GDB server host (default: localhost)
    GDB_SERVER_PORT        GDB server port (default: 1025)
    I3C_CASE_TIMEOUT       Seconds to wait for one case to finish (default: 30)
    I3C_BUILD_JOBS         Max parallel west builds (default: 1)

Usage:
    python tests/run_i3c_tests.py
    python tests/run_i3c_tests.py --case case_pure_i3c_6m25
"""

import argparse
import json
import os
import re
import signal
import sys
import subprocess
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


REPO_ROOT = Path(__file__).resolve().parents[3]
SAMPLE_ROOT = REPO_ROOT / "810_driver_test" / "i3c_dualcore_test"
TESTS_DIR = SAMPLE_ROOT / "tests"
PROTOCOL_H = TESTS_DIR / "include" / "i3c_dualcore_test_protocol.h"

TOKEN_RE = re.compile(
    r"^I3C_TEST:(CTRL|TGT):(START|PASS|FAIL):(\w+)(?::(.*))?\s*$"
)
DONE_RE = re.compile(r"^I3C_TEST:DONE:(PASS|FAIL)\s*$")


def parse_test_names() -> List[str]:
    """Read the shared protocol header and extract the i3c_test_names array."""
    text = PROTOCOL_H.read_text(encoding="utf-8")
    # Find the array body between braces.
    m = re.search(r"i3c_test_names\[I3C_TEST_COUNT\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise RuntimeError(f"could not parse i3c_test_names from {PROTOCOL_H}")
    body = m.group(1)
    names = []
    for line in body.splitlines():
        mm = re.search(r'\[I3C_TEST_\w+\]\s*=\s*"([^"]+)"', line)
        if mm:
            names.append(mm.group(1))
    return names


class SerialCapture:
    """Capture lines from a serial port in a background thread."""

    def __init__(self, port: str, baud: int = 115200):
        self.port = port
        self.baud = baud
        self.lines: List[Tuple[float, str]] = []
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._ser: Optional[serial.Serial] = None
        self.error: Optional[str] = None

    def start(self) -> None:
        try:
            self._ser = serial.Serial(self.port, self.baud, timeout=0.1)
        except Exception as exc:
            self.error = f"failed to open {self.port}: {exc}"
            return

        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self) -> None:
        assert self._ser is not None
        while not self._stop.is_set():
            try:
                data = self._ser.readline()
            except Exception as exc:
                self.lines.append((time.time(), f"[serial error: {exc}]"))
                break
            if data:
                line = data.decode("utf-8", errors="replace").rstrip("\r\n")
                self.lines.append((time.time(), line))

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
        if self._ser is not None and self._ser.is_open:
            self._ser.close()

    def flush_stale(self, duration: float = 0.3) -> None:
        """Discard data already in the UART buffer before a run."""
        if self._ser is None or not self._ser.is_open:
            return
        deadline = time.time() + duration
        while time.time() < deadline:
            try:
                if self._ser.in_waiting:
                    self._ser.reset_input_buffer()
            except Exception:
                pass
            time.sleep(0.05)

    @property
    def text(self) -> str:
        return "\n".join(line for _, line in self.lines)


def find_cases(filter_name: Optional[str] = None) -> List[Path]:
    cases = sorted(p for p in TESTS_DIR.iterdir() if p.is_dir() and p.name.startswith("case_"))
    if filter_name:
        cases = [p for p in cases if p.name == filter_name]
    return cases


def build_case(case_dir: Path) -> None:
    print(f"\n[BUILD] {case_dir.name}")
    start = time.time()
    result = subprocess.run(
        ["bash", "test.sh"],
        cwd=str(case_dir),
        stdout=None,
        stderr=None,
    )
    elapsed = time.time() - start
    if result.returncode != 0:
        raise RuntimeError(f"build failed for {case_dir.name} (rc={result.returncode})")
    print(f"[BUILD] {case_dir.name} OK ({elapsed:.1f}s)")


def run_with_timeout(cmd: List[str], cwd: Path, timeout: int) -> int:
    """Run a command and terminate the process group on timeout (Windows-safe)."""
    kwargs = {"cwd": str(cwd)}
    if sys.platform == "win32":
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP

    proc = subprocess.Popen(cmd, **kwargs)
    try:
        rc = proc.wait(timeout=timeout)
        return rc
    except subprocess.TimeoutExpired:
        print(f"[RUN] timed out after {timeout}s, terminating process group", file=sys.stderr)
        if sys.platform == "win32":
            try:
                os.kill(proc.pid, signal.CTRL_BREAK_EVENT)
            except Exception as exc:
                print(f"[RUN] CTRL_BREAK_EVENT failed: {exc}", file=sys.stderr)
                proc.kill()
        else:
            proc.terminate()
        try:
            rc = proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            rc = proc.wait(timeout=5)
        return rc if rc is not None else -1


def run_case(case_dir: Path, ctrl_port: str, tgt_port: str, case_timeout: int) -> Dict:
    print(f"\n[RUN] {case_dir.name}")

    ctrl_cap = SerialCapture(ctrl_port)
    tgt_cap = SerialCapture(tgt_port)
    ctrl_cap.start()
    tgt_cap.start()
    if ctrl_cap.error:
        raise RuntimeError(ctrl_cap.error)
    if tgt_cap.error:
        raise RuntimeError(tgt_cap.error)

    # Clear any stale data and give the threads a moment to start.
    ctrl_cap.flush_stale()
    tgt_cap.flush_stale()
    time.sleep(0.2)

    start = time.time()
    run_rc = run_with_timeout(
        ["bash", "test.sh", "-r"],
        case_dir,
        case_timeout,
    )

    # Wait a little for trailing UART output, then stop capture.
    time.sleep(0.5)
    ctrl_cap.stop()
    tgt_cap.stop()
    elapsed = time.time() - start
    print(f"[RUN] {case_dir.name} finished in {elapsed:.1f}s (rc={run_rc})")

    return {
        "case": case_dir.name,
        "ctrl_text": ctrl_cap.text,
        "tgt_text": tgt_cap.text,
        "ctrl_lines": [line for _, line in ctrl_cap.lines],
        "tgt_lines": [line for _, line in tgt_cap.lines],
        "run_rc": run_rc,
    }


def parse_tokens(lines: List[str]) -> Tuple[Dict[str, str], Optional[str]]:
    """Return a dict {test_name: outcome} and the DONE result."""
    outcomes: Dict[str, str] = {}
    done: Optional[str] = None
    for line in lines:
        m = TOKEN_RE.match(line)
        if m:
            core, outcome, name, reason = m.groups()
            key = name
            if outcome == "FAIL":
                outcomes[key] = "FAIL"
            elif outcome == "PASS" and outcomes.get(key) != "FAIL":
                outcomes[key] = "PASS"
            # START is recorded implicitly by presence; missing test = not run.
        m2 = DONE_RE.match(line)
        if m2:
            done = m2.group(1)
    return outcomes, done


def evaluate_case(case_name: str, ctrl_lines: List[str], tgt_lines: List[str], test_names: List[str]) -> Dict:
    ctrl_outcomes, ctrl_done = parse_tokens(ctrl_lines)
    tgt_outcomes, tgt_done = parse_tokens(tgt_lines)

    tests = []
    case_pass = True
    for name in test_names:
        ctrl = ctrl_outcomes.get(name, "MISSING")
        tgt = tgt_outcomes.get(name, "MISSING")
        test_pass = (ctrl == "PASS" and tgt == "PASS")
        if not test_pass:
            case_pass = False
        tests.append({
            "name": name,
            "ctrl": ctrl,
            "tgt": tgt,
            "result": "PASS" if test_pass else "FAIL",
        })

    if ctrl_done != "PASS" or tgt_done != "PASS":
        case_pass = False

    return {
        "name": case_name,
        "result": "PASS" if case_pass else "FAIL",
        "ctrl_done": ctrl_done,
        "tgt_done": tgt_done,
        "tests": tests,
    }


def print_report(results: List[Dict]) -> None:
    print("\n" + "=" * 80)
    print("I3C Dual-Core Regression Report")
    print("=" * 80)

    overall = "PASS"
    for case in results:
        print(f"\n## {case['name']}: {case['result']}")
        print(f"| Test | Controller | Target | Result |")
        print(f"|------|------------|--------|--------|")
        for t in case["tests"]:
            print(f"| {t['name']} | {t['ctrl']} | {t['tgt']} | {t['result']} |")
        print(f"DONE: CTRL={case['ctrl_done']}, TGT={case['tgt_done']}")
        if case["result"] != "PASS":
            overall = "FAIL"

    print("\n" + "=" * 80)
    print(f"OVERALL: {overall}")
    print("=" * 80)


def write_json_report(results: List[Dict], path: Path) -> None:
    overall = "PASS" if all(r["result"] == "PASS" for r in results) else "FAIL"
    report = {
        "timestamp": datetime.now().isoformat(),
        "overall": overall,
        "cases": results,
    }
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"\nJSON report written to: {path}")


def list_serial_ports() -> List[str]:
    return [p.device for p in serial.tools.list_ports.comports()]


def main() -> int:
    parser = argparse.ArgumentParser(description="I3C dual-core regression agent")
    parser.add_argument("--case", help="Run a single case (default: all case_* dirs)")
    parser.add_argument("--ctrl-uart", help="Controller UART port")
    parser.add_argument("--tgt-uart", help="Target UART port")
    parser.add_argument(
        "--case-timeout",
        type=int,
        default=int(os.environ.get("I3C_CASE_TIMEOUT", "30")),
        help="Seconds to wait for each case run (default: 30)",
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

    # Build phase.
    if not args.skip_build:
        for case_dir in cases:
            try:
                build_case(case_dir)
            except RuntimeError as exc:
                print(f"ERROR: {exc}", file=sys.stderr)
                return 1

    if args.build_only:
        print("\nBuild-only requested; skipping run phase.")
        return 0

    test_names = parse_test_names()
    print(f"Tests per case: {test_names}")

    # Run phase.
    results: List[Dict] = []
    for case_dir in cases:
        try:
            raw = run_case(case_dir, ctrl_port, tgt_port, args.case_timeout)
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 1
        case_result = evaluate_case(case_dir.name, raw["ctrl_lines"], raw["tgt_lines"], test_names)
        results.append(case_result)

    print_report(results)
    write_json_report(results, Path("i3c_test_report.json"))
    return 0 if all(r["result"] == "PASS" for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
