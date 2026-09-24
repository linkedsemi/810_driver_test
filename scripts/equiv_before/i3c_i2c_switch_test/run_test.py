#!/usr/bin/env python3
"""I3C/I2C switch-test dual-core one-button regression agent (ztest-native).

Runs all cases under 810_driver_test/projects/i3c_i2c_switch_test/tests/case_*/
on the connected controller/target UARTs, parses the ztest-native output
(common/ztest_report.parse_ztest) and produces an archived Markdown + JSON
report under 810_driver_test/reports/<YYYY-MM-DD>-<idx>/ (SPEC_REFERENCE.md §6.4).

双核工程 ztest 原生化版：每核固件跑自己的 ztest 套件，逐项结果 = 同名
测试项双核 PASS；case 判定 = 双核 execution 均 "SUCCESSFUL" + 全部项双核
PASS。完成判定（提前杀 gdb 的静默判据）= 本固件 ZT_BOOT 已见的端口上出现
PROJECT EXECUTION（他方固件无 ZT_BOOT，其标记一律忽略，KNOWN_ISSUES.md #30）。

Archiving behavior (SPEC_REFERENCE.md §6.4):
    - Report written to reports/<date>-<idx>/<driver>_<case>/i3c_i2c_switch_test_report.{md,json}
    - Session summary.md index updated (per-case row replaced, §12.4)
    - Missing UART items backfilled from memory via dump_results.py --json
      (§12.3: token wins, memory fills gaps, both missing -> FAIL)

Environment:
    I3C_CTRL_UART          Controller UART port (e.g. COM15)
    I3C_TGT_UART           Target UART port (e.g. COM16)
    GDB_SERVER_HOST        GDB server host (default: localhost)
    GDB_SERVER_PORT        GDB server port (default: 1025)
    I3C_CASE_TIMEOUT       Seconds to wait for one case to finish (default: 60)

Usage:
    python run_test.py
    python run_test.py --case case_switch_core
    python run_test.py --case ... --session-dir reports/2026-08-29-3
"""

import argparse
import json
import os
import re
import shutil
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import subprocess
import tempfile
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


# tests/run_test.py -> parents[1] = i3c_i2c_switch_test 工程根
SAMPLE_ROOT = Path(__file__).resolve().parents[1]
# 归档根：810_driver_test/reports/（会话目录 <YYYY-MM-DD>-<idx> 由 report_dir 分配）
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

# 公共解析/归档层（Task 1/2 产物，路径同 trng 样板；REPORTS_ROOT / "common"
# 是计划笔误——公共模块在 810_driver_test/common/）
sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
from ztest_report import parse_ztest   # noqa: E402
from serial_archive import archive_serial  # noqa: E402
import process  # noqa: E402  —— L2 处理层（归档 → 判定 → 报告）
from report_dir import (allocate_session_dir, case_dir_for,  # noqa: E402
                        update_summary)

# ---- F1/F2/F3 判定框架收敛点（common/judge.py）----
from judge import (MEM_ONLY_NOTE, exec_conflict_note, exec_label, has_evidence,  # noqa: E402
                   mem_only_evidence, mem_state_line, mem_state_of_payload,
                   truncation_evidence)
from judge import (REPORT_TABLE_HEADER, REPORT_TABLE_SEP,  # noqa: E402
                   clip_cell, coverage_expected, format_report_header,
                   md_table_row, result_text, source_label)

DRIVER_NAME = "i3c_i2c_switch_test"          # §12.4 summary 行 `<driver>_test` 前缀
REPORT_BASE = f"{DRIVER_NAME}_report"        # i3c_i2c_switch_test_report.{md,json}（修复旧版双 _test）

CTRL_BANNERS = ("CPU2 I3C_I2C_CTRL", "i3c_i2c_master_test")
TGT_BANNERS = ("CPU1 I3C_I2C_TGT", "i3c_i2c_target_test")

# 测试项顺序 = protocol.h 枚举序（tr_record(enum_id,...) 按枚举下标写入 test_results[]）。
# 本工程不使用 ztest 注册（ztest_unit_test_list 为空），无 ztest 链接序概念。
# 枚举序（2026-09-09 从 preprocessed .c.i 确认，11 项）：
#   0:pinctrl_states, 1:i3c_baseline, 2:switch_i3c_to_i2c,
#   3:i2c_rw_1, 4:i2c_rw_16, 5:i2c_rw_17, 6:i2c_rw_100,
#   7:switch_i2c_to_i3c, 8:i2c_nack_bad_addr, 9:switch_stress,
#   10:broadcast_nack_recovery
TEST_NAMES = [
    "pinctrl_states",
    "i3c_baseline",
    "switch_i3c_to_i2c",
    "i2c_rw_1",
    "i2c_rw_16",
    "i2c_rw_17",
    "i2c_rw_100",
    "switch_i2c_to_i3c",
    "i2c_nack_bad_addr",
    "switch_stress",
    "broadcast_nack_recovery",
]


def _find_git_bash() -> str:
    """解析 Git Bash 完整路径。

    Windows 下裸 "bash" 会被 C:\\Windows\\System32\\bash.exe（WSL）抢先解析
    （CreateProcess 先搜 System32 再搜 PATH），WSL 里无 west → build rc=127。
    显式过滤 system32 / windowsapps 路径，优先 GIT_BASH 环境变量。
    """
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
GDB = shutil.which("riscv64-unknown-elf-gdb") or "riscv64-unknown-elf-gdb"
DEBUGSERVER_EXE = r"C:\uaer_app\debug_server\bin\DebugServerConsole.exe"


