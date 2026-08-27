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
import shutil
import signal
import sys
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


# tests/scripts/run_i3c_tests.py -> parents[0]=tests/scripts -> [1]=tests -> [2]=<driver>_test root
SAMPLE_ROOT = Path(__file__).resolve().parents[2]


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
    """清 DebugServer 连接残留（gdb continue 被杀后的高频坑，见 810 CLAUDE.md §10.1）。

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
    time.sleep(30)           # 稳定期：新实例 CKLink 初始化/总线状态稳定后再连跑


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
PROTOCOL_H = TESTS_DIR / "include" / "i3c_dualcore_test_protocol.h"
# 归档根：810_driver_test/reports/<YYYY-MM-DD>/（CLAUDE.md §12.1）
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

# 注意：不用 ^ 锚点、用 .search()（CLAUDE.md §10.5）——shell 提示符
# 重绘时 token 行前会带 "uart:~$ " 前缀（实测 token 行大量带前缀）。
# DONE_RE 支持可选 core：controller 打 "I3C_TEST:DONE:PASS"（无 core），
# target 打 "I3C_TEST:TGT:DONE:PASS"（带 core，I3C_TEST_TGT_DONE_VAR）。
TOKEN_RE = re.compile(
    r"I3C_TEST:(CTRL|TGT):(START|PASS|FAIL):(\w+)(?::(.*))?\s*$"
)
INFO_RE = re.compile(r"I3C_TEST:(CTRL|TGT):INFO:(\w+):([\w.=,\-]+)\s*$")
DONE_RE = re.compile(r"I3C_TEST:(?:(CTRL|TGT):)?DONE:(PASS|FAIL)\s*$")

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
        _kill_proc_group(proc)
        return proc.returncode if proc.returncode is not None else -1


def run_until_done(cmd: List[str], cwd: Path, timeout: int,
                   is_done, grace_s: float = 3.0) -> int:
    """（备选完成判据，当前未启用）跑命令直到 is_done() 为真或超时。

    注意：该判据依赖串口 token（如 I3C_TEST:DONE），串口异常时完全失效，
    且各驱动测试工程的 token 前缀不同（非 I3C 专有前缀不可用），
    通用性不如纯超时方案。保留供特定场景显式启用。
    """
    kwargs = {"cwd": str(cwd)}
    if sys.platform == "win32":
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP

    proc = subprocess.Popen(cmd, **kwargs)
    deadline = time.time() + timeout
    done_since = None
    while time.time() < deadline:
        if is_done():
            if done_since is None:
                done_since = time.time()
            if time.time() - done_since >= grace_s:
                break
        else:
            done_since = None
        rc = proc.poll()
        if rc is not None:
            return rc
        time.sleep(0.5)

    if proc.poll() is None:
        _kill_proc_group(proc)
    return proc.returncode if proc.returncode is not None else 0


def _spawn_test_run(case_dir: Path):
    """启动 test.sh -r，返回 (proc, proc_lines)。gdb 输出经 PIPE 线程采集。

    RUN_TIMEOUT 放宽到 120s：continue 后 gdb 挂住等目标（正常），固件独立
    运行；烧录+BOOT 全程从容完成。gdb 死活不影响固件，由上层判据控制。
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


def _wait_continue(proc, proc_lines, start, cap_s=60):
    """等 gdb 的 "Continuing"（load 完成标志）；返回时刻或 None（失败/超时）。

    gdb 提前退出（连接被占/目标不可达，-batch 下秒级失败）必须立即返回，
    否则空等满 cap_s（批量回归曾因此单个 case 卡 60s）。
    """
    while time.time() - start < cap_s:
        if any("Continuing" in l for l in proc_lines):
            return time.time()
        if proc.poll() is not None:
            return None
        time.sleep(0.3)
    return None


