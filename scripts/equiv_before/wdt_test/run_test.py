#!/usr/bin/env python3
"""WDT non-destructive single-core regression runner."""
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
from serial_archive import archive_serial  # noqa: E402
import process  # noqa: E402  —— L2 处理层（归档 → 判定 → 报告）

# ---- F1 判定框架收敛点（common/judge.py）----
from judge import (ZERO_EVIDENCE_NOTE, declared_names_from_dumper,  # noqa: E402
                   exec_label, has_evidence, truncation_evidence)
from judge import (REPORT_TABLE_HEADER, REPORT_TABLE_SEP,  # noqa: E402
                   clip_cell, coverage_expected, format_report_header,
                   md_table_row, result_text, source_label)

# 已声明测试项的唯一真源 = 同工程 scripts/dump_results.py 的
# TEST_NAMES/TEST_NAMES_BY_CASE（nm 链接序核验过，mem 兜底 idx 对齐也靠它）。
# 本文件**不再复制一份**（复制 = 漂移源，§6.4.3）。
DECLARED_DUMPER = Path(__file__).resolve().parent / "scripts" / "dump_results.py"

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
# 本工程为单核 1os 构建（cpu1 入口 = 0x10000000）：构建/烧录一律走
# test.sh → test_common.sh 的 build_single_test/load_and_run_single
# （SINGLE_BOARD / SINGLE_ROM_START），本脚本不消费 TARGET_ROM_START/BOARD。
# 原 2os 硬编码常量（0x100c0000 / lsqsh_evb@2os）会误导，已删（KNOWN_ISSUES #57）
DRIVER_NAME = "wdt_test"
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

    # F1（2026-09-18）：执行窗口到点时 ztest 的收尾标记（PROJECT EXECUTION
    # SUCCESSFUL）常还没打出来 —— 那不是失败，是**截断**。只要所有**已声明**
    # 测试项都已拿到确定结果（token；本工程无 mem 兜底）且全 PASS，就只差收尾
    # 标记 ⇒ 判 PASS，并把 execution 标成 TRUNCATED_AFTER_ALL_ITEMS（报告可辨）。
    # ★ 红线：任一已声明项**无证据**（MISSING）⇒ 维持 FAIL，不得被截断接受放过
    #   （否则会掩盖真挂死）；declared 表取不到时该分支一律不生效（fail-closed）。
    declared = declared_names_from_dumper(DECLARED_DUMPER, case_name)
    status = {i["name"]: i["result"] for i in items}
    trunc_ok, _missing = truncation_evidence(execution, declared, status)
    truncated = False
    # 零证据守卫（2026-09-18）：items 为空 ⇒ 一个测试项 token 都没捕到 ⇒
    # **不得判 PASS**（`all([]) == True` 会把"根本没测"读成"全过"，与上一轮
    # "5/5 全 PASS[mem]"假 PASS 同类）。判据收敛在 common/judge.py::has_evidence。
    all_pass = (execution == "SUCCESSFUL" and has_evidence(items)
                and all(i["result"] == "PASS" for i in items))
    if (not all_pass and trunc_ok and has_evidence(items)
            and all(i["result"] == "PASS" for i in items)):
        all_pass = True
        truncated = True
    # 零证据时报告要一眼看出"一个项都没捕到"，不能拿空表冒充正常（F2 同款要求）
    zero = not has_evidence(items)
    return {"case": case_name, "result": "PASS" if all_pass else "FAIL",
            "execution": exec_label(execution, truncated), "items": items,
            "zero_evidence": zero,
            "items_note": ZERO_EVIDENCE_NOTE if zero else ""}


def print_report(results):
    print("\n--- WDT test results ---")
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


def write_markdown_report(cdir, result):
    """§6.4.2 报告：与 json 同源（items[] 渲染），覆盖写。

    列序 / 表头 / 单元格转义统一走 common/judge.py（REPORT_TABLE_HEADER +
    md_table_row），与全家族逐字一致；本工程无内存兜底（来源恒为 token）。
    """
    base = cdir / f"{REPORT_BASE}.md"
    items = result.get("items", [])
    passed = sum(1 for it in items if it.get("result") == "PASS")
    total = len(items)
    status = result.get("result") or "FAIL"
    _core_exec = ", ".join(f"{k.upper()}={result[k]}"
                           for k in ("ctrl_execution", "tgt_execution") if result.get(k))
    lines = [
        f"# {DRIVER_NAME} 测试报告",
        "",
        *format_report_header(
            DRIVER_NAME, result.get("case", ""), "lsqsh_evb@1os/lsqsh/cpu1",
            date=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            execution=result.get("execution", "UNKNOWN"),
            mem_line="内存兜底: 不适用（本工程无内存兜底）",
            result=f"{status} ({passed}/{total})"),
        *([f"- 核执行标记: {_core_exec}"] if _core_exec else []),
        *([""] + ["> " + result.get("items_note", "")]
          if result.get("zero_evidence") else []),
        "",
        "## 测试项结果",
        "",
        REPORT_TABLE_HEADER,
        REPORT_TABLE_SEP,
    ]
    for it in items:
        lines.append(md_table_row([
            it.get("name", "?"), result_text(it.get("result", "?")), "token",
            "-", "-", it.get("reason", "")]))
    lines.append("")
    lines.append(f"## 汇总\n\nOVERALL: {status}\n")
    base.write_text("\n".join(lines), encoding="utf-8")
    return base



# ---- L2 处理层规格（common/process.py）：只声明**本工程特有**的接入点 ----
# 判定族特有的谓词 = evaluate_case（ztest token + 截断接受 + 零证据守卫）；
# 渲染 = write_markdown_report / print_report；其余（读归档、落盘、summary、rc）
# 由 process.py 统一。归档行是纯字符串 ⇒ 判定入参按 (0.0, 行) 重组（判定只读行文本）。
# 契约：每工程暴露 `l2_spec(facts=None)`；`facts` = 未归档的采集事实（本族不使用）。
def l2_spec(facts=None):
    return process.ProcessSpec(
        project=DRIVER_NAME,
        driver=DRIVER_NAME,
        report_base=REPORT_BASE,
        judge=lambda ci: evaluate_case(ci.case, [(0.0, ln) for ln in ci.lines["TGT"]]),
        render_md=lambda judged, cdir: write_markdown_report(cdir, judged[0]),
        console=lambda judged: print_report(judged),
        summary=lambda judged: (
            judged[0].get("result") or "FAIL",
            sum(1 for it in judged[0].get("items", []) if it.get("result") == "PASS"),
            len(judged[0].get("items", []))),
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", default="case_non_destructive")
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

    # L1→L2 边界（L0 归档形状，common/serial_archive.py）：先把采集结果归一成
    # `{核名: [原始行]}`，落档，再把**同一个对象**交给 L2（common/process.py）。
    # 离线重放读的就是这份归档 ⇒ 在线判定与离线重放的输入逐字相同。
    lines_by_core = {"TGT": [l for _, l in raw["tgt_lines"]]}
    if not args.no_archive:
        archive_serial(case_dir_for(session, DRIVER_NAME, args.case), args.case,
                       lines_by_core,
                       board="lsqsh_evb@1os/lsqsh/cpu1",
                       ports={"TGT": tgt_uart}, timeout=args.case_timeout,
                       script=__file__)
    return process.run(l2_spec(), case=args.case, lines=lines_by_core,
                       session=session, write=not args.no_archive).rc


if __name__ == "__main__":
    sys.exit(main())