def _netstat_1025() -> List[Tuple[str, str, str]]:
    """netstat 1025 端口状态：(状态, 本端PID, 对端PID)。"""
    rows: List[Tuple[str, str, str]] = []
    try:
        out = subprocess.run(["netstat", "-ano"], capture_output=True,
                             text=True, timeout=15).stdout
        for line in out.splitlines():
            if ":1025" not in line:
                continue
            parts = line.split()
            if len(parts) >= 5 and parts[0] == "TCP":
                rows.append((parts[3], parts[4], parts[1]))  # (STATE, localPID, remote)
    except Exception:
        pass
    return rows


def _kill_residual_gdb() -> None:
    """精准杀 1025 ESTABLISHED 连接对端（是 gdb 就 taskkill）。

    taskkill /T 树杀有时杀不干净（实测残留 gdb 仍持有连接），必须按连接杀。
    ★ Python subprocess 无 MSYS 路径转换层：tasklist/taskkill 参数必须用
    单斜杠（/FI /FO /F /PID），双斜杠 //F 会被 Windows 程序拒绝
    "无效参数/选项"（Git Bash 里 //F 能被 MSYS 转成 /F，subprocess 不会）。
    """
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


def _restart_debugserver() -> None:
    """重启 DebugServer 进程并等就绪（日志轮询 "DebuggerServer$" 提示符）。

    实测：任何"连接→detach→退出"的 gdb（包括探测连接）都会让 DebugServer
    留下连接残留，下一次 gdb 全部 "Target disconnected"——detach 不能真正
    释放连接，唯一可靠的方式是重启进程（qsh-debug §2.3）。
    CKLink 初始化耗时随机，不能只固定 sleep → stdout 重定向日志文件轮询。
    """
    print("[ENV] restarting DebugServer (residue)", file=sys.stderr)
    subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
    time.sleep(1)
    # taskkill /IM 可能杀不净（无效实例占 1025，新实例绑定失败卡在初始化）
    # → 用 netstat 精确杀 1025 的 LISTENING 进程兜底。
    for state, local_pid, _ in _netstat_1025():
        if state == "LISTENING" and local_pid.isdigit():
            subprocess.run(["taskkill", "/F", "/PID", local_pid],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=15)
    log_path = os.path.join(tempfile.gettempdir(), "debug_server.log")
    try:
        os.remove(log_path)
    except OSError:
        pass
    try:
        subprocess.Popen(
            [r"C:/uaer_app/debug_server/bin/DebugServerConsole.exe"],
            cwd=r"C:/uaer_app/debug_server/bin",
            stdout=open(log_path, "w", encoding="utf-8", errors="replace"),
            stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NEW_CONSOLE,
        )
    except Exception:
        pass
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            with open(log_path, encoding="utf-8", errors="replace") as f:
                if "DebuggerServer$" in f.read() or "DebuggerServer>" in f.read():
                    break
        except Exception:
            pass
        time.sleep(0.5)
    else:
        print("[ENV] WARNING: DebugServer 30s 内未打印就绪提示符", file=sys.stderr)
    time.sleep(1)


def clean_gdb_residue(force_restart: bool = False) -> None:
    """清 DebugServer 连接残留（gdb continue 被杀后的高频坑，见 KNOWN_ISSUES.md #1）。

    批量回归中每个 case 的固件跑完后 gdb continue 不会自行退出，只能杀进程，
    被杀后 DebugServer 仍认为有连接 → 下一次 load 静默失效（空采集连环 FAIL）。
    DebugServer 同一时刻只接受一个连接：残留 gdb 占着时新 gdb 连不上
    （"Target disconnected"）。

    ★ 用户反馈：无条件重启 DebugServer 太频繁（CKLink 开关 1s 3-4 次）→
    改为**按需重启**：先杀残留 gdb，杀完复查 1025——连接残留还在（ESTABLISHED
    未释放）或 server 已死（无 LISTENING）才重启；健康则不重启。
    """
    _kill_residual_gdb()
    if force_restart:
        _restart_debugserver()
        return
    need = False
    for state, _, _ in _netstat_1025():
        if state == "ESTABLISHED":
            need = True
            break
    if not need:
        # server 没起来（无 LISTENING）也要重启
        found_listening = any(st == "LISTENING" for st, _, _ in _netstat_1025())
        need = not found_listening
    if need:
        _restart_debugserver()


def _probe_gdb_alive() -> bool:
    """探测 DebugServer 连接是否健康：能连上且读到目标寄存器状态。"""
    try:
        result = subprocess.run(
            [GDB, "-ex", "target remote 127.0.0.1:1025", "-ex", "info registers pc",
             "-ex", "detach", "-ex", "quit"],
            capture_output=True, text=True, timeout=15,
        )
        out = (result.stdout or "") + (result.stderr or "")
        return "Remote debugging" in out or "0x" in out
    except Exception:
        return False


