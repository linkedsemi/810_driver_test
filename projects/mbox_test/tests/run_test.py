#!/usr/bin/env python3
"""MBOX dual-core regression runner.

Mbox 测试不用 ztest，而是 cpu1/cpu2 各自运行 mbox_*_main() 后手动打印
"PROJECT EXECUTION: SUCCESSFUL"。判定 = 两核都有该标记且无 error。

结果归档到 reports/<date>-<idx>/（report_dir 公共模块）。
"""
import argparse
import json
import os
import subprocess
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import threading
import time
from datetime import datetime
from pathlib import Path

import serial
import serial.tools.list_ports as list_ports
import shutil

SAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
from report_dir import allocate_session_dir, case_dir_for, update_summary

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST = os.environ.get("GDB_SERVER_HOST", "127.0.0.1")
GDB_PORT = os.environ.get("GDB_SERVER_PORT", "1025")
DRIVER_NAME = "mbox_test"
REPORT_BASE = f"{DRIVER_NAME}_report"

SUCCESS_MARKER = "PROJECT EXECUTION: SUCCESSFUL"
FAIL_MARKER = "PROJECT EXECUTION: FAILED"


def log(msg):
    print(msg, flush=True)


def _find_git_bash():
    # Prefer bin/bash.exe (not usr/bin which is MSYS2 alternate)
    candidates = [
        os.environ.get("GIT_BASH"),
        r"C:\Program Files\Git\bin\bash.exe",
        r"C:\Program Files (x86)\Git\bin\bash.exe",
    ]
    for c in candidates:
        if c and Path(c).exists():
            low = c.lower()
            if "system32" in low or "windowsapps" in low:
                continue
            return c
    # Fallback: any bash but not usr/bin
    p = shutil.which("bash")
    if p:
        return p
    return "bash"


Bash = _find_git_bash()
TESTS_DIR = SAMPLE_ROOT / "tests"


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
                data = self._ser.read(4096)
            except Exception:
                break
            if data:
                self.lines.append((time.time(), data.decode("utf-8", errors="replace")))

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2.0)
        if self._ser and self._ser.is_open:
            self._ser.close()

    @property
    def text(self):
        return "\n".join(l for _, l in self.lines)


