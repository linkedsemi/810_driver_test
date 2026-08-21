#!/usr/bin/env python3
"""
I3C dual-core one-button regression agent.

Runs all timing cases under 810_driver_test/i3c_dualcore_test/tests/case_*/
on the connected controller/target UARTs, parses the machine-readable UART tokens,
and produces an archived Markdown + JSON report under
810_driver_test/reports/<YYYY-MM-DD>/ (CLAUDE.md §12).

Archiving behavior (CLAUDE.md §12):
    - Report written to reports/<date>/i3c_test_report.{md,json}
    - Day-level summary.md index updated (per-project row replaced, §12.4)
    - INFO tokens (§13.4) parsed into the "actual" column of the report
    - Missing UART tokens backfilled from memory via dump_results.py --json
      (§12.3: token wins, memory fills gaps, both missing -> FAIL)

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


# tests/scripts/run_i3c_tests.py -> parents[0]=tests/scripts -> [1]=tests -> [2]=<driver>_test root
SAMPLE_ROOT = Path(__file__).resolve().parents[2]
TESTS_DIR = SAMPLE_ROOT / "tests"
PROTOCOL_H = TESTS_DIR / "include" / "i3c_dualcore_test_protocol.h"
# 归档根：810_driver_test/reports/<YYYY-MM-DD>/（CLAUDE.md §12.1）
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

TOKEN_RE = re.compile(
    r"^I3C_TEST:(CTRL|TGT):(START|PASS|FAIL):(\w+)(?::(.*))?\s*$"
)
INFO_RE = re.compile(r"^I3C_TEST:(CTRL|TGT):INFO:(\w+):([\w.=,\-]+)\s*$")
DONE_RE = re.compile(r"^I3C_TEST:DONE:(PASS|FAIL)\s*$")

SUMMARY_HEADER = "| 工程 | 日期时间 | 结果 | 通过/运行 | 备注 |\n|---|---|---|---|---|\n"


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


def parse_expected_from_coverage() -> Dict[str, str]:
    """从 tests/COVERAGE.md 提取每个测试项的预期数据（第 4 列，§13.3）。

    列序：| 测试项 | 驱动路径 | 维度 | 预期数据 | 预期来源 | 状态 |。
    工程没有 COVERAGE.md 时返回空（报告预期列填 -）。"""
    cov = TESTS_DIR / "COVERAGE.md"
    if not cov.exists():
        return {}
    expected: Dict[str, str] = {}
    for line in cov.read_text(encoding="utf-8").splitlines():
        m = re.match(r"\|\s*(\w+)\s*\|\s*([^|]*)\s*\|\s*([^|]*)\s*\|\s*([^|]*)\s*\|", line)
        if m and m.group(1) not in ("测试项", "---"):
            expected[m.group(1)] = m.group(4).strip()
    return expected


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


def parse_tokens(lines: List[str]) -> Tuple[Dict[str, Dict[str, str]], Optional[str],
                                            Dict[str, Dict[str, str]], Dict[str, Dict[str, str]]]:
    """返回 (outcomes, done, infos, reasons)。

    outcomes[name][core] = PASS|FAIL（core 缺失 = 该核无 token）
    infos[name][core] = "k=v,k=v"（INFO token 数值，§13.4）
    reasons[name][core] = FAIL 的 reason 原文（§13.5 格式）
    """
    outcomes: Dict[str, Dict[str, str]] = {}
    infos: Dict[str, Dict[str, str]] = {}
    reasons: Dict[str, Dict[str, str]] = {}
    done: Optional[str] = None
    for line in lines:
        m = TOKEN_RE.match(line)
        if m:
            core, outcome, name, reason = m.groups()
            outcomes.setdefault(name, {})
            if outcome == "FAIL":
                outcomes[name][core] = "FAIL"
                if reason:
                    reasons.setdefault(name, {})[core] = reason
            elif outcome == "PASS" and outcomes[name].get(core) != "FAIL":
                outcomes[name][core] = "PASS"
        m2 = INFO_RE.match(line)
        if m2:
            core, name, kv = m2.groups()
            infos.setdefault(name, {})[core] = kv
        m3 = DONE_RE.match(line)
        if m3:
            done = m3.group(1)
    return outcomes, done, infos, reasons


def memory_backfill(case_dir: Path) -> Optional[Dict]:
    """dump_results.py --json 读内存结果（§12.3 兜底）。

    失败（连接残留/符号缺失等）返回 None → 上层标记 mem-unavailable，不阻断回归。
    成功返回 {"CTRL": {name: status}, "TGT": {name: status}, "reasons": {name: str}}。
    """
    cmd = [sys.executable, "dump_results.py", "--case", case_dir.name, "--json"]
    try:
        proc = subprocess.run(cmd, cwd=str(TESTS_DIR / "scripts"),
                              capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        print("[MEM] dump_results.py timed out (mem-unavailable)", file=sys.stderr)
        return None
    if proc.returncode != 0:
        print(f"[MEM] dump_results.py rc={proc.returncode} (mem-unavailable): "
              f"{proc.stderr.strip()[-300:]}", file=sys.stderr)
        return None
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        print(f"[MEM] dump_results.py JSON parse failed (mem-unavailable): {exc}",
              file=sys.stderr)
        return None
    return data


def evaluate_case(case_name: str, ctrl_lines: List[str], tgt_lines: List[str],
                  test_names: List[str], mem_data: Optional[Dict]) -> Dict:
    ctrl_out, ctrl_done, ctrl_info, ctrl_reason = parse_tokens(ctrl_lines)
    tgt_out, tgt_done, tgt_info, tgt_reason = parse_tokens(tgt_lines)
    per_core = {"CTRL": (ctrl_out, ctrl_info, ctrl_reason),
                "TGT": (tgt_out, tgt_info, tgt_reason)}

    tests = []
    case_pass = True
    for name in test_names:
        statuses: Dict[str, str] = {}
        srcs: Dict[str, str] = {}
        for core in ("CTRL", "TGT"):
            out, _, _ = per_core[core]
            # outcomes[name] 是 {core: status}；缺失 = 该核无 token
            st = out.get(name, {}).get(core, "MISSING")
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
        test_pass = statuses["CTRL"] == "PASS" and statuses["TGT"] == "PASS"
        if not test_pass:
            case_pass = False

        # INFO 数值拼接（实际列）："CTRL:k=v,k=v; TGT:..."，无 INFO 留空
        info_parts = []
        for core in ("CTRL", "TGT"):
            kv = per_core[core][1].get(name)
            if kv:
                info_parts.append(f"{core}:{kv}")
        # reason 合并：token reason + 内存 reason（§13.5 可归因格式）
        reason_parts = []
        for core in ("CTRL", "TGT"):
            r = per_core[core][2].get(name)
            if r:
                reason_parts.append(f"{core}:{r}")
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

    if ctrl_done != "PASS" or tgt_done != "PASS":
        case_pass = False

    return {
        "name": case_name,
        "result": "PASS" if case_pass else "FAIL",
        "ctrl_done": ctrl_done,
        "tgt_done": tgt_done,
        "mem_unavailable": mem_data is None,
        "tests": tests,
    }


def print_report(results: List[Dict]) -> None:
    print("\n" + "=" * 80)
    print("I3C Dual-Core Regression Report")
    print("=" * 80)

    overall = "PASS"
    for case in results:
        print(f"\n## {case['name']}: {case['result']}")
        if case["mem_unavailable"]:
            print("  (mem-unavailable: 内存兜底未生效，MISSING 项按 FAIL 判定)")
        print(f"| Test | Controller | Target | Result | Source | INFO(actual) | Reason |")
        print(f"|------|------------|--------|--------|--------|--------------|--------|")
        for t in case["tests"]:
            print(f"| {t['name']} | {t['ctrl']} | {t['tgt']} | {t['result']} "
                  f"| {t['src']} | {t['info']} | {t['reason']} |")
        print(f"DONE: CTRL={case['ctrl_done']}, TGT={case['tgt_done']}")
        if case["result"] != "PASS":
            overall = "FAIL"

    print("\n" + "=" * 80)
    print(f"OVERALL: {overall}")
    print("=" * 80)


def write_markdown_report(results: List[Dict], expected: Dict[str, str],
                          report_dir: Path, driver_name: str) -> None:
    """§12.2 模板：表头含 预期 | 实际(INFO) | 结果 | 来源 | reason。"""
    now = datetime.now()
    all_tests = [t for c in results for t in c["tests"]]
    passed = sum(1 for t in all_tests if t["result"] == "PASS")
    failed = sum(1 for t in all_tests if t["result"] == "FAIL")
    overall = "PASS" if all(r["result"] == "PASS" for r in results) else "FAIL"

    lines = [
        f"# {driver_name} 测试报告",
        f"- 日期: {now:%Y-%m-%d %H:%M}  板卡: lsqsh_evb@2os  脚本: run_{driver_name}s.py",
        "- 环境: 见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在 COVERAGE.md 标注，CLAUDE.md §14）",
        f"- case 数: {len(results)}  总测试项: {len(all_tests)}  "
        f"通过: {passed}  失败: {failed}",
        "",
    ]
    for case in results:
        lines.append(f"## {case['name']} ({case['result']})")
        if case["mem_unavailable"]:
            lines.append("> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定")
        lines.append("| 测试项 | 预期 | 实际(INFO) | 结果 | 来源 | reason |")
        lines.append("|--------|------|-----------|------|------|--------|")
        for t in case["tests"]:
            exp = expected.get(t["name"], "-")
            lines.append(f"| {t['name']} | {exp} | {t['info'] or '-'} | {t['result']} "
                         f"| {t['src']} | {t['reason']} |")
        lines.append("")
    lines.append(f"## 汇总\n\nOVERALL: {overall}\n")

    path = report_dir / f"{driver_name}_report.md"
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


def update_summary(report_dir: Path, driver_name: str, overall: str,
                   passed: int, ran: int, note: str = "") -> None:
    """§12.4：当天 summary.md 索引。已存在则按工程名替换行，不重复追加。"""
    summary = report_dir / "summary.md"
    line = (f"| {driver_name} | {datetime.now():%Y-%m-%d %H:%M} | {overall} "
            f"| {passed}/{ran} | {note} |")
    if summary.exists():
        lines = summary.read_text(encoding="utf-8").splitlines()
        header_idx = next((i for i, l in enumerate(lines) if l.startswith("| 工程 |")), None)
        body_start = (header_idx + 2) if header_idx is not None else len(lines)
        new_lines = [l for l in lines[:body_start] if not l.startswith(f"| {driver_name} |")]
        new_lines.append(line)
        summary.write_text("\n".join(new_lines) + "\n", encoding="utf-8")
    else:
        summary.write_text(SUMMARY_HEADER + line + "\n", encoding="utf-8")
    print(f"summary.md updated: {summary}")


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
    expected = parse_expected_from_coverage()
    print(f"Tests per case: {test_names}")

    # Run phase: each case -> run -> memory backfill immediately (target alive).
    results: List[Dict] = []
    for case_dir in cases:
        try:
            raw = run_case(case_dir, ctrl_port, tgt_port, args.case_timeout)
        except RuntimeError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 1
        mem = memory_backfill(case_dir)
        case_result = evaluate_case(case_dir.name, raw["ctrl_lines"], raw["tgt_lines"],
                                    test_names, mem)
        results.append(case_result)

    print_report(results)

    if args.no_archive:
        print("\n--no-archive: skipping reports/ archiving.")
        return 0 if all(r["result"] == "PASS" for r in results) else 1

    # Archive (§12): reports/<date>/<driver>_test_report.{md,json} + summary.md
    driver_name = "i3c_test"
    report_dir = REPORTS_ROOT / datetime.now().strftime("%Y-%m-%d")
    report_dir.mkdir(parents=True, exist_ok=True)
    write_markdown_report(results, expected, report_dir, driver_name)
    write_json_report(results, report_dir / f"{driver_name}_test_report.json")

    all_tests = [t for c in results for t in c["tests"]]
    passed = sum(1 for t in all_tests if t["result"] == "PASS")
    ran = sum(1 for t in all_tests if t["result"] != "NOT_RUN")
    overall = "PASS" if all(r["result"] == "PASS" for r in results) else "FAIL"
    update_summary(report_dir, driver_name, overall, passed, ran)

    return 0 if overall == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