def restart_debugserver() -> None:
    """重启 XuanTie DebugServer（residue 深到 detach 清不掉时的兜底）。

    §10.1：残留时 reset/load 静默无效；§2.3：杀 server 重启是最终手段。
    ★ 重启后必须等就绪（日志轮询 "DebuggerServer$"）+ 稳定期：实测
    （2026-08-27，2/2 复现）DebugServer 新实例后立即连跑 → 间歇性
    load_fail / load 中途断连 / WDR 时序异常；实例稳定 ~30min 后连跑
    7 case 全无环境失败。重启后 warm_up_s 内不开始烧录。
    """
    _restart_debugserver()   # taskkill + 兜底杀 + Popen + 30s 就绪轮询
    time.sleep(5)           # 稳定期：新实例 CKLink 初始化/总线状态稳定后再连跑


def preflight_gdb() -> None:
    """烧录链路预检：不健康先 detach 清一次，仍不健康重启 DebugServer 再验。"""
    if _probe_gdb_alive():
        return
    clean_gdb_residue()
    if _probe_gdb_alive():
        return
    restart_debugserver()
    if not _probe_gdb_alive():
        raise RuntimeError(
            "DebugServer 预检失败（重启后仍连不上）：请检查 CKLink 连接/"
            "是否被 CSKYFlashProgrammer 占用（§10.2）")
    print("[PREFLIGHT] DebugServer restarted OK")


TESTS_DIR = SAMPLE_ROOT / "tests"


def parse_expected_from_coverage():
    """§6.5.2 预期数据（COVERAGE.md 表「预期数据」列）—— 解析收敛在 common/judge.py。

    只认列头含 `预期数据` 的表；转义竖线与裸竖线都不再串列（旧实现用
    `[^|]*` 解析，COVERAGE 里的转义竖线被截成半个单元格，见 §1.4）。"""
    return coverage_expected(TESTS_DIR / "COVERAGE.md")


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

    def plain_lines(self) -> List[str]:
        return [line for _, line in self.lines]


def find_cases(filter_name: Optional[str] = None) -> List[Path]:
    cases = sorted(p for p in TESTS_DIR.iterdir() if p.is_dir() and p.name.startswith("case_"))
    if filter_name:
        cases = [p for p in cases if p.name == filter_name]
    return cases


def build_case(case_dir: Path) -> None:
    print(f"\n[BUILD] {case_dir.name}")
    start = time.time()
    result = subprocess.run(
        [BASH, "test.sh"],
        cwd=str(case_dir),
        stdout=None,
        stderr=None,
    )
    elapsed = time.time() - start
    if result.returncode != 0:
        raise RuntimeError(f"build failed for {case_dir.name} (rc={result.returncode})")
    print(f"[BUILD] {case_dir.name} OK ({elapsed:.1f}s)")


def _kill_proc_group(proc) -> None:
    """杀掉整个进程树（bash → gdb 子进程一并带走）。

    Windows 下 CTRL_BREAK_EVENT 只发给 bash，MSYS bash 不转发给子进程 gdb
    → gdb 存活并持有 DebugServer 连接 → 后续所有 load 挂死（批量回归
    连环卡死的根因）。taskkill /T /F 才能保证树全灭。
    """
    if sys.platform == "win32":
        subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def _spawn_test_run(case_dir: Path):
    """启动 test.sh -r，返回 (proc, proc_lines)。gdb 输出经 PIPE 线程采集。

    RUN_TIMEOUT 放宽到 120s：continue 后 gdb 挂住等目标（正常），固件独立
    运行；gdb 死活不影响固件，由上层判据控制。
    """
    kwargs = {"cwd": str(case_dir), "stdout": subprocess.PIPE,
              "stderr": subprocess.STDOUT, "text": True, "bufsize": 1}
    if sys.platform == "win32":
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
    env = dict(os.environ)
    env["RUN_TIMEOUT"] = "120"
    proc = subprocess.Popen([BASH, "test.sh", "-r"], env=env, **kwargs)
    proc_lines = []
    def _reader():
        try:
            for line in proc.stdout:
                proc_lines.append(line)
        except Exception:
            pass
    threading.Thread(target=_reader, daemon=True).start()
    return proc, proc_lines


def _ztest_view(cap: SerialCapture) -> Dict:
    """串口采集行的 parse_ztest 视图（角色 banner 参数与 dbg_run 同款）。"""
    return parse_ztest(cap.plain_lines(),
                       ctrl_banners=CTRL_BANNERS, tgt_banners=TGT_BANNERS)