def run_case(case_name, ctrl_uart, tgt_uart, timeout=120):
    """Run mbox dual-core test and capture both UARTs."""
    build_dir = TESTS_DIR / case_name / "build"
    target_elf = build_dir / "target" / "zephyr" / "zephyr.elf"
    controller_bin = build_dir / "controller" / "zephyr" / "zephyr.bin"

    if not target_elf.exists():
        log(f"[ERROR] Target ELF not found: {target_elf}")
        return None
    if not controller_bin.exists():
        log(f"[ERROR] Controller BIN not found: {controller_bin}")
        return None

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

    # Run test.sh -r (stdout=DEVNULL, serial capture is the real output)
    test_sh = TESTS_DIR / case_name / "test.sh"
    env = dict(os.environ)
    env["SKIP_LOAD"] = "y"
    env["AGENT_RUN"] = "y"
    env["I3C_TEST_ROOT"] = str(SAMPLE_ROOT).replace("\\", "/")
    proc = subprocess.Popen(
        [Bash, str(test_sh).replace("\\", "/"), "-r"],
        cwd=str(SAMPLE_ROOT).replace("\\", "/"),
        env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )

    # Poll until test.sh exits (load_and_run stays alive via gdb continue)
    start = time.time()
    while time.time() - start < timeout:
        if proc.poll() is not None:
            break
        time.sleep(0.5)

    # Kill entire test.sh + gdb process tree (qsh-debug §2.3: gdb 残留
    # 会占 CKLink 连接，下次 load 失败)
    try:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass
    time.sleep(1)
    # 兜底：杀所有残留 gdb（进程树杀不净时）
    try:
        subprocess.run(["wmic", "process", "where", "name='riscv64-unknown-elf-gdb.exe'", "delete"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
    except Exception:
        pass
    time.sleep(1)
    ctrl_cap.stop()
    tgt_cap.stop()

    return {
        "case": case_name,
        "ctrl_lines": ctrl_cap.lines,
        "tgt_lines": tgt_cap.lines,
        "timed_out": (time.time() - start >= timeout),
    }


def evaluate_case(case_name, ctrl_lines, tgt_lines):
    """Mbox dual-core判定：
    - ctrl/tgt 各自检查 SUCCESS_MARKER/FAIL_MARKER
    - 额外检查 LOG err 计数
    - case PASS = 两核都有 SUCCESS_MARKER 且无 FAIL_MARKER
    """
    ctrl_text = "\n".join(l for _, l in ctrl_lines)
    tgt_text = "\n".join(l for _, l in tgt_lines)

    ctrl_ok = SUCCESS_MARKER in ctrl_text and FAIL_MARKER not in ctrl_text
    tgt_ok = SUCCESS_MARKER in tgt_text and FAIL_MARKER not in tgt_text

    # Count LOG errors
    ctrl_errs = sum(1 for _, l in ctrl_lines if "<err>" in l.lower())
    tgt_errs = sum(1 for _, l in tgt_lines if "<err>" in l.lower())

    result = "PASS" if (ctrl_ok and tgt_ok) else "FAIL"
    items = [
        {"name": "ctrl_mbox_controller", "result": "PASS" if ctrl_ok else "FAIL",
         "reason": f"{ctrl_errs} err logs" if ctrl_errs else ""},
        {"name": "tgt_mbox_target", "result": "PASS" if tgt_ok else "FAIL",
         "reason": f"{tgt_errs} err logs" if tgt_errs else ""},
    ]

    return {
        "case": case_name,
        "result": result,
        "ctrl_execution": "SUCCESSFUL" if ctrl_ok else ("FAILED" if FAIL_MARKER in ctrl_text else "UNKNOWN"),
        "tgt_execution": "SUCCESSFUL" if tgt_ok else ("FAILED" if FAIL_MARKER in tgt_text else "UNKNOWN"),
        "items": items,
    }


def print_report(results):
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
            reason = item.get("reason", "")
            print(f"  {name}: {res} {reason}")


def write_markdown_report(cdir, result):
    """§6.4.1/§6.4.2 补写 markdown 报告：与 json 同源（items[] 渲染），覆盖写。"""
    base = cdir / f"{REPORT_BASE}.md"
    items = result.get("items", [])
    passed = sum(1 for it in items if it.get("result") == "PASS")
    total = len(items)
    status = result.get("result") or "FAIL"
    head = f"## {result.get('case', '')}: {status} ({passed}/{total})"
    extra = [f"{k.upper()}={result[k]}" for k in ("ctrl_execution", "tgt_execution")
             if result.get(k)]
    if extra:
        head += "  " + ", ".join(extra)
    lines = [
        f"# {DRIVER_NAME} 测试报告",
        "",
        f"- 工程: {DRIVER_NAME}",
        f"- case: {result.get('case', '')}",
        f"- 日期: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
        "- 板级: lsqsh_evb@2os",
        "",
        head,
        "",
        "| 测试项 | 结果 | 失败原因 |",
        "|---|---|---|",
    ]
    for it in items:
        name = it.get("name", "?")
        res = it.get("result", "?")
        st = "✅ PASS" if res == "PASS" else ("❌ FAIL" if res == "FAIL" else f"⬜ {res}")
        lines.append(f"| {name} | {st} | {it.get('reason', '')} |")
    lines.append("")
    base.write_text("\n".join(lines), encoding="utf-8")
    return base


def main():
    parser = argparse.ArgumentParser(description="MBOX dual-core regression runner")
    parser.add_argument("--case", default="case_mbox_basic",
                        help="Test case directory name")
    parser.add_argument("--ctrl-uart", default=None)
    parser.add_argument("--tgt-uart", default=None)
    parser.add_argument("--session-dir", default=None)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--no-archive", action="store_true")
    parser.add_argument("--case-timeout", type=int, default=120)
    args = parser.parse_args()

    ctrl_uart = args.ctrl_uart or os.environ.get("I3C_CTRL_UART")
    tgt_uart = args.tgt_uart or os.environ.get("I3C_TGT_UART")
    if not ctrl_uart or not tgt_uart:
        log("ERROR: --ctrl-uart and --tgt-uart required for mbox dual-core test")
        return 1

    log(f"Controller UART: {ctrl_uart}")
    log(f"Target UART: {tgt_uart}")

    date_str = datetime.now().strftime("%Y-%m-%d")
    session = Path(args.session_dir) if args.session_dir else allocate_session_dir(REPORTS_ROOT, date_str)

    if not args.skip_build:
        log(f"[BUILD] {args.case} (both cores)")
        test_sh = TESTS_DIR / args.case / "test.sh"
        env = dict(os.environ)
        env["I3C_TEST_ROOT"] = str(SAMPLE_ROOT).replace("\\", "/")
        rc = subprocess.call([Bash, str(test_sh).replace("\\", "/")],
                             cwd=str(SAMPLE_ROOT).replace("\\", "/"), env=env)
        if rc != 0:
            log(f"[ERROR] build failed rc={rc}")
            return 1

    raw = run_case(args.case, ctrl_uart, tgt_uart, args.case_timeout)
    if raw is None:
        return 1

    result = evaluate_case(args.case, raw["ctrl_lines"], raw["tgt_lines"])
    print_report([result])

    if not args.no_archive:
        cdir = case_dir_for(session, DRIVER_NAME, args.case)
        cdir.mkdir(parents=True, exist_ok=True)
        report_json = cdir / f"{REPORT_BASE}.json"
        report_json.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
        report_md = write_markdown_report(cdir, result)
        log(f"[run] archived -> {report_json}, {report_md}")
        items = result.get("items", [])
        passed = sum(1 for it in items if it.get("result") == "PASS")
        update_summary(session, f"{DRIVER_NAME}_{args.case}",
                       result.get("result") or "FAIL", passed, len(items),
                       f"{cdir.name}/{REPORT_BASE}.md")

    return 0 if result.get("result") == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