def run_case(case_dir: Path, ctrl_port: str, tgt_port: str, case_timeout: int) -> Dict:
    """单个 case 的烧录+运行+采集（两遍烧录法，810 CLAUDE.md §10.4）。

    判据（串口字节流为准，不依赖 gdb 输出——Windows gdb stdout 经管道
    块缓冲，load 后的 "Continuing." 可能永远读不到，实测必现）：
    1. 两遍烧录：prime 轮跑到串口出现 BOOT 标记（固件跑起来 = load 完成，
       冲刷 I-cache）再等 5s 即杀；清残留后正式轮再烧同一镜像；
    2. 环境判定：正式轮 spawn 后 LOAD_WINDOW_S 内收不到 BOOT 标记 →
       判定测试环境有问题（区别于测试失败）；
    3. 静默判完成：BOOT 之后连续 IDLE_S 无新字节 → 套件已结束，提前杀
       （BOOT 前的陈旧输出不计入——那是上一轮镜像的残留）；
    4. case_timeout 硬上限兜底。
    """
    LOAD_WINDOW_S = 20
    IDLE_S = 10
    P1_HOLD_S = 5

    print(f"[RUN] {case_dir.name}")
    # 上个 case 结束时 gdb 被杀必然留残留连接（DebugServer 单连接，
    # 残留占着则本次全部 load 失败）→ 每个 case 开头先清一次（qsh-debug §2.3）
    # （main() 循环已调 clean_gdb_residue，这里不再重复——clean 已改为按需
    #  重启，但重复调用仍会各跑一次 netstat；保持单一入口防 DebugServer 高频开关）

    ctrl_cap = SerialCapture(ctrl_port)
    tgt_cap = SerialCapture(tgt_port)
    ctrl_cap.start()
    tgt_cap.start()
    if ctrl_cap.error:
        raise RuntimeError(ctrl_cap.error)
    if tgt_cap.error:
        raise RuntimeError(tgt_cap.error)

    def _has_boot() -> bool:
        return (":BOOT" in ctrl_cap.text) or (":BOOT" in tgt_cap.text)

    # 套件完成标记：SUMMARY:END 或 DONE（任一核出现即认为套件自报结束）。
    # 静默判据以此为准——10s 静默但无标记 = 测试中途长间隔，不能判完成。
    def _has_completion_marker() -> bool:
        return ((":SUMMARY:END" in ctrl_cap.text) or (":SUMMARY:END" in tgt_cap.text)
                or (":DONE:" in ctrl_cap.text) or (":DONE:" in tgt_cap.text))

    # prime 轮固件跑的是 I-cache 残留的旧镜像（无 BOOT 标记的旧固件）——
    # 判据用"任何 I3C_TEST token"（旧固件的 START/SUMMARY 也算），
    # 只等 BOOT 会误判环境失败。BOOT 是 pass-2（新固件）的判据。
    def _has_any_token() -> bool:
        return (_has_boot()
                or "I3C_TEST:" in ctrl_cap.text
                or "I3C_TEST:" in tgt_cap.text)

    # ---- pass 1 (prime)：冲刷 I-cache 旧镜像，输出丢弃 ----
    print(f"[RUN] {case_dir.name} pass-1 (prime, I-cache flush)")
    proc, proc_lines = _spawn_test_run(case_dir)
    p1_start = time.time()
    p1_boot = None
    while time.time() - p1_start < LOAD_WINDOW_S:
        if _has_any_token():
            p1_boot = time.time()
            break
        if proc.poll() is not None:
            break
        time.sleep(0.3)
    _kill_proc_group(proc)
    clean_gdb_residue()
    if p1_boot is None:
        raise RuntimeError(
            f"{case_dir.name}: pass-1 烧录后 {LOAD_WINDOW_S}s 内无任何固件输出"
            "（I3C_TEST token）——测试环境有问题（DebugServer 残留/烧录链路/镜像陈旧）")
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
        # 不影响固件——只把"BOOT 都没见就退出"当作环境失败
        if proc.poll() is not None and boot_time is None:
            outcome = "load_fail"
            break
        if boot_time is None and now - start > LOAD_WINDOW_S:
            outcome = "load_fail"    # 烧录后迟迟无 BOOT：环境问题
            break
        # 静默判据：BOOT 后连续 IDLE_S 无新字节。★ 2026-08-25 用户反馈
        # "测试跑到一半被截断"：慢速 case（400kHz）相邻 token 间隔偶发 >10s，
        # 静默在测试中途误触发 → 提前杀 gdb 截断后半套件。修正：静默超时
        # 且已见**套件完成标记**（SUMMARY:END 或 DONE）才算 done；中途静默
        # 继续等（case_timeout 硬上限兜底），每 15s 打一条诊断。
        if (boot_time is not None and now - last_activity > IDLE_S):
            if _has_completion_marker():
                outcome = "done"     # 套件已自报结束（SUMMARY:END/DONE）
                break
            if int(now - start) != last_idle_warn:
                last_idle_warn = int(now - start)
                print(f"  [t+{int(now - start)}s] 静默 {now - last_activity:.0f}s 但无完成标记"
                      "（SUMMARY:END/DONE），继续等待", file=sys.stderr)
        if now - start > case_timeout:
            outcome = "timeout"
            break
        time.sleep(0.3)

    if proc.poll() is None:
        _kill_proc_group(proc)
    run_rc = proc.returncode if proc.returncode is not None else 0

    if outcome == "load_fail":
        print(f"[RUN] {case_dir.name} spawn 后 {LOAD_WINDOW_S}s 未见 BOOT 标记"
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
        "ctrl_lines": [line for _, line in ctrl_cap.lines],
        "tgt_lines": [line for _, line in tgt_cap.lines],
        "run_rc": run_rc,
    }
    if outcome == "env_fail":
        result["env_failed"] = True
    elif outcome == "load_fail":
        result["load_failed"] = True
    return result



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
        m = TOKEN_RE.search(line)
        if m:
            core, outcome, name, reason = m.groups()
            outcomes.setdefault(name, {})
            if outcome == "FAIL":
                outcomes[name][core] = "FAIL"
                if reason:
                    reasons.setdefault(name, {})[core] = reason
            elif outcome == "PASS" and outcomes[name].get(core) != "FAIL":
                outcomes[name][core] = "PASS"
        m2 = INFO_RE.search(line)
        if m2:
            core, name, kv = m2.groups()
            infos.setdefault(name, {})[core] = kv
        m3 = DONE_RE.search(line)
        if m3:
            done = m3.group(2)
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
    # 行缓冲输出：批量回归跑 ~10min 级，块缓冲会让 [BUILD]/[RUN] 进度
    # 全程不可见（误判为"卡死"）
    try:
        sys.stdout.reconfigure(line_buffering=True)
        sys.stderr.reconfigure(line_buffering=True)
    except Exception:
        pass

    parser = argparse.ArgumentParser(description="I3C dual-core regression agent")
    parser.add_argument("--case", help="Run a single case (default: all case_* dirs)")
    parser.add_argument("--ctrl-uart", help="Controller UART port")
    parser.add_argument("--tgt-uart", help="Target UART port")
    parser.add_argument(
        "--case-timeout",
        type=int,
        default=int(os.environ.get("I3C_CASE_TIMEOUT", "60")),
        help="Seconds per case run (default: 60; 仅兜底——正常由 10s 看门狗/10s 静默判据提前结束)",
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

    # Run phase: preflight -> each case: clean residue -> run -> clean residue -> backfill
    preflight_gdb()
    results: List[Dict] = []
    for case_dir in cases:
        clean_gdb_residue()  # 上一 case 杀 gdb 的残留不带到本 case
        # load_fail / 环境失败自动修复重试：重启 DebugServer（含 30s 稳定期，
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
        if raw.get("env_failed"):
            # BOOT 标记未到 = 测试环境有问题（烧录链路/串口/镜像陈旧），
            # 继续跑只会批量产出假 FAIL——直接中止
            print("ERROR: 测试环境异常（未收到 BOOT 标记），中止回归。"
                  "请检查 DebugServer/串口/接线后重试", file=sys.stderr)
            return 2
        clean_gdb_residue()  # 本 case 杀 gdb 的残留不带给 backfill/下一 case
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