def run_case(case_dir: Path, ctrl_port: str, tgt_port: str, case_timeout: int) -> Dict:
    """单个 case 的烧录+运行+采集（两遍烧录法，KNOWN_ISSUES.md #4）。

    判据（串口字节流为准，不依赖 gdb 输出——Windows gdb stdout 经管道
    块缓冲，load 后的 "Continuing." 可能永远读不到，实测必现）：
    1. 两遍烧录：prime 轮跑到串口出现任何固件输出（ZT_BOOT/ztest 行/旧
       token，旧镜像可能无 ZT_BOOT，等 ZT_BOOT 会误报环境失败）再等 5s
       即杀；清残留后正式轮再烧同一镜像；
    2. 环境判定：正式轮 spawn 后 LOAD_WINDOW_S 内收不到 ZT_BOOT →
       判定测试环境有问题（区别于测试失败）；
    3. 静默判完成（§10.30）：本固件 ZT_BOOT 已见（boot_seen）的端口上出现
       PROJECT EXECUTION 后连续 IDLE_S 无新字节 → 套件已结束，提前杀
       （他方固件无 ZT_BOOT，其 PROJECT EXECUTION 一律忽略——板被他人
       占用时的串扰防护）；
    4. case_timeout 硬上限兜底。
    """
    LOAD_WINDOW_S = 20
    IDLE_S = 10
    P1_HOLD_S = 5

    # 上个 case 结束时 gdb 被杀必然留残留连接（DebugServer 单连接，
    # 残留占着则本次全部 load 失败）→ main() 循环已在每个 case 前调
    # clean_gdb_residue（qsh-debug §2.3），此处保持单一入口防 DebugServer 高频开关
    ctrl_cap = SerialCapture(ctrl_port)
    tgt_cap = SerialCapture(tgt_port)
    ctrl_cap.start()
    tgt_cap.start()
    if ctrl_cap.error:
        tgt_cap.stop()   # 开不动一路时另一路必须关掉，否则句柄泄漏致重试 PermissionError
        raise RuntimeError(ctrl_cap.error)
    if tgt_cap.error:
        ctrl_cap.stop()
        raise RuntimeError(tgt_cap.error)

    def _has_boot() -> bool:
        return _ztest_view(ctrl_cap)["boot_seen"] or _ztest_view(tgt_cap)["boot_seen"]

    # 套件完成标记：仅认"本固件 ZT_BOOT 已见（boot_seen）"的端口上的
    # PROJECT EXECUTION（§10.30 门控）。静默判据以此为准——10s 静默但无
    # 标记 = 测试中途长间隔，不能判完成。
    def _has_completion_marker() -> bool:
        c = _ztest_view(ctrl_cap)
        t = _ztest_view(tgt_cap)
        return ((c["boot_seen"] and c["execution"] is not None)
                or (t["boot_seen"] and t["execution"] is not None))

    # prime 轮固件跑的是 I-cache 残留的旧镜像（迁移前固件无 ZT_BOOT）——
    # 判据用"任何固件输出"（旧 token/ztest 行/ZT_BOOT 任一即算）。
    def _has_any_output() -> bool:
        txt = ctrl_cap.text + "\n" + tgt_cap.text
        for k in ("ZT_BOOT", "I3C_I2C_TEST:", "PROJECT EXECUTION",
                  "START - ", " PASS - ", " FAIL - "):
            if k in txt:
                return True
        return False

    # ---- pass 1 (prime)：冲刷 I-cache 旧镜像，输出丢弃 ----
    print(f"[RUN] {case_dir.name} pass-1 (prime, I-cache flush)")
    proc, proc_lines = _spawn_test_run(case_dir)
    p1_start = time.time()
    p1_boot = None
    while time.time() - p1_start < LOAD_WINDOW_S:
        if _has_any_output():
            p1_boot = time.time()
            break
        if proc.poll() is not None:
            break
        time.sleep(0.3)
    _kill_proc_group(proc)
    clean_gdb_residue()
    if p1_boot is None:
        ctrl_cap.stop()   # 抛错前释放串口句柄：泄漏会让同进程重试开 COM 失败
        tgt_cap.stop()
        raise RuntimeError(
            f"{case_dir.name}: pass-1 烧录后 {LOAD_WINDOW_S}s 内无任何固件输出"
            "（ZT_BOOT/ztest 行/I3C_I2C_TEST token）——测试环境有问题"
            "（DebugServer 残留/烧录链路/镜像陈旧）")
    # prime 轮再跑 5s，确保 I-cache 冲刷充分（两遍烧录法要点）
    time.sleep(P1_HOLD_S)

    # ---- pass 2 (capture)：正式轮，结果以此为准 ----
    print(f"[RUN] {case_dir.name} pass-2 (capture)")
    # 清空采集行：pass-1 的 token（旧固件/prime 轮）不能混进正式结果
    ctrl_cap.lines.clear()
    tgt_cap.lines.clear()
    ctrl_cap.flush_stale()
    tgt_cap.flush_stale()
    time.sleep(0.2)

    # 诊断：spawn 前 DebugServer 监听状态（1025 LISTENING 的 PID 是否 DebugServerConsole）
    try:
        out = subprocess.run(["netstat", "-ano"], capture_output=True,
                             text=True, timeout=15).stdout
        ds_pid = ""
        for line in out.splitlines():
            if ":1025" in line and "LISTENING" in line:
                ds_pid = line.split()[-1]
                break
        if ds_pid:
            ti = subprocess.run(["tasklist", "/FI", f"PID eq {ds_pid}"],
                                capture_output=True, text=True, timeout=15).stdout
            name = [l for l in ti.splitlines() if str(ds_pid) in l]
            print(f"[ENV] 1025 LISTENING pid={ds_pid} {name[0][:60] if name else '?'}",
                  file=sys.stderr)
        else:
            print("[ENV] WARNING: 1025 无 LISTENING！DebugServer 未就绪", file=sys.stderr)
    except Exception:
        pass

    start = time.time()
    proc, proc_lines = _spawn_test_run(case_dir)
    outcome = "timeout"
    last_activity = start
    prev_len = 0
    boot_time = None
    last_log_sec = -1
    last_idle_warn = -1

    while True:
        now = time.time()
        ctrl_txt, tgt_txt = ctrl_cap.text, tgt_cap.text
        cur_len = len(ctrl_txt) + len(tgt_txt)
        if cur_len != prev_len:
            prev_len = cur_len
            last_activity = now
        if _has_boot() and boot_time is None:
            boot_time = now
        if int(now - start) % 3 == 0 and int(now - start) != last_log_sec:
            last_log_sec = int(now - start)
            print(f"  [t+{int(now - start)}s] boot={boot_time is not None} "
                  f"ctrl={len(ctrl_cap.lines)} tgt={len(tgt_cap.lines)} "
                  f"idle={now - last_activity:.1f}s", file=sys.stderr)

        # gdb 是启动器：continue 后固件独立运行，gdb 死（RUN_TIMEOUT 到点）
        # 不影响固件——只把"ZT_BOOT 都没见就退出"当作环境失败
        if proc.poll() is not None and boot_time is None:
            outcome = "load_fail"
            break
        if boot_time is None and now - start > LOAD_WINDOW_S:
            outcome = "load_fail"    # 烧录后迟迟无 ZT_BOOT：环境问题
            break
        # 静默判据：ZT_BOOT 后连续 IDLE_S 无新字节。★ 2026-08-25 用户反馈
        # "测试跑到一半被截断"：慢速 case（400kHz）相邻 token 间隔偶发 >10s，
        # 静默在测试中途误触发 → 提前杀 gdb 截断后半套件。修正：静默超时
        # 且已见**本固件 PROJECT EXECUTION**（§10.30 门控）才算 done；中途
        # 静默继续等（case_timeout 硬上限兜底），每 15s 打一条诊断。
        if (boot_time is not None and now - last_activity > IDLE_S):
            if _has_completion_marker():
                outcome = "done"     # 套件已自报结束（PROJECT EXECUTION）
                break
            if int(now - start) != last_idle_warn:
                last_idle_warn = int(now - start)
                print(f"  [t+{int(now - start)}s] 静默 {now - last_activity:.0f}s 但无完成标记"
                      "（PROJECT EXECUTION），继续等待", file=sys.stderr)
        if now - start > case_timeout:
            outcome = "timeout"
            break
        time.sleep(0.3)

    if proc.poll() is None:
        _kill_proc_group(proc)
    run_rc = proc.returncode if proc.returncode is not None else 0

    if outcome == "load_fail":
        print(f"[RUN] {case_dir.name} spawn 后 {LOAD_WINDOW_S}s 未见 ZT_BOOT"
              f"——环境问题（DebugServer 残留/烧录链路/镜像陈旧），非测试失败",
              file=sys.stderr)
        if proc_lines:
            print("[RUN] gdb 输出尾部（诊断）：", file=sys.stderr)
            for l in proc_lines[-6:]:
                print(f"    {l.rstrip()}", file=sys.stderr)
    elif outcome == "timeout":
        print(f"[RUN] {case_dir.name} 超过 {case_timeout}s 未完成，强杀", file=sys.stderr)

    time.sleep(0.5)
    ctrl_cap.stop()
    tgt_cap.stop()
    elapsed = time.time() - start
    print(f"[RUN] {case_dir.name} finished in {elapsed:.1f}s (rc={run_rc}, {outcome})")

    result = {
        "case": case_dir.name,
        "ctrl_text": ctrl_cap.text,
        "tgt_text": tgt_cap.text,
        "ctrl_lines": ctrl_cap.plain_lines(),
        "tgt_lines": tgt_cap.plain_lines(),
        "run_rc": run_rc,
    }
    if outcome == "load_fail":
        result["load_failed"] = True
    return result


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
    """双核判定（ztest 原生）：两路 lines 各 parse_ztest 后合并。

    - 测试项判定 = 同名项双核 PASS（CTRL+TGT）；单核缺失走 §12.3 内存兜底
    - case 判定 = 双核 execution 均 "SUCCESSFUL" + 全部项双核 PASS
    - ctrl_done/tgt_done 字段 = 两核 execution 值（ztest 无 DONE token，
      PROJECT EXECUTION 是套件完成标记）

    返回结构保持双核样板形状：{name, result, ctrl_done, tgt_done,
    mem_unavailable, tests:[{name,ctrl,tgt,src,info,reason,result}]}。
    """
    ctrl_p = parse_ztest(ctrl_lines, ctrl_banners=CTRL_BANNERS,
                         tgt_banners=TGT_BANNERS)
    tgt_p = parse_ztest(tgt_lines, ctrl_banners=CTRL_BANNERS,
                        tgt_banners=TGT_BANNERS)
    per_core = {"CTRL": ctrl_p, "TGT": tgt_p}

    tests = []
    case_pass = True
    ev_srcs = []   # 每项×每核的证据来源（R7「批量必须靠 token」判据用）
    for name in test_names:
        statuses: Dict[str, str] = {}
        srcs: Dict[str, str] = {}
        for core in ("CTRL", "TGT"):
            t = per_core[core]["tests"].get(name)
            st = t["result"] if t else "MISSING"
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
            ev_srcs.append(src)
        test_pass = statuses["CTRL"] == "PASS" and statuses["TGT"] == "PASS"
        if not test_pass:
            case_pass = False

        # INFO 数值拼接（实际列）："CTRL:k=v,k=v; TGT:..."，无 INFO 留空
        info_parts = []
        for core in ("CTRL", "TGT"):
            kv = ", ".join(f"{k}={v}"
                           for k, v in sorted(per_core[core]["infos"].get(name, {}).items()))
            if kv:
                info_parts.append(f"{core}:{kv}")
        # reason 合并：双核 token reason + 内存 reason（§13.5 可归因格式）
        reason_parts = []
        for core in ("CTRL", "TGT"):
            t = per_core[core]["tests"].get(name)
            if t and t["reason"]:
                reason_parts.append(f"{core}:{t['reason']}")
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

    ctrl_done = ctrl_p["execution"]
    tgt_done = tgt_p["execution"]
    # 零证据守卫（2026-09-18；同日按裁定**收紧一档**）：
    #   ① tests 为空 ⇒ 一个测试项都没有 ⇒ 不得判 PASS。
    #   ② **某一核整体没有确定结果**（该核全部项既非 PASS 也非 FAIL —— 串口未捕到
    #      **且** 内存兜底也没给出值）⇒ 不得因"另一核完好"而判 PASS。
    #      理由：那一核根本没跑 / 跑崩了且兜底也读不出，两者都不是可容忍的丢行；
    #      丢行本该由 §12.3 内存兜底补回（兜底给出确定结果 ⇒ 不算零证据，仍可 PASS）。
    # ★ 边界：**只堵整核零证据**；部分项缺失仍走 incomplete 容忍（原设计不变）。
    # ★ 与"测坏了"（显式 FAIL token）**分开表述**：这是"没测到"，理由串不混。
    _gaps = []
    for _core in ("CTRL", "TGT"):
        _sts = [t[_core.lower()] for t in tests]
        if _sts and not any(s in ("PASS", "FAIL") for s in _sts):
            _gaps.append(_core)
    _gap_note = ""
    if _gaps:
        _gap_note = ("证据缺失（非驱动失败）：" + "、".join(
            f"{c} 核全部 {len(tests)} 项无确定结果（串口未捕到 + 内存兜底无结果）"
            for c in _gaps) + " ⇒ 不得因另一核完好判 PASS")
    if (not has_evidence(tests) or _gaps or ctrl_done != "SUCCESSFUL" or tgt_done != "SUCCESSFUL"):
        case_pass = False
    # F1/F2（2026-09-18）：见 common/judge.py。
    # F1：收尾标记未到时，只要**全部已声明测试项在两核都有确定结果**（无 MISSING）
    #     且全 PASS ⇒ 判 PASS 并标注 TRUNCATED_AFTER_ALL_ITEMS；任一已声明项在
    #     任一核无证据 ⇒ 维持 FAIL（红线）。
    # F2：mem_state 三态（ok / invalid / no_sentinel_symbol）进报告，读者不用翻日志。
    _status = {}
    for _t in tests:
        if _t["ctrl"] == "MISSING" or _t["tgt"] == "MISSING":
            _status[_t["name"]] = "MISSING"
        elif _t["ctrl"] == "PASS" and _t["tgt"] == "PASS":
            _status[_t["name"]] = "PASS"
        else:
            _status[_t["name"]] = _t["result"]
    _trunc_ok, _missing = truncation_evidence(ctrl_done, test_names, _status)
    truncated = False
    if (not case_pass and _trunc_ok and tgt_done != "FAILED" and tests
            and not _gaps and all(_t["result"] == "PASS" for _t in tests)):
        case_pass = True
        truncated = True
    _mem_state, _mem_note = mem_state_of_payload(mem_data)
    # R6（2026-09-19）：相反收尾标记并存 ⇒ parse_ztest 已 fail-closed 归成 FAILED；
    # 这里把"矛盾"本身显式写进报告（读者要能一眼看出是矛盾，而不是笼统 FAIL）。
    _conf_seen = sorted(set(ctrl_p.get("exec_seen", [])) | set(tgt_p.get("exec_seen", [])))
    _conf = bool(ctrl_p.get("exec_conflict") or tgt_p.get("exec_conflict"))
    _conf_note = exec_conflict_note(_conf_seen) if _conf else ""

    # R7（2026-09-20）批量必须靠 token：**全项**证据都来自 mem 兜底 ⇒ 不得判 PASS。
    # 判据与理由串的唯一来源 = common/judge.py（mem_only_evidence / MEM_ONLY_NOTE）；
    # 理由串挂在 mem_note 上 ⇒ 控制台 / .md / .json 三处同源可见。
    if mem_only_evidence(ev_srcs):
        case_pass = False
        _mem_note = ((_mem_note + "；") if _mem_note else "") + MEM_ONLY_NOTE

    return {
        "name": case_name,
        "result": "PASS" if case_pass else "FAIL",
        "ctrl_done": exec_label(ctrl_done, truncated,
                                 conflict=bool(ctrl_p.get("exec_conflict"))),
        "tgt_done_conflict": bool(tgt_p.get("exec_conflict")),
        "exec_conflict": _conf,
        "exec_conflict_note": _conf_note,
        "tgt_done": tgt_done,
        "mem_unavailable": mem_data is None,
        "mem_state": _mem_state,
        "zero_evidence": not has_evidence(tests),
        "evidence_gap": _gaps,
        "evidence_note": _gap_note,
        "mem_note": _mem_note,
        "tests": tests,
    }


def print_report(results: List[Dict]) -> None:
    print("\n" + "=" * 80)
    print("I3C/I2C Switch Dual-Core Regression Report")
    print("=" * 80)

    overall = "PASS"
    for case in results:
        print(f"\n## {case['name']}: {case['result']}")
        if case["mem_unavailable"]:
            print("  (mem-unavailable: 内存兜底未生效，MISSING 项按 FAIL 判定)")
        print("  " + mem_state_line(case.get("mem_state", ""),
                                    case.get("mem_note", "")))
        if case.get("evidence_gap"):
            print("  " + case.get("evidence_note", ""))
        if case.get("exec_conflict"):
            print("  " + case.get("exec_conflict_note", ""))
        print(f"| Test | Controller | Target | Result | Source | INFO(actual) | Reason |")
        print(f"|------|------------|--------|--------|--------|--------------|--------|")
        for t in case["tests"]:
            print(f"| {t['name']} | {t['ctrl']} | {t['tgt']} | {t['result']} "
                  f"| {t['src']} | {t['info']} | {t['reason']} |")
        print(f"EXECUTION: CTRL={case['ctrl_done']}, TGT={case['tgt_done']}")
        if case["result"] != "PASS":
            overall = "FAIL"

    print("\n" + "=" * 80)
    print(f"OVERALL: {overall}")
    print("=" * 80)


def write_markdown_report(results: List[Dict], expected: Dict[str, str],
                          report_dir: Path, driver_name: str) -> None:
    """§6.4.2 报告：列序 / 表头 / 单元格转义统一走 common/judge.py。

    与全家族逐字一致（REPORT_TABLE_HEADER）；「期望」列超长截断（完整值见
    tests/COVERAGE.md）。多 case 时表头字段取聚合值，逐 case 明细见下方小节。
    """
    now = datetime.now()
    all_tests = [t for c in results for t in c["tests"]]
    passed = sum(1 for t in all_tests if t["result"] == "PASS")
    failed = sum(1 for t in all_tests if t["result"] == "FAIL")
    overall = "PASS" if all(r["result"] == "PASS" for r in results) else "FAIL"
    single = len(results) == 1
    names = "、".join(c["name"] for c in results) or "-"
    if single:
        exec_txt = (f"CTRL={results[0].get('ctrl_done')}, "
                    f"TGT={results[0].get('tgt_done')}")
        mem_txt = mem_state_line(results[0].get("mem_state", ""),
                                 results[0].get("mem_note", ""))
    else:
        exec_txt = "; ".join(
            f"{c['name']}: CTRL={c.get('ctrl_done')}, TGT={c.get('tgt_done')}"
            for c in results)
        mem_txt = "; ".join(
            f"{c['name']}: " + mem_state_line(c.get("mem_state", ""),
                                              c.get("mem_note", ""))
            for c in results)

    lines = [
        f"# {driver_name} 测试报告",
        "",
        *format_report_header(
            driver_name, names, "lsqsh_evb@2os",
            date=f"{now:%Y-%m-%d %H:%M:%S}",
            env="见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在"
                " COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）",
            execution=exec_txt, mem_line=mem_txt,
            result=f"{overall} ({passed}/{len(all_tests)}"
                   + (f"；失败 {failed}" if failed else "") + ")"),
        "",
    ]
    for case in results:
        lines.append(f"## {case['name']} ({case['result']})")
        lines.append("")
        if case.get("mem_unavailable"):
            lines.append("> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定")
        if case.get("zero_evidence") or not case.get("tests"):
            lines.append("> 零证据: 未捕获到任何测试项 ⇒ 不得判 PASS")
        if case.get("evidence_gap"):
            lines.append("> " + case.get("evidence_note", ""))
        if case.get("exec_conflict"):
            lines.append("> " + case.get("exec_conflict_note", ""))
        lines.append("")
        lines.append(REPORT_TABLE_HEADER)
        lines.append(REPORT_TABLE_SEP)
        for t in case.get("tests", []):
            lines.append(md_table_row([
                t["name"], result_text(t["result"]), source_label(t.get("src")),
                clip_cell(expected.get(t["name"], "") or "-"),
                t.get("info") or "-", t.get("reason", "")]))
        lines.append("")
    lines.append(f"## 汇总\n\nOVERALL: {overall}\n")

    path = report_dir / f"{REPORT_BASE}.md"
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


def list_serial_ports() -> List[str]:
    return [p.device for p in serial.tools.list_ports.comports()]


# ---- L2 处理层规格（common/process.py）：只声明**本工程特有**的接入点 ----
# 本工程判定族特有（D3）：双核同名项 + CTRL 收尾标记 + TGT 结构性无收尾标记的例外
# 规则、mem 兜底、R6 矛盾标记、R7 token 守卫 —— 全在 evaluate_case 里，原样保留。
# 归档按核分文件（serial_CTRL.log / serial_TGT.log）⇒ L2 直接用两个核的行。
# **聚合形态**：一次跑多个 case，但只写**一份**报告（results 列表整体交给渲染）。
# 契约：每工程暴露 l2_spec(facts=None, test_names=None)；facts = 未归档采集事实。
def l2_spec(facts=None, test_names=None):
    test_names = test_names or TEST_NAMES
    expected = parse_expected_from_coverage()

    def _judge(ci):
        return evaluate_case(ci.case, ci.lines.get("CTRL", []),
                             ci.lines.get("TGT", []), test_names, ci.mem)

    def _render_md(jl, cdir):
        write_markdown_report(jl, expected, cdir, DRIVER_NAME)
        return cdir / f"{REPORT_BASE}.md"

    def _render_json(jl, cdir):
        write_json_report(jl, cdir / f"{REPORT_BASE}.json")
        return cdir / f"{REPORT_BASE}.json"

    return process.ProcessSpec(
        project=DRIVER_NAME, driver=DRIVER_NAME, report_base=REPORT_BASE,
        judge=_judge, render_md=_render_md, render_json=_render_json,
        console=lambda jl: print_report(jl),
        status=lambda j: j["result"],
        summary=lambda jl: (
            "PASS" if all(x["result"] == "PASS" for x in jl) else "FAIL",
            sum(1 for c in jl for t in c["tests"] if t["result"] == "PASS"),
            sum(1 for c in jl for t in c["tests"] if t["result"] != "NOT_RUN")),
        log_archived=lambda jp, mp: print(f"[run] archived -> {mp}"),
    )


def main() -> int:
    # 行缓冲输出：批量回归跑 ~10min 级，块缓冲会让 [BUILD]/[RUN] 进度
    # 全程不可见（误判为"卡死"）
    try:
        sys.stdout.reconfigure(line_buffering=True)
        sys.stderr.reconfigure(line_buffering=True)
    except Exception:
        pass

    parser = argparse.ArgumentParser(description="I3C/I2C switch dual-core regression agent (ztest-native)")
    parser.add_argument("--case", help="Run a single case (default: all case_* dirs)")
    parser.add_argument("--ctrl-uart", help="Controller UART port")
    parser.add_argument("--tgt-uart", help="Target UART port")
    parser.add_argument("--session-dir", default=None,
                        help="归档会话目录（reports/<date>-<idx> 或任意路径）；"
                             "不带则按当天自动分配新会话目录")
    parser.add_argument(
        "--case-timeout",
        type=int,
        default=int(os.environ.get("I3C_CASE_TIMEOUT", "60")),
        help="Seconds per case run (default: 60; 仅兜底——正常由 10s 静默判据提前结束)",
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
    session_dir = None   # 延迟分配：原始串口归档与报告共用同一会话目录

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

    test_names = TEST_NAMES
    # L2 规格：expected 由 l2_spec() 内部构造
    _L2_SPEC = l2_spec({"ctrl_uart": ctrl_port, "tgt_uart": tgt_port}, test_names)
    print(f"Tests per case: {test_names}")

    # Run phase: preflight -> each case: clean residue -> run -> clean residue -> backfill
    preflight_gdb()
    results: List[Dict] = []
    for case_dir in cases:
        clean_gdb_residue()  # 上一 case 杀 gdb 的残留不带到本 case
        # load_fail / 环境失败自动修复重试：重启 DebugServer（含 5s 稳定期，
        # 见 restart_debugserver 注释）后重跑该 case 1 次——"重启后立即连跑
        # 不稳定"实测 2/2 复现（load_fail/断连/WDR 时序异常），稳定期后再跑
        # 可自愈；重试仍失败才中止/判 FAIL（不掩盖真环境故障）。
        raw = None
        for attempt in range(2):
            try:
                raw = run_case(case_dir, ctrl_port, tgt_port, args.case_timeout)
            except RuntimeError as exc:
                if attempt == 0:
                    print(f"[RETRY] {case_dir.name}: 环境失败（{exc}）→ "
                          "重启 DebugServer 后重试", file=sys.stderr)
                    restart_debugserver()
                    clean_gdb_residue()
                    continue
                print(f"ERROR: {exc}", file=sys.stderr)
                return 1
            if raw.get("load_failed") and attempt == 0:
                print(f"[RETRY] {case_dir.name}: load_fail（环境问题）→ "
                      "重启 DebugServer 后重试", file=sys.stderr)
                restart_debugserver()
                clean_gdb_residue()
                continue
            break
        clean_gdb_residue()  # 本 case 杀 gdb 的残留不带给 backfill/下一 case
        # L1→L2 边界（L0 归档形状）：两核原始行**分核**归一；mem 兜底是 L1 侧产物
        # （读目标内存，不进归档）⇒ 作为独立入参交给 L2（common/process.py）。
        lines_by_core = {"CTRL": raw["ctrl_lines"], "TGT": raw["tgt_lines"]}
        if not args.no_archive:
            session_dir = session_dir or (
                Path(args.session_dir) if args.session_dir
                else allocate_session_dir(REPORTS_ROOT, date_str))
            archive_serial(case_dir_for(session_dir, DRIVER_NAME, case_dir.name),
                           case_dir.name, lines_by_core, board="lsqsh_evb@2os",
                           ports={"CTRL": ctrl_port, "TGT": tgt_port},
                           timeout=args.case_timeout, script=__file__)
        mem = memory_backfill(case_dir)
        results.append(process.judge(_L2_SPEC, process.L2Input(
            case=case_dir.name, lines=lines_by_core, mem=mem)))

    print_report(results)

    if args.no_archive:
        print("\n--no-archive: skipping reports/ archiving.")
        return 0 if all(r["result"] == "PASS" for r in results) else 1

    # Archive (§12)：会话目录 reports/<date>-<idx>/，每 case 子目录
    # <driver>_<case>/<driver>_test_report.{md,json} + summary.md 索引行
    # （落盘 / 汇总走 common/process.py::emit，与既有写法逐字一致）。
    date_str = datetime.now().strftime("%Y-%m-%d")
    session_dir = session_dir or (
        Path(args.session_dir) if args.session_dir
        else allocate_session_dir(REPORTS_ROOT, date_str))
    case_label = args.case or (cases[0].name if len(cases) == 1 else "all_cases")
    process.emit(_L2_SPEC, results, session_dir, case_label=case_label)
    return process.rc(_L2_SPEC, results)


if __name__ == "__main__":
    sys.exit(main())
