#!/usr/bin/env python3
"""L1 采集层（acquisition layer）：**唯一碰板子的一层**（A2，2026-09-20）。

## 这个文件是什么

`run_test.py` 的 `main()` 此前把七件事揉在一段里（构建 / 烧录 / 采集 / 判定 /
渲染 / 落盘 / 汇总）。A1 已把 **L2 处理层**（采集完 → 判定 → 报告）提到
`common/process.py`；本模块把**它之前的那一段**（构建 → 烧录 → 串口采集 →
落 L0 归档 → 释放资源）提成接口。

    L1 采集（本模块，唯一碰板子）──► L0 归档 = serial_<CORE>.log + replay_meta.json
                                        │
    L2 处理（common/process.py，纯离线）◄┘

## 输入输出契约

**输入**（`*Spec` 的字段，见 §规格）：case 名 / 工程根 / board / 串口（或自动发现）
/ timeout / 构建参数 / 采集形态参数。

**输出**（`Acquired`）：

* `lines`  = `{核名: [原始行]}` —— **就是归档进去的那一份**，也是交给 L2 的同一个对象；
* `facts`  = **未归档的采集事实**（`timed_out` / `elapsed_s` / `load_failed`…），
  只影响报告里"采集时长"这类明确不参与判定的字段；
* `mem`    = 内存兜底 payload（`dump_results.py --json` 的产物）。★ 它是 **L1 侧
  产物、不进归档**（当前 L0 契约的已知缺口 D7）；
* `session`/`case_dir` = L0 归档落点（`session` 由本层分配并回传，调用方不得重复分配）；
* `ok` / `err` / `rc` —— 采集成功与否 + 需要中止时的返回码。

**契约要点**：L2 的输入里**没有**端口名 / 超时 / gdb —— 那些只作 `replay_meta.json`
的元信息。这正是"改处理层不必重跑板子"成立的形式化条件。

## 为什么是「5 个策略 + 逐字搬运的原语」而不是「一个通用采集脚本」

24 份 `run_test.py` 的采集差异分 5 个**真实语义形态**（与 `regression/replay.py`
的适配器分组同源）：SINGLE_CAP / PORTS_DICT / DUAL_RUN / MBOX / IWDG。差异是
真实的（两遍烧录 / 单核 hold cpu2 / mbox 字节块 / iwdg 双 pass / rtc_ls 无 hold），
不是噪音。把 `if project == ...` 写进本文件 = 把 24 份的分叉搬进一个文件。

所以：

  * 本文件的**函数体一律逐字搬运**自原 `run_test.py`（含注释与空行习惯），
    只把原来的模块级全局（`SAMPLE_ROOT` / `TESTS_DIR` / `BASH` / `BOARD` /
    `TARGET_ROM_START`）改成显式形参或 spec 字段；
  * 工程差异走 **spec 字段 / 钩子**，不走分支；
  * 每个策略一个入口，工程薄壳只声明"我用哪个策略、参数是什么"。

## ★ 时序常量是实测值，不得"整理"、不得"优化"、不得改数值

两遍烧录的 prime 轮、`IDLE_S = 10`、`LOAD_WINDOW_S = 20`、`P1_HOLD_S = 5`、
单核 hold cpu2 的 `0x4002200c = 2`、写包提速 `16384`、以及各处的 `time.sleep`
都是板上实测调出来的。本模块**逐字保留**它们。

## 设计约束

1. **只碰硬件、不判定**：本模块不 import `judge` / `ztest_report`，不产出任何 PASS/FAIL
   （`ztest_report.parse_ztest` 只在 DUAL_RUN 的"是否见到 BOOT/收尾标记"门控里用，
   与 L2 的判定同源但用途是**决定何时停止采集**，不是判 PASS/FAIL）；
2. **不 import `process`**：L1 与 L2 无依赖关系（调用方负责把 `lines` 交给 L2）；
3. **行为零变化**：搬运只做"模块全局 → 形参"的重命名，命令序列 / 参数 / 等待时长逐字相同。
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

__all__ = [
    "GDB", "GDB_HOST", "GDB_PORT", "DEBUGSERVER_EXE", "log", "Acquired",
    "SingleCapSpec", "PortsDictSpec", "DualRunSpec", "MboxSpec", "IwdgSpec",
    "SerialCapture", "reader", "discover_ports", "find_tgt_uart", "find_git_bash",
    "memory_backfill", "dump_mem", "archive", "build_via_test_sh", "west_build",
    "netstat_1025", "kill_residual_gdb", "restart_debugserver", "clean_gdb_residue",
    "ensure_debugserver_alive", "DS_HEALTHY_MSG", "dbg_run_preflight",
    "probe_gdb_alive", "preflight_gdb", "kill_proc_group",
    "gdb_flash_once", "gdb_watch_session", "spawn_test_run",
    "run_case_shell", "run_case_dual", "run_case_mbox", "run_pass_iwdg",
    "single_cap", "ports_dict", "ports_dict_case", "dual_run", "mbox", "iwdg",
]

# =============================================================== 常量
# 24 份里 23 份写的是裸名（D3 三份写 `shutil.which(...) or 裸名`）。本机
# PATH 上解析到同一可执行体，故统一用裸名（见交付报告的"声明式差异"一节）。
GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
DEBUGSERVER_EXE = r"C:\uaer_app\debug_server\bin\DebugServerConsole.exe"

# otbn / otbn_selftest / mbedtls（watch 驱动）的三个正则，逐字搬运
ZT_BOOT_RE = re.compile(r"ZT_BOOT TGT")
EXEC_RE = re.compile(r"PROJECT EXECUTION (SUCCESSFUL|FAILED)")
ZTEST_ACTIVITY_RE = re.compile(r"(ZT_BOOT|START - | PASS - | FAIL - | SKIP - "
                               r"|PROJECT EXECUTION|Assertion failed)")
# 22 份通用的 ANSI 重绘前缀（mbox 的 `_ANSI_RE` 少一个 `?`，属 mbox 自有，不在此处）
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def log(msg):
    """24 份 `run_test.py` 逐字同构的 `log()`。"""
    print(msg, flush=True)


def _resolve_session(session):
    """会话目录惰性求值：`session` 可以是 Path、None 或**可调用工厂**。

    ★ 必须惰性：原 `main()` 里 `session_dir = session_dir or (Path(args.session_dir)
    if args.session_dir else allocate_session_dir(REPORTS_ROOT, date_str))` 的**求值
    时机**在"采集成功之后、归档之前"，且 `allocate_session_dir` 有副作用（占序号）。
    传工厂可让 L1 在被调用的同一时点才分配，行为与原码逐字一致。
    """
    return session() if callable(session) else session


def _reports_root(root):
    """24 份逐字同构：`REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"`。"""
    return Path(root).parents[1] / "reports"


# =============================================================== 规格与结果
@dataclass
class Acquired:
    """一次 L1 采集的产出。

    Attributes:
        ok:     采集是否成功。False 时调用方走各自既有的失败分支。
        err:    失败原因（进日志 / 报告诊断）。
        rc:     需要**中止整个回归**时的返回码（DUAL_RUN 的 rc=2 = 环境中止）；
                0 表示"没有中止语义"，调用方按 `ok` 自己决定。
        lines:  `{核名: [原始行]}` = L0 归档内容 = 交给 L2 的同一个对象。
        facts:  未归档的采集事实（timed_out / elapsed_s / load_failed …）。
        mem:    内存兜底 payload（未归档，D7）。
        session/case_dir: L0 归档落点。**`session` 由调用方分配后传入**（会话目录
                分配属 L3 编排职责；`allocate_session_dir` 有"取下一个空闲序号"的
                副作用 ⇒ 全流程只能分配一次，故不放在本层）。
    """
    ok: bool = True
    err: str = ""
    rc: int = 0
    phase: str = ""                   # 失败发生在哪一段：'build' | 'capture'
    lines: dict = field(default_factory=dict)
    facts: dict = field(default_factory=dict)
    mem: object = None
    session: Path = None
    case_dir: Path = None


@dataclass
class SingleCapSpec:
    """SINGLE_CAP（9 份）：`test.sh -r` 驱动 gdb，Python 只采一路串口。"""
    driver: str
    root: Path                       # 工程根（= 原 SAMPLE_ROOT）
    tests_dir: Path
    case: str
    board: str                       # 归档 board 串
    tgt_uart: object = None          # None = 自动发现
    case_timeout: int = 60
    skip_build: bool = False
    archive: bool = True
    session: object = None          # 会话目录（由调用方/main 分配，L3 职责）
    script: str = ""
    bash_style: str = "fixed"        # 'fixed'(Z) | 'filtered'(D3) | 'env_first'(mbox)
    com99_probe: bool = True         # Z 族 _restart_debugserver 里的 COM99 探测（eth 无）
    mem_hook: object = None          # eth: dump_mem 的偏函数


@dataclass
class PortsDictSpec:
    """PORTS_DICT（10 份）：Python 直接驱动 gdb，自己烧录 + 采一路串口。"""
    driver: str
    root: Path
    tests_dir: Path
    case: str
    board: str
    tgt_uart: object = None
    timeout: float = 30
    skip_build: bool = False
    archive: bool = True
    session: object = None          # 会话目录（由调用方/main 分配，L3 职责）
    script: str = ""
    target_rom_start: str = "0x10000000"
    # 构建（west）：build_subdir='target'|''，progress='auto'|'always'
    src_dir: str = ""
    build_subdir: str = "target"
    build_progress: str = "auto"
    build_timeout: int = 900
    build_conf: bool = False          # mbedtls: -DCONF_FILE=<src>/prj.conf;<case>/case.conf
    # 采集形态
    driver_kind: str = "oneshot"      # 'oneshot'（一次性 -ex 链）| 'watch'（交互式 + 提前收尾）
    pre_erase: bool = False           # oneshot: -ex "monitor flash erase_address ..."
    hold_cpu2: bool = True            # 单核 hold cpu2（rtc_ls 不 hold）
    stall_s: int = 60                 # watch: 无 token 卡死提前收尾阈值（mbedtls=300）
    retry_on_no_boot: bool = False    # otbn/otbn_selftest/mbedtls：重启 DS 自愈重试 1 次
    mem: bool = True                  # 是否做内存兜底
    mem_dumper: object = None         # 传给 memory_backfill 的 dumper（相对名或绝对路径）
    mem_cwd: object = None
    mem_verbose: bool = False
    tgt_banners: tuple = ("TGT",)


@dataclass
class DualRunSpec:
    """DUAL_RUN（3 份）：Python 两遍烧录 + 双路串口 + 静默判完成 + 环境自愈重试。"""
    driver: str
    root: Path
    tests_dir: Path
    case: str = ""
    board: str = "lsqsh_evb@2os"
    ctrl_uart: object = None
    tgt_uart: object = None
    case_timeout: int = 60
    skip_build: bool = False
    archive: bool = True
    session: object = None          # 会话目录（由调用方/main 分配，L3 职责）
    script: str = ""
    banner_key: str = "ZT_BOOT"       # _has_any_output 的工程 token 前缀
    announce: bool = False            # i3c_dualcore: run_case 开头多打一行 [RUN] <case>
    abort_on_load_fail: bool = False  # i3c_dualcore: 重试后仍 load_fail ⇒ 中止（rc=2）
    ctrl_banners: tuple = ()
    tgt_banners: tuple = ()
    # 单出口（SPEC_REFERENCE §6.9.4）：非空 ⇒ 从核不本地输出 token，它的行只
    # 出现在主核串口流里（主核加 `<PREFIX>:<core>:` 核信封转发）。L1 按这些
    # 前缀把它们分拣回从核列表，使归档与交给 L2 的 `{核名: [行]}` 与"两核各自
    # 本地输出"同形（重放输入不变）。
    relay_prefixes: tuple = ()
    # 信封是**中继侧新加的壳**（ztest 型从核）⇒ 分拣时剥掉，使 serial_TGT.log
    # 与从核原本地输出逐行等价。命令通道型从核（token 自带前缀）保持 False。
    relay_strip: bool = False


@dataclass
class MboxSpec:
    """MBOX（1 份）：单次 `test.sh -r`、stdout 丢弃、双核早收敛、wmic 兜底。"""
    driver: str
    root: Path
    tests_dir: Path
    case: str
    board: str = "lsqsh_evb@2os"
    ctrl_uart: object = None
    tgt_uart: object = None
    case_timeout: int = 120
    skip_build: bool = False
    archive: bool = True
    session: object = None          # 会话目录（由调用方/main 分配，L3 职责）
    script: str = ""
    # ★ mbox 的采集/归档语义是**字节块**级（与判定同款的两个纯函数）；
    #   它们留在工程侧（属"工程特有的东西"），本层只调用。
    core_view: object = None          # `_core_view(capture_lines, core)`
    split_lines: object = None        # `_split_lines(capture_lines)` → 归档用逐行文本
    combined_lines: object = None     # `_combined_lines(capture_lines)` → 进度日志字节数


@dataclass
class IwdgSpec:
    """IWDG（1 份）：两轮 `run_pass`（prime 丢弃 + capture 为准），双核。"""
    driver: str
    root: Path
    tests_dir: Path
    case: str
    board: str = "lsqsh_evb@2os"
    ctrl_uart: object = None
    tgt_uart: object = None
    case_timeout: int = 120
    skip_build: bool = False
    archive: bool = True
    session: object = None          # 会话目录（由调用方/main 分配，L3 职责）
    script: str = ""
    extra_logs: bool = True           # 本项目独有的 cpu1/cpu2 合并旁证文件
    # 单出口（SPEC_REFERENCE §6.9.3/§6.9.4）：同 DualRunSpec.relay_prefixes /
    # relay_strip —— 从核不出本地 token，它的行只在主核流里（带核信封）。
    relay_prefixes: tuple = ()
    relay_strip: bool = False


# =============================================================== 串口采集
def _serial():
    import serial
    return serial


class SerialCapture:
    """后台线程采串口（24 份 `run_test.py` 里的 4 种实现合并为一个类）。

    `mode` 精确对应原来 4 种实现的**采集与切行语义**：

      * `"line"`       —— Z 族 9 份：`readline()`，去尾 `\\r\\n`，append `(时刻, 行)`；
      * `"line_err"`   —— D3 三份：同上，另在异常时 append 一条 `[serial error: …]`；
      * `"chunk"`      —— mbox：`read(4096)` 原样 append `(时刻, 字节块文本)`（**不切行**），
                          `start()` 里额外 `reset_input_buffer()` 丢开窗前的积压；
      * `"chunk_ansi"` —— app_iwdg：`read(256)` + 按 `\\n` 切 + 去 ANSI，append `(时刻, 行)`。

    四种模式产出的 `lines` / `text` 与各自原实现**逐字相同**（`chunk_ansi` 原实现
    存的是裸字符串，本类统一存 `(时刻, 行)` 二元组；`:text` 的拼接结果不变）。
    """

    def __init__(self, port, baud=115200, *, mode="line"):
        self.port = port
        self.baud = baud
        self.mode = mode
        self.lines = []
        self._stop = threading.Event()
        self._thread = None
        self._ser = None
        self.error = None

    def start(self):
        try:
            self._ser = _serial().Serial(self.port, self.baud, timeout=0.1)
        except Exception as exc:
            self.error = f"failed to open {self.port}: {exc}"
            return
        if self.mode == "chunk":
            # 时序门前置：丢掉本次采集开始前已积压在驱动缓冲里的字节，
            # 避免上一轮固件的收尾输出被当成本轮结果。（mbox start()）
            try:
                self._ser.reset_input_buffer()
            except Exception:
                pass
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        if self.mode == "line":
            while not self._stop.is_set():
                try:
                    data = self._ser.readline()
                except Exception:
                    break
                if data:
                    line = data.decode("utf-8", errors="replace").rstrip("\r\n")
                    self.lines.append((time.time(), line))
        elif self.mode == "line_err":
            while not self._stop.is_set():
                try:
                    data = self._ser.readline()
                except Exception as exc:
                    self.lines.append((time.time(), f"[serial error: {exc}]"))
                    break
                if data:
                    line = data.decode("utf-8", errors="replace").rstrip("\r\n")
                    self.lines.append((time.time(), line))
        elif self.mode == "chunk":
            while not self._stop.is_set():
                try:
                    data = self._ser.read(4096)
                except Exception:
                    break
                if data:
                    self.lines.append((time.time(), data.decode("utf-8", errors="replace")))
        else:  # chunk_ansi
            buf = b""
            while not self._stop.is_set():
                try:
                    chunk = self._ser.read(256)
                except Exception:
                    break
                if not chunk:
                    continue
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    self.lines.append((time.time(), ANSI_RE.sub(
                        "", line.decode("utf-8", "replace").rstrip("\r"))))

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2.0)
        if self._ser and self._ser.is_open:
            self._ser.close()

    def clear(self):
        self.lines = []

    def flush_stale(self, duration=0.3):
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
    def text(self):
        return "\n".join(ln for _, ln in self.lines)

    def plain_lines(self):
        return [ln for _, ln in self.lines]


def reader(ser, lines, stop):
    """PORTS_DICT 10 份逐字同构的读线程体（256B 缓冲切行 + 去 ANSI）。"""
    import serial                     # 原实现的 `except serial.SerialException` 逐字保留
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
    """PORTS_DICT 10 份逐字同构：候选串口 = 显式给定 or 全部非 CKLink 口。"""
    import serial.tools.list_ports as list_ports
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


def find_tgt_uart():
    """SINGLE_CAP 9 份逐字同构：优先 CH342 Serial-B（cpu1 uart1）。"""
    import serial.tools.list_ports as list_ports
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


def find_git_bash(style="fixed"):
    """解析 Git Bash 完整路径（3 种实现合并为 `style`）。

    Windows 下裸 "bash" 会被 C:\\Windows\\System32\\bash.exe（WSL）抢先解析
    （CreateProcess 先搜 System32 再搜 PATH），WSL 里无 west → build rc=127。

      * `"fixed"`     —— Z 族 9 份：只试两个固定路径，否则裸名 "bash"；
      * `"filtered"`  —— D3 三份：GIT_BASH 环境变量 → which → 固定路径，过滤 system32/windowsapps；
      * `"env_first"` —— mbox：GIT_BASH → 两个固定路径 → which。
    """
    if style == "fixed":
        for p in [r"C:\Program Files\Git\bin\bash.exe",
                  r"C:\Program Files (x86)\Git\bin\bash.exe"]:
            if Path(p).exists():
                return p
        return "bash"
    if style == "filtered":
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
    # env_first（mbox）
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
    p = shutil.which("bash")
    if p:
        return p
    return "bash"


# =============================================================== 内存兜底（L1 侧）
def memory_backfill(case, *, dumper, cwd=None, timeout=60, verbose=False):
    """`dump_results.py --json` 读内存兜底。失败返回 None → mem-unavailable。

    ★ 这一步会起 gdb 子进程（碰硬件）⇒ 属**采集层**，不属于判定。

    两种调用形态（逐字对应）：
      * PORTS_DICT 形态：`dumper` = `<tests>/scripts/dump_results.py` 绝对路径，
        `cwd=None`（继承当前目录），失败静默返回 None；
      * DUAL_RUN 形态：`dumper` = `"dump_results.py"` + `cwd=<tests>/scripts`，
        `verbose=True`（失败时往 stderr 打诊断）。
    """
    if verbose:
        cmd = [sys.executable, dumper, "--case", case, "--json"]
        try:
            proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                                  timeout=timeout)
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
    script = dumper
    try:
        proc = subprocess.run([sys.executable, str(script), "--case", case, "--json"],
                              capture_output=True, text=True, timeout=timeout)
    except (subprocess.TimeoutExpired, OSError):
        return None
    if proc.returncode != 0:
        return None
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError:
        return None


def dump_mem(case_name, dumper):
    """eth_test 独有的内存兜底形态：**只取 stdout 最后一行 JSON**，超时 90s。

    （与 `memory_backfill` 的差别是真实差异，故单列一个函数，不做归一。）
    """
    try:
        proc = subprocess.run([sys.executable, str(dumper), "--case", case_name, "--json"],
                              capture_output=True, text=True, timeout=90)
        payload = None
        for line in reversed((proc.stdout or "").strip().splitlines()):
            line = line.strip()
            if line.startswith("{"):
                payload = json.loads(line)
                break
        if payload is None:
            raise ValueError(f"no JSON on stdout (rc={proc.returncode})")
        return payload
    except Exception as exc:                      # noqa: BLE001
        log(f"[mem] unavailable: {exc}")
        return None


# =============================================================== L0 归档
def split_relay_lines(lines_by_core, prefixes, strip=False):
    """单出口（SPEC_REFERENCE §6.9.4）：把主核流里属于从核的行分拣回从核列表。

    从核不往自己的串口打 token ⇒ 它的行只出现在**主核**串口流里。主核转发时
    加 `<PREFIX>:<core>:` 核信封 ⇒ 按前缀把 `<PREFIX>:TGT:` 行搬回 TGT 列表、
    主核列表只留主核自己的行。于是归档与交给 L2 的 `{核名: [行]}` 与"两核各自
    本地输出"同形，§6.4.7 的按核重放输入契约逐字不变。

    硬约束：同一条从核行**只能出现在一个列表里**（搬走即从主核列表移除），
    否则重放会重复计项。

    `strip`：信封是**中继侧新加的壳**时置 True —— 搬运时把 `<PREFIX>:<core>:`
    剥掉，使 `serial_TGT.log` 与"从核本来自己打"的行**逐行等价**（ztest 型从核
    的 D1 行 ` PASS - <名> in X.XXX seconds` 本来没有前缀，信封纯属传输痕迹）。
    默认 False：`i3c_i2c_switch_test` 的从核 token 本身就以 `<PREFIX>:TGT:` 开头
    （它是 D2 文法的 token，不是壳）⇒ 剥掉会破坏 `_NATIVE_RESULT_RE` 认领。
    """
    if not prefixes:
        return lines_by_core
    pat = re.compile(r"(?:" + "|".join(re.escape(p) for p in prefixes)
                     + r"):(CTRL|TGT):")
    out = {}
    for core, lines in lines_by_core.items():
        if core != "CTRL":
            out[core] = list(lines or [])
    ctrl_out = []
    for ln in (lines_by_core.get("CTRL") or []):
        m = pat.search(str(ln))
        if m is not None and m.group(1) != "CTRL":
            if strip:
                ln = str(ln)[:m.start()] + str(ln)[m.end():]
            out.setdefault(m.group(1), []).append(ln)
        else:
            ctrl_out.append(ln)
    out["CTRL"] = ctrl_out
    return out


def archive(session, driver, case, lines_by_core, *, board, ports, timeout, script,
            split_from_primary=False, envelope_stripped=False):
    """把采集结果落成 L0 归档（唯一写入 chokepoint，便于静态审计）。"""
    from report_dir import case_dir_for
    from serial_archive import archive_serial
    cdir = case_dir_for(session, driver, case)
    archive_serial(cdir, case, lines_by_core, board=board, ports=ports,
                   timeout=timeout, script=script,
                   split_from_primary=split_from_primary,
                   envelope_stripped=envelope_stripped)
    return cdir


# =============================================================== 构建
def build_via_test_sh(case_dir, root, bash, *, at_root=False):
    """`test.sh`（无参）构建，返回 rc。

    `at_root=False`（Z/eth，默认）→ `cwd = <tests>/<case>`；
    `at_root=True`（mbox / app_iwdg）→ `cwd = <工程根>`。
    （两处既有的 cwd 取值不同，是真实差异，故用参数保留。）

    DUAL_RUN 的 `build_case`（`cwd=case_dir`、`[bash, "test.sh"]`、rc≠0 抛
    RuntimeError）另见 `build_via_test_sh_raise`。
    """
    test_sh = case_dir / "test.sh"
    env = dict(os.environ)
    env["I3C_TEST_ROOT"] = str(root).replace("\\", "/")
    cwd = str(root if at_root else case_dir).replace("\\", "/")
    return subprocess.call([bash, str(test_sh).replace("\\", "/")], cwd=cwd, env=env)


def build_via_test_sh_raise(case_dir, bash):
    """DUAL_RUN 的 `build_case`：rc≠0 抛 RuntimeError。"""
    print(f"\n[BUILD] {case_dir.name}")
    start = time.time()
    result = subprocess.run([bash, "test.sh"], cwd=str(case_dir),
                            stdout=None, stderr=None)
    elapsed = time.time() - start
    if result.returncode != 0:
        raise RuntimeError(f"build failed for {case_dir.name} (rc={result.returncode})")
    print(f"[BUILD] {case_dir.name} OK ({elapsed:.1f}s)")


def west_build(spec, case):
    """PORTS_DICT 10 份的 `west build`（逐字复刻 `test_common.sh build_single_test`）。

    返回 `(ok, err)`。工程差异全部走 spec 字段：
    `src_dir` / `build_subdir` / `build_progress` / `build_timeout` / `build_conf`。
    """
    build = spec.tests_dir / case / "build"
    if spec.build_subdir:
        build = build / spec.build_subdir
    src = spec.root / spec.src_dir
    overlay = src / "app.overlay"
    cmd = ["west", "build", "-p", spec.build_progress, "-b", spec.board,
           "-d", build.as_posix(), src.as_posix(),
           f"-DDTC_OVERLAY_FILE={overlay.as_posix()}"]
    if spec.build_conf:
        conf = spec.tests_dir / case / "case.conf"
        conf_file = f"{(src / 'prj.conf').as_posix()};{conf.as_posix()}"
        cmd.append(f"-DCONF_FILE={conf_file}")
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=spec.build_timeout)
    except subprocess.TimeoutExpired:
        return False, "build timeout"
    if proc.returncode != 0:
        return False, proc.stdout[-2000:] + proc.stderr[-2000:]
    return True, ""


# =============================================================== 环境自愈
def netstat_1025():
    """netstat 1025 端口状态：(状态, 本端PID, 对端PID)。"""
    rows = []
    try:
        out = subprocess.run(["netstat", "-ano"], capture_output=True,
                             text=True, timeout=15).stdout
        for line in out.splitlines():
            if ":1025" not in line:
                continue
            parts = line.split()
            if len(parts) >= 5 and parts[0] == "TCP":
                rows.append((parts[3], parts[4], parts[1]))   # (STATE, localPID, remote)
    except Exception:
        pass
    return rows


def kill_residual_gdb():
    """精准杀 1025 ESTABLISHED 连接对端（是 gdb 就 taskkill）。

    taskkill /T 树杀有时杀不干净（实测残留 gdb 仍持有连接），必须按连接杀。
    ★ Python subprocess 无 MSYS 路径转换层：tasklist/taskkill 参数必须用
    单斜杠（/FI /FO /F /PID），双斜杠 //F 会被 Windows 程序拒绝。
    """
    try:
        pids = set()
        for state, local_pid, _ in netstat_1025():
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


def restart_debugserver(style="d3", com99_probe=False):
    """重启 DebugServer 并等就绪。

    三种实现（逐字保留）：
      * `"z"`    —— Z 族/eth：taskkill /IM → sleep 2 → Popen(DEVNULL) → 30×(sleep 1
                    [+ COM99 探测] + netstat 见 1025 LISTENING) → log ready；
      * `"d3"`   —— D3 三份：taskkill /IM + netstat 精杀 → 删日志 → Popen(CREATE_NEW_CONSOLE,
                    stdout→临时日志) → 30s 轮询 "DebuggerServer$" → sleep 1；
      * `"otbn"` —— otbn/otbn_selftest/mbedtls：同 d3，诊断走 log() 而非 stderr。

    ★ 返回 `True` = 已确认就绪；`False` = 启动失败或等待超时（F4：`"z"` 分支原先
    `Popen` 无包裹，exe 不存在时异常穿透到调用方）。既有调用方一律忽略返回值
    ⇒ 返回值的引入不改变任何既有行为。
    """
    if style == "z":
        subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                       capture_output=True)
        time.sleep(2)
        ds = Path(DEBUGSERVER_EXE)
        try:
            subprocess.Popen([str(ds)], stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
        except Exception as exc:                  # exe 缺失/不可执行 ⇒ 明确失败，不穿透
            log(f"[ENV] ERROR: DebugServer 启动失败（{ds}）：{exc}")
            return False                          # 进程根本没起来，30s 轮询纯属空等
        for _ in range(30):
            time.sleep(1)
            if com99_probe:
                try:
                    s = _serial().Serial("COM99", 115200, timeout=0.1)
                    s.close()
                except Exception:
                    pass
            try:
                out = subprocess.run(["netstat", "-ano"], capture_output=True,
                                     text=True, timeout=10).stdout
                for line in out.splitlines():
                    if ":1025" in line and "LISTENING" in line:
                        log("[ENV] DebugServer ready")
                        return True
            except Exception:
                pass
        log("[ENV] WARNING: DebugServer may not be ready")
        return False

    # d3 / otbn
    def _say(msg):
        if style == "otbn":
            log(msg)
        else:
            print(msg, file=sys.stderr)

    _say("[ENV] restarting DebugServer (residue)")
    subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
    time.sleep(1)
    # taskkill /IM 可能杀不净（无效实例占 1025，新实例绑定失败卡在初始化）
    # → 用 netstat 精确杀 1025 的 LISTENING 进程兜底。
    for state, local_pid, _ in netstat_1025():
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
    except Exception as exc:                      # 同 z 分支：留痕，不静默
        _say(f"[ENV] ERROR: DebugServer 启动失败：{exc}")
    deadline = time.time() + 30
    ready = False
    while time.time() < deadline:
        try:
            with open(log_path, encoding="utf-8", errors="replace") as f:
                if "DebuggerServer$" in f.read() or "DebuggerServer>" in f.read():
                    ready = True
                    break
        except Exception:
            pass
        time.sleep(0.5)
    if not ready:
        _say("[ENV] WARNING: DebugServer 30s 内未打印就绪提示符")
    time.sleep(1)
    return ready


def clean_gdb_residue_plain():
    """Z 族 9 份的 `_clean_gdb_residue`：无脑杀 gdb 进程（按镜像名）。"""
    subprocess.run(["taskkill", "/F", "/IM", "riscv64-unknown-elf-gdb.exe"],
                   capture_output=True)
    time.sleep(1)


def clean_gdb_residue(force_restart=False, *, style="d3"):
    """按需清 DebugServer 连接残留。

    `style="z"`    —— Z 族 9 份：只杀 gdb 进程；
    `style="d3"`   —— D3 三份：先杀残留 gdb，复查 1025 仍残留 / server 已死才重启；
    `style="otbn"` —— otbn/otbn_selftest/mbedtls：同 d3。
    """
    if style == "z":
        clean_gdb_residue_plain()
        return
    kill_residual_gdb()
    if force_restart:
        restart_debugserver(style=style)
        return
    need = any(st == "ESTABLISHED" for st, _, _ in netstat_1025())
    if not need:
        need = not any(st == "LISTENING" for st, _, _ in netstat_1025())
    if need:
        restart_debugserver(style=style)


def restart_debugserver_stable(style="d3"):
    """兜底重启 + 5s 稳定期（新实例立即连跑会间歇性环境异常）。"""
    restart_debugserver(style=style)
    time.sleep(5)


# ---- case 前的 DebugServer 存活检查（E-2）----
# 为什么需要：一次 CKLink 探针瞬时故障会让 PC 侧 DebugServer **进程退出**
# （1025 不再 LISTENING）⇒ 之后每个 case 的 gdb 都只能报 `could not connect
# (error 138)`。此前这一检查只在**整批回归开始前**做一次，循环内不补刀 ⇒
# 一次掉线被放大成 N 个假 FAIL。本组函数把它做成"每个 case 都能调"的幂等动作。
DS_READY_WAIT_S = 15.0     # 刚 Popen 的新实例"bind+listen 就绪"的有限等待窗口
DS_ENSURE_ATTEMPTS = 2     # 轻自愈 + 重自愈各一轮仍不就绪 ⇒ 放弃（调用方 fail-closed）
# "健康"这一条返回串 = "无需自愈"的判据：调用方据此决定**是否留痕**（只有真发生了
# 自愈才打日志；健康路径保持安静，否则 30 个 case 会刷 30 行噪音）。
DS_HEALTHY_MSG = "1025 LISTENING 且无连接残留"


def _ds_healthy(wait_s=0.0):
    """`(healthy, reason)`：healthy = 1025 有 LISTENING **且**无 ESTABLISHED 残留。

    ★ 判据为什么稳（不误杀健康实例）：
      * TCP LISTENING 只在 `bind()+listen()` 完成后出现 —— 是"能连"的**必要**条件；
        刚拉起的实例在 listen 之前 netstat 看不到，所以**不能一次 netstat 就下结论**，
        用 `wait_s` 轮询把它等出来；
      * netstat 本身偶发失败时内层重试 3 次 —— 否则"查询失败"会被误读成"进程已死"
        而错杀一个活着的实例（重启一次 ~35s，还会打断正在跑的批次）；
      * ESTABLISHED 也算不健康：那是上一 case 的 gdb 连接残留，会让后续 load 无效
        （§10.1），必须清掉才算"可用"。
    """
    deadline = time.time() + max(0.0, wait_s)
    listening = residue = False
    while True:
        for _ in range(3):                     # netstat 瞬时失败重试，避免错杀
            rows = netstat_1025()
            listening = any(st == "LISTENING" for st, _, _ in rows)
            residue = any(st == "ESTABLISHED" for st, _, _ in rows)
            if listening or residue:
                break
            time.sleep(0.3)
        if listening and not residue:
            return True, DS_HEALTHY_MSG
        if time.time() >= deadline:
            return False, ("1025 有 ESTABLISHED 连接残留" if residue
                           else "1025 无 LISTENING（DebugServer 进程未运行）")
        time.sleep(0.5)


def ensure_debugserver_alive(style="d3", *, wait_s=None, attempts=None):
    """**每个 case 前置**的幂等动作：确保 DebugServer 在 1025 上听、且连接干净。

    返回 `(ok, msg)`。`ok=False` ⇒ 调用方必须 **fail-closed**（明确报错并中止该
    case），不得放它去烧录 —— 否则只会退化成 gdb `error 138` 的假 FAIL。

    阶梯（**复用既有原语，不另写一套 restart**）：
      ① 健康 ⇒ 立即返回，**零副作用**（不起探测 gdb、不重启）；
      ② 轻自愈 `kill_residual_gdb()` —— 只清连接残留，**不重启**（不误杀活实例）；
      ③ 重自愈 `restart_debugserver_stable()`（既有四步 + 5s 稳定期）；
      ④ ①-③ 走 `attempts` 轮仍不就绪 ⇒ `(False, reason)`。

    ★ 为什么健康路径**不做** `probe_gdb_alive()`：任何"连接→detach→退出"的 gdb
    都会给 DebugServer 留连接残留（§10.17），探测本身会把"健康"变成"需要重启"
    ⇒ 每个 case 平白多 ~35s。健康路径只用**无副作用**的 netstat 判活。

    `style` 只影响重启用的实现族（`"d3"` = 日志轮询判就绪，最严；29 份脚本同一份）。
    """
    wait_s = DS_READY_WAIT_S if wait_s is None else wait_s
    attempts = DS_ENSURE_ATTEMPTS if attempts is None else attempts
    cause = ""
    for attempt in range(1, max(1, attempts) + 1):
        # 首轮不等（健康路径要零延迟）；重启后的轮次给 wait_s 等 listen 就绪
        ok, reason = _ds_healthy(wait_s if attempt > 1 else 0.0)
        if ok:
            if attempt == 1:
                return True, reason
            return True, f"自愈第 {attempt - 1} 轮后恢复（前因：{cause}）"
        cause = cause or reason
        kill_residual_gdb()                            # ② 轻自愈（既有原语）
        ok, reason = _ds_healthy(0.0)
        if ok:
            return True, f"清 gdb 连接残留后恢复（前因：{cause}）"
        restart_debugserver_stable(style=style)        # ③ 重自愈（既有原语）
        ok, reason = _ds_healthy(wait_s)
        if ok:
            return True, f"重启 DebugServer 后恢复（前因：{cause}）"
    return False, f"{attempts} 轮自愈后 1025 仍不就绪：{cause}"


def dbg_run_preflight(style="d3"):
    """`dbg_run.py`（单 case 调试入口）的 DebugServer 前置检查（F1）。

    **薄适配**，不是第二套自愈：判活/轻自愈/重自愈全部在 `ensure_debugserver_alive`
    一处实现，本函数只负责把它的结果翻译成调试入口该有的对外表现 ——

      * 健康 ⇒ 静默返回（零副作用，不刷日志）；
      * 发生过自愈 ⇒ 打一行说明（让人知道结果是在自愈之后拿到的）；
      * **自愈失败 ⇒ 明确报错 + `sys.exit(3)`（fail-closed）**：不放它去烧录，
        否则只会退化成 gdb `could not connect (error 138)` 的假失败。

    为什么用 `sys.exit` 而不是抛异常：`dbg_run.py` 是**直接执行的独立脚本**
    （没有共享 main 可供提前拦截），出错时唯一正确的对外表现就是非零退出 +
    一行原因，而不是把异常抛成一页 traceback。

    `style="d3"`（日志轮询判就绪，最严）—— Z 族那条分支有 F4 的启动隐患，
    调试入口一律不走。
    """
    ok, msg = ensure_debugserver_alive(style=style)
    if not ok:
        print(f"[dbg] ERROR: DebugServer 不可用（{msg}）；"
              "请检查 CKLink 连接 / DebugServer 后重试", file=sys.stderr, flush=True)
        sys.exit(3)
    if msg != DS_HEALTHY_MSG:
        print(f"[dbg] [ENV] DebugServer 前置检查: {msg}", flush=True)


def probe_gdb_alive():
    """探测 DebugServer 连接是否健康：能连上且读到目标寄存器状态。"""
    try:
        result = subprocess.run(
            [GDB, "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
             "-ex", "info registers pc", "-ex", "detach", "-ex", "quit"],
            capture_output=True, text=True, timeout=15,
        )
        out = (result.stdout or "") + (result.stderr or "")
        return "Remote debugging" in out or "0x" in out
    except Exception:
        return False


def preflight_gdb(style="d3"):
    """烧录链路预检：不健康先 detach 清一次，仍不健康重启 DebugServer 再验。"""
    if probe_gdb_alive():
        return
    clean_gdb_residue(style=style)
    if probe_gdb_alive():
        return
    restart_debugserver_stable(style=style)
    if not probe_gdb_alive():
        raise RuntimeError(
            "DebugServer 预检失败（重启后仍连不上）：请检查 CKLink 连接/"
            "是否被 CSKYFlashProgrammer 占用（§10.2）")
    print("[PREFLIGHT] DebugServer restarted OK")


def kill_proc_group(proc, *, style="z"):
    """杀掉整个进程树（bash → gdb 子进程一并带走）。

    Windows 下 CTRL_BREAK_EVENT 只发给 bash，MSYS bash 不转发给子进程 gdb
    → gdb 存活并持有 DebugServer 连接 → 后续所有 load 挂死（批量回归
    连环卡死的根因）。taskkill /T /F 才能保证树全灭。
    """
    if style == "z":
        try:
            import signal  # noqa: F401  （原实现里的死代码，逐字保留）
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                           capture_output=True)
        except Exception:
            proc.kill()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        return
    # d3
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


def spawn_test_run(case_dir, root, bash, *, style="z"):
    """启动 `test.sh -r`，gdb 输出经 PIPE 线程采集。

    `style="z"`  —— Z/eth：stdout 逐行回显，返回 `(proc, lines, thread)`；
    `style="d3"` —— D3：`bufsize=1` + `CREATE_NEW_PROCESS_GROUP` + `RUN_TIMEOUT=120`，
                    返回 `(proc, proc_lines)`（不回显）。
    """
    if style == "z":
        test_sh = case_dir / "test.sh"
        env = dict(os.environ)
        env["SKIP_LOAD"] = "y"
        env["AGENT_RUN"] = "y"
        env["I3C_TEST_ROOT"] = str(root).replace("\\", "/")
        proc = subprocess.Popen(
            [bash, str(test_sh).replace("\\", "/"), "-r"],
            cwd=str(root).replace("\\", "/"),
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

    # d3
    kwargs = {"cwd": str(case_dir), "stdout": subprocess.PIPE,
              "stderr": subprocess.STDOUT}
    if sys.platform == "win32":
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
    env = dict(os.environ)
    env["RUN_TIMEOUT"] = "120"
    proc = subprocess.Popen([bash, "test.sh", "-r"], env=env, **kwargs)
    proc_lines = []

    def _reader():
        # 按 **\\r 与 \\n** 双向切分：gdb 的 `load` 进度是 `\\r` 原地刷新
        # （`section progress: …` 一行反复重写），**不产生换行**。若只按行读，
        # 慢烧录期间 `proc_lines` 长时间不增长 ⇒ `_LoadWatch` 的"连续
        # LOAD_WINDOW_S 无输出"把"正在烧、只是慢"误判成卡死（实测 i3c_dualcore
        # 烧录 ~33s 时必现；同一镜像快时 ~18s 侥幸通过）—— 这正是 §6.11.4 想
        # 修的误判，只是行缓冲把它的进度信号又漏掉了。
        # 只影响"进度判活"与 load_fail 诊断的尾部打印，不参与判定。
        buf = b""
        fd = proc.stdout.fileno()      # 裸 fd：`read1`/`read(n)` 会攒够 n 字节才返回，
        try:                           # 慢烧录时那等于"又不产出进度"（同一坑的另一面）
            while True:
                chunk = os.read(fd, 256)
                if not chunk:
                    break
                buf += chunk
                while True:
                    i_n = buf.find(b"\n")
                    i_r = buf.find(b"\r")
                    cand = [i for i in (i_n, i_r) if i >= 0]
                    if not cand:
                        break
                    i = min(cand)
                    proc_lines.append(buf[:i].decode("utf-8", "replace"))
                    buf = buf[i + 1:]
        except Exception:
            pass
    threading.Thread(target=_reader, daemon=True).start()
    return proc, proc_lines


# =============================================================== gdb 驱动器
def gdb_flash_once(elf, run_secs, *, rom_start="0x10000000", hold_cpu2=True,
                   pre_erase=False):
    """一次性 `-ex` 链烧录并运行 run_secs 秒，干净退出。

    `hold_cpu2`：continue 前按住 cpu2 复位（APP_CPU_SRST=0x4002200c，
    W1S/W1C：写 2=保持复位、写 1=释放、写 0 无效果），防他方 RAM 残留
    镜像串扰采集；跑完不释放（双核流程自会写 1 释放）。
    `pre_erase`：load 前 `monitor flash erase_address 0x10000000 0x10000`。

    ★ 不能用 communicate()：它会关闭 proc.stdin，之后再写 "\\x03" 必抛 ValueError
    → 优雅中断 (interrupt+detach+quit) 直接退化成 proc.kill() → DebugServer 连接残留
    （qsh-debug §2.3）。改 drain 线程持续排空 stdout，stdin 全程保持可写，\\x03 才真发得出去。
    """
    cmd = [GDB, "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
           "-ex", "set confirm off", "-ex", "set pagination off",
           "-ex", f"file {elf}", "-ex", "reset"]
    if pre_erase:
        cmd += ["-ex", "monitor flash erase_address 0x10000000 0x10000"]
    cmd += ["-ex", "set memory-write-packet-size 16384",
            "-ex", "set remotewritesize 16384",
            "-ex", "load",
            "-ex", f"set $pc={rom_start}"]
    if hold_cpu2:
        cmd += ["-ex", "set {int}0x4002200c = 2"]
    cmd += ["-ex", "continue"]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    _gdb_lines = []

    def _drain_stdout():
        try:
            for _ln in proc.stdout:
                _gdb_lines.append(_ln)
        except Exception:
            pass
    _drain_t = threading.Thread(target=_drain_stdout, daemon=True)
    _drain_t.start()
    try:
        proc.wait(timeout=run_secs)
    except subprocess.TimeoutExpired:
        try:
            proc.stdin.write("\x03")   # interrupt: 暂停目标
            proc.stdin.flush()
            time.sleep(1.0)
            proc.stdin.write("detach\nquit\n")
            proc.stdin.flush()
            proc.wait(timeout=10)
        except Exception:
            proc.kill()                # 兜底（正常路径不走到）
            proc.wait()
    _drain_t.join(timeout=2)
    out = "".join(_gdb_lines)
    return out


def gdb_watch_session(elf, run_secs, *, watch, rom_start="0x10000000",
                      stall_s=60, tgt_banners=()):
    """交互式 gdb 会话（otbn/otbn_selftest/mbedtls）。

    reset+load 后必须等 gdb 打出 "Transfer rate"（load 完成）再 set $pc+continue。
    一次性 -ex 链在 load 刚结束的瞬间就发 continue，DebugServer 状态不佳时 continue
    会被静默丢弃——CPU 停在 halt 无 BOOT，detach 时才跑起来。

    `watch=ports` 时启用完成标记提前收尾：PROJECT EXECUTION 出现后再静默 5s 即提前杀
    gdb；无完成标记且连续 `stall_s` 无 ztest 逐项输出判定套件卡死提前结束；90s 无
    ZT_BOOT 判环境问题（返回 no_boot=True，交给主流程 [RETRY] 自愈）。

    超时后必须干净退出（interrupt + detach + quit），不能 proc.kill()：
    kill 会让 DebugServer 认为连接仍存在（连接残留），下次 target remote 能连上
    但 reset/load 实际无效（§10.1）。
    """
    cmd = [GDB, "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
           "-ex", "set confirm off", "-ex", "set pagination off",
           # CKLink load 实测 1KB/s → 41KB/s（41x），写包参数默认开启
           "-ex", "set memory-write-packet-size 16384",
           "-ex", "set remotewritesize 16384",
           "-ex", f"file {elf}"]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    gdb_out = []
    gdb_out_stop = threading.Event()

    def gdb_reader():
        while not gdb_out_stop.is_set():
            line = proc.stdout.readline()
            if not line:
                break
            gdb_out.append(line.rstrip("\n"))

    gt = threading.Thread(target=gdb_reader, daemon=True)
    gt.start()

    def gdb_send(*cmds):
        for c in cmds:
            proc.stdin.write(c + "\n")
        proc.stdin.flush()

    def wait_load_done(timeout_s=420):
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if any("Transfer rate" in ln for ln in gdb_out):
                return True
            if any(("Remote failure" in ln) or ("E22" in ln) for ln in gdb_out):
                return False   # 协议错误（如写包过大被 stub 拒绝），立即失败
            time.sleep(0.5)
        return False

    def quit_clean():
        try:
            gdb_send("\x03")
            time.sleep(1.0)
            gdb_send("detach", "quit")
            proc.wait(timeout=10)
        except Exception:
            proc.kill()
            proc.wait()
        gdb_out_stop.set()
        gt.join(timeout=2)

    def _own_port(p):
        """§10.30 串扰免疫：已见本固件 ZT_BOOT 且带本固件 banner 的端口才算我方。"""
        pl = p["lines"]
        return (any(ZT_BOOT_RE.search(ln) for ln in pl)
                and any(b in ln for b in tgt_banners for ln in pl))

    no_boot = False

    gdb_send("reset", "load")
    if not wait_load_done():
        log("[run] !!! load timeout/error (Remote failure/E22/420s) -> abort")
        quit_clean()
        return "\n".join(gdb_out), False

    if watch is None:
        # 无采集轮：continue 后干等 run_secs 收尾
        gdb_send("set {int}0x4002200c = 2",
                 f"set $pc={rom_start}", "continue")
        try:
            proc.wait(timeout=run_secs)
        except subprocess.TimeoutExpired:
            pass
        quit_clean()
        return "\n".join(gdb_out), False

    # capture 轮：continue 后按提前收尾判据采集
    gdb_send("set {int}0x4002200c = 2",
             f"set $pc={rom_start}", "continue")
    deadline = time.time() + run_secs
    start_at = time.time()
    done_at = None
    last_byte_at = time.time()
    seen = 0
    seen_all = 0
    while time.time() < deadline and proc.poll() is None:
        # 新 ztest 逐项输出行实时回显（采集不再盲等；外来端口也回显备查）
        all_tokens = [ln for p in watch.values() for ln in p["lines"]
                      if ZTEST_ACTIVITY_RE.search(ln)]
        n_all = len(all_tokens)
        if n_all != seen_all:
            for ln in all_tokens[seen_all:]:
                log(f"  [{int(time.time() - start_at):4d}s] {ln}")
            seen_all = n_all
        # §10.30 防线二：判定只认我方端口（已见本固件 ZT_BOOT+banner）
        own_lines = [ln for p in watch.values() if _own_port(p) for ln in p["lines"]]
        own_tokens = [ln for ln in own_lines if ZTEST_ACTIVITY_RE.search(ln)]
        n = len(own_tokens)
        if n != seen:
            seen = n
            last_byte_at = time.time()
        txt = "\n".join(own_lines)
        if EXEC_RE.search(txt):
            if done_at is None:
                done_at = time.time()
            elif time.time() - done_at >= 5:
                log(f"[run] PROJECT EXECUTION + 5s idle -> early stop "
                    f"(elapsed {int(time.time() - start_at)}s)")
                break
        elif done_at is None and seen > 0 and time.time() - last_byte_at >= stall_s:
            log(f"[run] WARNING: {stall_s}s no token (t={int(time.time() - start_at)}s), "
                "suite stalled? -> early stop")
            break
        elif seen == 0 and time.time() - start_at >= 90:
            # §6.2-2：load 后 90s 仍无 ZT_BOOT = 环境问题（烧录链路/
            # 连接残留/不稳定期 §10.20）。no_boot=True 强制主流程 [RETRY]
            # 自愈（重启 DebugServer 重试），防止 teardown 窗口内才跑起来
            # 的零散 token 骗过 BOOT 检查、产出半截报告。
            log(f"[run] ERROR: 90s no ZT_BOOT (t={int(time.time() - start_at)}s), "
                "环境问题 -> early stop for auto-retry")
            no_boot = True
            break
        time.sleep(0.5)
    quit_clean()
    return "\n".join(gdb_out), no_boot


# =============================================================== run_case
def run_case_shell(spec, case_name, tgt_uart, timeout, bash):
    """SINGLE_CAP 9 份逐字同构：单路采集 + `test.sh -r` 干等 timeout。"""
    build_dir = spec.tests_dir / case_name / "build" / "target"
    elf = build_dir / "zephyr" / "zephyr.elf"
    if not elf.exists():
        log(f"[ERROR] ELF not found: {elf}")
        return None

    cap = SerialCapture(tgt_uart, mode="line")
    cap.start()
    if cap.error:
        log(f"[ERROR] {cap.error}")
        return None

    proc, proc_lines, thread = spawn_test_run(spec.tests_dir / case_name,
                                              spec.root, bash, style="z")

    start = time.time()
    last_activity = start
    while time.time() - start < timeout:
        if proc.poll() is not None:
            break
        time.sleep(0.3)

    kill_proc_group(proc, style="z")
    thread.join(timeout=3.0)
    cap.stop()

    return {
        "case": case_name,
        "ctrl_lines": [],
        "tgt_lines": cap.lines,
        "proc_lines": proc_lines,
        "timed_out": (time.time() - start >= timeout),
    }


class _LoadWatch:
    """烧录进度看门狗：区分"没有在烧录"和"烧录正在慢慢进行"。

    ★ 为什么需要（2026-09-20，B2-fix §1）：`LOAD_WINDOW_S` 原本是**绝对**窗口 ——
    spawn 后 20s 内没见到 BOOT 就判 `load_fail`。但烧录是 CKLink 驱动限速的
    （实测 `Transfer rate: 41 KB/sec`，见 §3），最大的一份镜像（i3c_dualcore_test
    target ELF 114 KB + controller bin restore）实测 **17～50s** 才落地 —— 于是
    "烧录正常、只是慢"被误判成"环境失败"，`abort_on_load_fail` 直接把整批回归
    中止。B2 报告的"harness prime 20s 窗口"根因即此（当时被归因成管道问题）。

    判据改成**基于进度**：只要 test.sh/gdb 还在往 stdout 产出（`proc_lines` 在长），
    它就在干活 ⇒ 窗口放宽到 `case_timeout` 硬上限；只有**连续 `LOAD_WINDOW_S`
    一点输出都没有**（spawn 起来就没动静 / 烧录中途卡死）才判死 —— 这才是原来
    那条判据想表达的语义。

    ★ 这个"进度"信号之所以拿得到，正是因为 `spawn_test_run` 用 drain 线程**持续**
      读管道（同族缺陷的标准修法）。不排空的实现连"它到底在不在动"都看不见。
    """

    def __init__(self, proc_lines, start):
        self._lines = proc_lines
        self._n = len(proc_lines)
        self._start = start
        self._last = start

    def dead(self, idle_s, hard_cap_s, now=None):
        now = time.time() if now is None else now
        n = len(self._lines)
        if n != self._n:
            self._n = n
            self._last = now
        if now - self._last > idle_s:
            return True                      # 连续 idle_s 无任何输出 ⇒ 环境问题
        return now - self._start > hard_cap_s   # 硬上限兜底（不无限等）


def run_case_dual(spec, case_dir, bash):
    """DUAL_RUN 3 份：两遍烧录 + 双核采集 + 静默判完成。

    返回 raw dict；环境失败（pass-1 无任何输出）抛 RuntimeError，由 `dual_run`
    的重试循环接住。
    """
    from ztest_report import parse_ztest
    LOAD_WINDOW_S = 20
    IDLE_S = 10
    P1_HOLD_S = 5

    if spec.announce:
        print(f"[RUN] {case_dir.name}")

    ctrl_cap = SerialCapture(spec.ctrl_uart, mode="line_err")
    tgt_cap = SerialCapture(spec.tgt_uart, mode="line_err")
    ctrl_cap.start()
    tgt_cap.start()
    if ctrl_cap.error:
        tgt_cap.stop()   # 异常路径必须释放串口，否则重试/下一 case 自占 COM 口
        raise RuntimeError(ctrl_cap.error)
    if tgt_cap.error:
        ctrl_cap.stop()
        raise RuntimeError(tgt_cap.error)

    def _ztest_view(cap):
        return parse_ztest(cap.plain_lines(),
                           ctrl_banners=spec.ctrl_banners,
                           tgt_banners=spec.tgt_banners)

    def _has_boot():
        return _ztest_view(ctrl_cap)["boot_seen"] or _ztest_view(tgt_cap)["boot_seen"]

    # 套件完成标记：仅认"本固件 ZT_BOOT 已见（boot_seen）"的端口上的
    # PROJECT EXECUTION（§10.30 门控）。静默判据以此为准
    # ——10s 静默但无标记 = 测试中途长间隔，不能判完成。
    def _has_completion_marker():
        c = _ztest_view(ctrl_cap)
        t = _ztest_view(tgt_cap)
        return ((c["boot_seen"] and c["execution"] is not None)
                or (t["boot_seen"] and t["execution"] is not None))

    # prime 轮固件跑的是 I-cache 残留的旧镜像（迁移前固件无 ZT_BOOT）——
    # 判据用"任何固件输出"（旧 token/ztest 行/ZT_BOOT 任一即算）。
    def _has_any_output():
        txt = ctrl_cap.text + "\n" + tgt_cap.text
        for k in ("ZT_BOOT", spec.banner_key, "PROJECT EXECUTION",
                  "START - ", " PASS - ", " FAIL - "):
            if k in txt:
                return True
        return False

    # ---- pass 1 (prime)：冲刷 I-cache 旧镜像，输出丢弃 ----
    print(f"[RUN] {case_dir.name} pass-1 (prime, I-cache flush)")
    proc, proc_lines = spawn_test_run(case_dir, spec.root, bash, style="d3")
    p1_start = time.time()
    p1_boot = None
    p1_watch = _LoadWatch(proc_lines, p1_start)
    while True:
        if _has_any_output():
            p1_boot = time.time()
            break
        if proc.poll() is not None:
            break
        if p1_watch.dead(LOAD_WINDOW_S, spec.case_timeout):
            break
        time.sleep(0.3)
    kill_proc_group(proc, style="d3")
    clean_gdb_residue(style="d3")
    if p1_boot is None:
        ctrl_cap.stop()
        tgt_cap.stop()
        raise RuntimeError(
            f"{case_dir.name}: pass-1 烧录后连续 {LOAD_WINDOW_S}s 无任何固件输出"
            f"（ZT_BOOT/ztest 行/{spec.banner_key} token；test.sh/gdb 也没产出任何"
            "行）——测试环境有问题"
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
    proc, proc_lines = spawn_test_run(case_dir, spec.root, bash, style="d3")
    watch = _LoadWatch(proc_lines, start)
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
        if boot_time is None and watch.dead(LOAD_WINDOW_S, spec.case_timeout, now):
            # 连续 LOAD_WINDOW_S 内 test.sh/gdb **一点输出都没有** ⇒ 环境问题。
            # ★ 不再用"spawn 后满 LOAD_WINDOW_S"这个绝对窗口：烧录本身可慢到
            #   17～50s（CKLink 限速 41 KB/s），绝对值会把"正常但慢"误判成失败
            #   （见 _LoadWatch 的说明）。
            outcome = "load_fail"
            break
        # 静默判据：ZT_BOOT 后连续 IDLE_S 无新字节。★ 慢速 case 相邻 token
        # 间隔偶发 >10s，静默在测试中途误触发会截断后半套件。修正：静默超时
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
        if now - start > spec.case_timeout:
            outcome = "timeout"
            break
        time.sleep(0.3)

    if proc.poll() is None:
        kill_proc_group(proc, style="d3")
    run_rc = proc.returncode if proc.returncode is not None else 0

    if outcome == "load_fail":
        print(f"[RUN] {case_dir.name} 连续 {LOAD_WINDOW_S}s 无任何 gdb/固件输出"
              f"——环境问题（DebugServer 残留/烧录链路/镜像陈旧），非测试失败",
              file=sys.stderr)
        if proc_lines:
            print("[RUN] gdb 输出尾部（诊断）：", file=sys.stderr)
            for ln in proc_lines[-6:]:
                print(f"    {ln.rstrip()}", file=sys.stderr)
    elif outcome == "timeout":
        print(f"[RUN] {case_dir.name} 超过 {spec.case_timeout}s 未完成，强杀", file=sys.stderr)

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


def run_case_mbox(spec, case_name, ctrl_uart, tgt_uart, timeout, bash,
                  core_view, combined_lines):
    """MBOX：单次 `test.sh -r`（stdout 丢弃）+ 双核早收敛 + wmic 兜底杀 gdb。"""
    build_dir = spec.tests_dir / case_name / "build"
    target_elf = build_dir / "target" / "zephyr" / "zephyr.elf"
    controller_bin = build_dir / "controller" / "zephyr" / "zephyr.bin"

    if not target_elf.exists():
        log(f"[ERROR] Target ELF not found: {target_elf}")
        return None
    if not controller_bin.exists():
        log(f"[ERROR] Controller BIN not found: {controller_bin}")
        return None

    ctrl_cap = SerialCapture(ctrl_uart, mode="chunk")
    tgt_cap = SerialCapture(tgt_uart, mode="chunk")
    ctrl_cap.start()
    tgt_cap.start()

    if ctrl_cap.error:
        log(f"[ERROR] {ctrl_cap.error}")
        tgt_cap.stop()
        return None
    if tgt_cap.error:
        log(f"[ERROR] {tgt_cap.error}")
        ctrl_cap.stop()
        return None

    ctrl_cap.flush_stale()
    tgt_cap.flush_stale()

    # Run test.sh -r (stdout=DEVNULL, serial capture is the real output)
    test_sh = spec.tests_dir / case_name / "test.sh"
    env = dict(os.environ)
    env["SKIP_LOAD"] = "y"
    env["AGENT_RUN"] = "y"
    env["I3C_TEST_ROOT"] = str(spec.root).replace("\\", "/")
    proc = subprocess.Popen(
        [bash, str(test_sh).replace("\\", "/"), "-r"],
        cwd=str(spec.root).replace("\\", "/"),
        env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )

    # Poll until test.sh exits (load_and_run stays alive via gdb continue)
    start = time.time()
    timed_out = False
    last_tick = -1
    while True:
        elapsed = time.time() - start
        if elapsed >= timeout:
            timed_out = True
            break
        if proc.poll() is not None:
            break
        # 提前收敛：两核都已见 BOOT + verdict 标记即可收工（RUN_TIMEOUT=0
        # 下 gdb 永不退出，否则每个 case 都要空等满 timeout）。只影响等待
        # 时长，不参与任何 PASS/FAIL 判定。
        c = core_view(ctrl_cap.lines, "CTRL")
        t = core_view(tgt_cap.lines, "TGT")
        if (c["boot"] and c["verdict"] is not None and
                t["boot"] and t["verdict"] is not None):
            break
        tick = int(elapsed)
        if tick != last_tick and tick % 5 == 0:
            last_tick = tick
            log(f"  [t+{tick}s] ctrl_boot={c['boot']} tgt_boot={t['boot']} "
                f"ctrl={len(combined_lines(ctrl_cap.lines))}B "
                f"tgt={len(combined_lines(tgt_cap.lines))}B")
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
        subprocess.run(["wmic", "process", "where",
                        "name='riscv64-unknown-elf-gdb.exe'", "delete"],
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
        "timed_out": timed_out,
        "elapsed_s": time.time() - start,
    }


def run_pass_iwdg(spec, case_name, ctrl_cap, tgt_cap, timeout, bash):
    """IWDG 的一轮（test.sh -r）：加载两核 + 采集。返回 `(proc_lines, timed_out, rc)`。"""
    test_sh = spec.tests_dir / case_name / "test.sh"
    env = dict(os.environ)
    env["I3C_TEST_ROOT"] = str(spec.root).replace("\\", "/")
    env["SKIP_LOAD"] = "y"
    env["AGENT_RUN"] = "y"
    env["RUN_TIMEOUT"] = "0"

    proc = subprocess.Popen(
        [bash, str(test_sh).replace("\\", "/"), "-r"],
        cwd=str(spec.root).replace("\\", "/"), env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        encoding="utf-8", errors="replace",
    )
    lines = []

    def _reader():
        for line in proc.stdout:
            lines.append(line.rstrip("\r\n"))

    t = threading.Thread(target=_reader, daemon=True)
    t.start()

    start = time.time()
    while time.time() - start < timeout and proc.poll() is None:
        time.sleep(0.3)

    timed_out = (time.time() - start >= timeout)
    try:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                       capture_output=True, timeout=10)
    except Exception:
        proc.kill()
    time.sleep(0.5)
    rc = proc.poll()
    t.join(timeout=2.0)
    return lines, timed_out, rc


def capture_case(spec, case, tgt_uart, timeout):
    """PORTS_DICT 的烧录+采集：`oneshot` 两遍法 / `watch` 单遍。"""
    build = spec.tests_dir / case / "build"
    if spec.build_subdir:
        build = build / spec.build_subdir
    elf = (build / "zephyr" / "zephyr.elf").as_posix()
    if not Path(elf).exists():
        return None, False, f"not built: {elf}"

    names = discover_ports([p for p in (tgt_uart,) if p])
    ports = {n: {"lines": [], "stop": threading.Event()} for n in names}

    def start_capture():
        sers = {}
        for n in names:
            try:
                sers[n] = _serial().Serial(n, 115200, timeout=0.1)
            # 原实现是 `except serial.SerialException`（窄于此处的 Exception），逐字保留
            except _serial().SerialException as e:
                log(f"[run] WARNING: cannot open {n}: {e}")
        threads = []
        for n, ser in sers.items():
            t = threading.Thread(target=reader, args=(ser, ports[n]["lines"], ports[n]["stop"]),
                                 daemon=True)
            t.start()
            threads.append(t)
        return sers, threads

    if spec.driver_kind == "watch":
        # 单遍烧录 + 首轮采集（单核工程）。两遍烧录法的 capture 轮 = 会话内
        # interrupt 后二次 load，实测静默挂死（§10.24）；且 reset 已冲刷
        # I-cache（§10.4 慢一拍场景被覆盖），load 后状态天然干净，直接采首轮。
        sers, threads = start_capture()
        out, no_boot = gdb_watch_session(elf, timeout, watch=ports,
                                         rom_start=spec.target_rom_start,
                                         stall_s=spec.stall_s,
                                         tgt_banners=spec.tgt_banners)
        time.sleep(0.3)
        for n in ports:
            ports[n]["stop"].set()
        for t in threads:
            t.join(timeout=1)
        for ser in sers.values():
            ser.close()

        if no_boot:
            # 90s 无 ZT_BOOT = 环境问题（烧录链路/连接残留/不稳定期 §10.20），
            # 强制主流程 [RETRY]（重启 DebugServer 后重试），不放行部分采集。
            return ports, False, "no ZT_BOOT within 90s (env issue, auto-retry)"
        ok = "Loading section" in out
        if not ok:
            return ports, False, "capture flash failed"
        return ports, True, ""

    # oneshot：两遍烧录法（I-cache 规避）；prime 轮不开串口
    out = gdb_flash_once(elf, 20, rom_start=spec.target_rom_start,
                         hold_cpu2=spec.hold_cpu2, pre_erase=spec.pre_erase)
    if "Loading section" not in out:
        return None, False, "prime flash failed"
    time.sleep(1)

    sers, threads = start_capture()
    out = gdb_flash_once(elf, timeout, rom_start=spec.target_rom_start,
                         hold_cpu2=spec.hold_cpu2, pre_erase=spec.pre_erase)
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


# =============================================================== 5 个策略
def single_cap(spec):
    """SINGLE_CAP（9 份）：发现串口 → 构建 → 采集 → 落 L0 归档。"""
    bash = find_git_bash(spec.bash_style)
    tgt_uart = spec.tgt_uart or find_tgt_uart()
    if not tgt_uart:
        log("ERROR: no target UART found")
        return Acquired(ok=False, err="no target UART found")
    log(f"Target UART: {tgt_uart}")

    session = _resolve_session(spec.session)

    # E-2（每 case 前置）：DebugServer 不活 ⇒ fail-closed，**在构建之前**中止 ——
    # 既省掉一次白构建，也不让它退化成 gdb `error 138` 的假 FAIL。
    ds_ok, ds_msg = ensure_debugserver_alive(style="d3")
    if not ds_ok:
        log(f"[ERROR] DebugServer 前置检查失败：{ds_msg}")
        return Acquired(ok=False, err=f"DebugServer unavailable: {ds_msg}",
                        session=session)
    if ds_msg != DS_HEALTHY_MSG:      # 只有真发生过自愈才留痕（健康路径保持安静）
        log(f"[ENV] DebugServer 前置检查: {ds_msg}")

    if not spec.skip_build:
        log(f"[BUILD] {spec.case}")
        rc = build_via_test_sh(spec.tests_dir / spec.case, spec.root, bash)
        if rc != 0:
            log(f"[ERROR] build failed rc={rc}")
            return Acquired(ok=False, err=f"build failed rc={rc}", session=session)

    raw = run_case_shell(spec, spec.case, tgt_uart, spec.case_timeout, bash)
    if raw is None:
        return Acquired(ok=False, err="capture failed", session=session)

    mem = spec.mem_hook(spec.case) if spec.mem_hook else None

    # L1→L2 边界（L0 归档形状，common/serial_archive.py）：先把采集结果归一成
    # `{核名: [原始行]}`，落档，再把**同一个对象**交给 L2（common/process.py）。
    # 离线重放读的就是这份归档 ⇒ 在线判定与离线重放的输入逐字相同。
    lines_by_core = {"TGT": [ln for _, ln in raw["tgt_lines"]]}
    cdir = None
    if spec.archive:
        cdir = archive(session, spec.driver, spec.case, lines_by_core,
                       board=spec.board, ports={"TGT": tgt_uart},
                       timeout=spec.case_timeout, script=spec.script)
    return Acquired(ok=True, lines=lines_by_core, mem=mem,
                    session=session, case_dir=cdir)


def _ports_dict_one(spec, case):
    # E-2（每 case 前置）：DebugServer 不活 ⇒ fail-closed，**在构建之前**中止。
    # ★ 本族原先连"回归前清一次"都没有 ⇒ 掉线后只能逐个产出 gdb `error 138` 的假 FAIL。
    #   （早先曾声明过一个 `PortsDictSpec.clean_before` 字段表达该意图，但从未接线；
    #   2026-09-21 评估后**删除** —— 该意图已被下面这道每 case 前置严格覆盖，
    #   而"入口无条件清残留"正是 E-2 刻意去掉的行为。）
    ds_ok, ds_msg = ensure_debugserver_alive(style="d3")
    if not ds_ok:
        log(f"[ERROR] DebugServer 前置检查失败：{ds_msg}")
        return Acquired(ok=False, err=f"DebugServer unavailable: {ds_msg}"), None
    if ds_msg != DS_HEALTHY_MSG:      # 只有真发生过自愈才留痕
        log(f"[ENV] DebugServer 前置检查: {ds_msg}")
    if not spec.skip_build:
        ok, err = west_build(spec, case)
        if not ok:
            return Acquired(ok=False, err=err, phase="build"), None
    ports, ok, err = capture_case(spec, case, spec.tgt_uart, spec.timeout)
    if spec.retry_on_no_boot:
        # §6.2-2 BOOT 环境判定 + §10.22 自愈重试：load 后无 ZT_BOOT =
        # 测试环境问题（烧录链路/连接残留/镜像陈旧）→ 重启 DebugServer 重试 1 次
        for attempt in range(2):
            if ok:
                # §10.30：BOOT 判定只认我方端口（已见本固件 ZT_BOOT+banner）；
                # 他方残留镜像的 ZT_BOOT 不算本固件已启动
                own_lines = [ln for p in ports.values()
                             for ln in p["lines"] if _is_own(p, spec.tgt_banners)]
                if any(ZT_BOOT_RE.search(ln) for ln in own_lines):
                    break
                ok, err = False, "no ZT_BOOT after flash"
            if attempt == 0:
                log(f"[RETRY] {case}: {err}（环境问题）→ 重启 DebugServer 后重试")
                restart_debugserver_stable(style="otbn")
                ports, ok, err = capture_case(spec, case, spec.tgt_uart, spec.timeout)
    if not ok:
        return Acquired(ok=False, err=err, phase="capture"), None
    if ports is None:
        return Acquired(ok=False, err="elf missing", phase="capture"), None

    lines = []
    for _n, p in ports.items():
        text = "\n".join(p["lines"])
        if any(b in text for b in spec.tgt_banners):
            lines = p["lines"]
    if not lines:
        log(f"[run] WARNING: no TGT banner lines captured ({case})")

    # L1→L2 边界（L0 归档形状）：采集行按归档形状归一；mem 兜底是 L1 侧
    # 产物（读目标内存，不进归档）⇒ 作为独立入参交给 L2（common/process.py）。
    lines_by_core = {"TGT": lines}
    session = _resolve_session(spec.session)
    cdir = None
    if spec.archive and session is not None:
        cdir = archive(session, spec.driver, case, lines_by_core, board=spec.board,
                       ports={"TGT": spec.tgt_uart}, timeout=spec.timeout,
                       script=spec.script)
    mem = None
    if spec.mem:
        mem = memory_backfill(case, dumper=spec.mem_dumper, cwd=spec.mem_cwd,
                              verbose=spec.mem_verbose)
    return Acquired(ok=True, lines=lines_by_core, mem=mem, session=session,
                    case_dir=cdir), session


def _is_own(p, tgt_banners):
    """§10.30 串扰免疫（PORTS_DICT 版，逐字 `_own_port`）。"""
    pl = p["lines"]
    return (any(ZT_BOOT_RE.search(ln) for ln in pl)
            and any(b in ln for b in tgt_banners for ln in pl))


def ports_dict_case(spec, case, session=None):
    """PORTS_DICT 的单 case 形态（多 case 工程在循环里逐个调本函数）。

    `session` 可传 Path / None / 工厂；传了就覆盖 `spec.session`（见 `_resolve_session`）。
    """
    if session is not None:
        spec.session = session
    spec.case = case
    acq, _ = _ports_dict_one(spec, case)
    return acq


def ports_dict(spec):
    """PORTS_DICT（10 份）：west 构建 → Python 直驱 gdb 烧录采集 → 落归档 → mem。

    `driver_kind` 二选一：
      * `"oneshot"` —— 一次性 `-ex` 链 + drain 线程（i2cf/io_probe/spi/spid/spif/trng/rtc_ls）；
      * `"watch"`   —— 交互式会话 + 完成标记提前收尾（otbn/otbn_selftest/mbedtls）。
    """
    return ports_dict_case(spec, spec.case)


def dual_run(spec, case_dir):
    """DUAL_RUN（3 份）：一个 case 的完整 L1（clean → 采集 → 自愈重试 → 归档 → mem）。"""
    bash = find_git_bash("filtered")
    # E-2（每 case 前置）：本族原有 `clean_gdb_residue` 在"进程已死"时确实会重启，
    # 但它**不判重启后是否真的就绪**、也不 fail-closed（起不来就继续烧录 → 假 FAIL）。
    # ensure 在它前面补上"就绪判定 + 明确中止"。
    ds_ok, ds_msg = ensure_debugserver_alive(style="d3")
    if not ds_ok:
        print(f"[ERROR] DebugServer 前置检查失败：{ds_msg}", file=sys.stderr)
        return Acquired(ok=False, err=f"DebugServer unavailable: {ds_msg}", rc=1)
    if ds_msg != DS_HEALTHY_MSG:      # 只有真发生过自愈才留痕
        print(f"[ENV] DebugServer 前置检查: {ds_msg}", file=sys.stderr)
    clean_gdb_residue(style="d3")  # 上一 case 杀 gdb 的残留不带到本 case
    raw = None
    for attempt in range(2):
        try:
            raw = run_case_dual(spec, case_dir, bash)
        except RuntimeError as exc:
            if attempt == 0:
                print(f"[RETRY] {case_dir.name}: 环境失败（{exc}）→ "
                      "重启 DebugServer 后重试", file=sys.stderr)
                restart_debugserver_stable(style="d3")
                clean_gdb_residue(style="d3")
                continue
            print(f"ERROR: {exc}", file=sys.stderr)
            return Acquired(ok=False, err=str(exc), rc=1)
        if raw.get("load_failed") and attempt == 0:
            print(f"[RETRY] {case_dir.name}: load_fail（环境问题）→ "
                  "重启 DebugServer 后重试", file=sys.stderr)
            restart_debugserver_stable(style="d3")
            clean_gdb_residue(style="d3")
            continue
        break
    if raw.get("load_failed") and spec.abort_on_load_fail:
        # 重试后仍无 ZT_BOOT（BOOT 未到）= 测试环境有问题（烧录链路/
        # 串口/镜像陈旧），继续跑只会批量产出假 FAIL——环境中止。
        print("ERROR: 测试环境异常（重试后仍未收到 ZT_BOOT），中止回归。"
              "请检查 DebugServer/串口/接线后重试", file=sys.stderr)
        return Acquired(ok=False, err="env abort", rc=2)
    clean_gdb_residue(style="d3")  # 本 case 杀 gdb 的残留不带给 backfill/下一 case

    # L1→L2 边界（L0 归档形状）：两核原始行**分核**归一；mem 兜底是 L1 侧产物
    # （读目标内存，不进归档）⇒ 作为独立入参交给 L2（common/process.py）。
    lines_by_core = {"CTRL": raw["ctrl_lines"], "TGT": raw["tgt_lines"]}
    # 单出口：从核行只在主核流里 ⇒ 先按前缀分拣回从核列表，再归档 / 交 L2
    lines_by_core = split_relay_lines(lines_by_core, spec.relay_prefixes,
                                      strip=spec.relay_strip)
    session = _resolve_session(spec.session)
    cdir = None
    if spec.archive and session is not None:
        cdir = archive(session, spec.driver, case_dir.name, lines_by_core,
                       board=spec.board,
                       ports={"CTRL": spec.ctrl_uart, "TGT": spec.tgt_uart},
                       timeout=spec.case_timeout, script=spec.script,
                       split_from_primary=bool(spec.relay_prefixes),
                       envelope_stripped=bool(spec.relay_strip))
    mem = memory_backfill(case_dir.name, dumper="dump_results.py",
                          cwd=str(spec.tests_dir / "scripts"), verbose=True)
    return Acquired(ok=True, lines=lines_by_core, mem=mem, session=session,
                    case_dir=cdir)


def mbox(spec):
    """MBOX（1 份）：构建 → 单次烧录采集 → 双核分核归一 → 落归档。"""
    if not spec.ctrl_uart or not spec.tgt_uart:
        log("ERROR: --ctrl-uart and --tgt-uart required for mbox dual-core test")
        return Acquired(ok=False, err="ports required")
    log(f"Controller UART: {spec.ctrl_uart}")
    log(f"Target UART: {spec.tgt_uart}")

    session = _resolve_session(spec.session)

    bash = find_git_bash("env_first")
    # E-2（每 case 前置）：DebugServer 不活 ⇒ fail-closed，构建前中止。
    ds_ok, ds_msg = ensure_debugserver_alive(style="d3")
    if not ds_ok:
        log(f"[ERROR] DebugServer 前置检查失败：{ds_msg}")
        return Acquired(ok=False, err=f"DebugServer unavailable: {ds_msg}",
                        session=session)
    if ds_msg != DS_HEALTHY_MSG:      # 只有真发生过自愈才留痕（健康路径保持安静）
        log(f"[ENV] DebugServer 前置检查: {ds_msg}")
    if not spec.skip_build:
        log(f"[BUILD] {spec.case} (both cores)")
        rc = build_via_test_sh(spec.tests_dir / spec.case, spec.root, bash, at_root=True)
        if rc != 0:
            log(f"[ERROR] build failed rc={rc}")
            return Acquired(ok=False, err=f"build failed rc={rc}", session=session)

    raw = run_case_mbox(spec, spec.case, spec.ctrl_uart, spec.tgt_uart,
                        spec.case_timeout, bash, spec.core_view, spec.combined_lines)
    if raw is None:
        return Acquired(ok=False, err="capture failed", session=session)

    # L1→L2 边界（L0 归档形状）：本工程判定入参是 (时刻, 字节块)，先用判定同款的
    # `_split_lines` 还原成逐行文本（= 归档内容），再把**同一个对象**交给 L2。
    lines_by_core = {"CTRL": spec.split_lines(raw["ctrl_lines"]),
                     "TGT": spec.split_lines(raw["tgt_lines"])}
    cdir = None
    if spec.archive and session is not None:
        cdir = archive(session, spec.driver, spec.case, lines_by_core,
                       board=spec.board,
                       ports={"CTRL": spec.ctrl_uart, "TGT": spec.tgt_uart},
                       timeout=spec.case_timeout, script=spec.script)
    return Acquired(ok=True, lines=lines_by_core, session=session, case_dir=cdir,
                    facts={"timed_out": raw["timed_out"], "elapsed_s": raw["elapsed_s"]})


def iwdg(spec):
    """IWDG（1 份）：构建 → prime 轮（丢弃）→ capture 轮 → 分核归一 → 落归档。"""
    if not spec.ctrl_uart or not spec.tgt_uart:
        log("ERROR: --ctrl-uart/--tgt-uart required（双核脚本不支持自动发现）")
        return Acquired(ok=False, err="ports required")
    log(f"Controller UART (cpu2): {spec.ctrl_uart}")
    log(f"Target UART (cpu1):     {spec.tgt_uart}")

    # app_iwdg 的 BASH 是模块级字面常量（不走 _find_git_bash），逐字保留
    bash = ("C:/Program Files/Git/bin/bash.exe"
            if Path("C:/Program Files/Git/bin/bash.exe").exists() else "bash")
    # E-2（每 case 前置）：DebugServer 不活 ⇒ fail-closed，构建前中止。
    ds_ok, ds_msg = ensure_debugserver_alive(style="d3")
    if not ds_ok:
        log(f"[ERROR] DebugServer 前置检查失败：{ds_msg}")
        return Acquired(ok=False, err=f"DebugServer unavailable: {ds_msg}")
    if ds_msg != DS_HEALTHY_MSG:      # 只有真发生过自愈才留痕
        log(f"[ENV] DebugServer 前置检查: {ds_msg}")
    if not spec.skip_build:
        log(f"[BUILD] {spec.case} (both cores)")
        rc = build_via_test_sh(spec.tests_dir / spec.case, spec.root, bash, at_root=True)
        if rc != 0:
            log(f"[ERROR] build failed rc={rc}")
            return Acquired(ok=False, err=f"build failed rc={rc}")

    ctrl_cap = SerialCapture(spec.ctrl_uart, mode="chunk_ansi")
    tgt_cap = SerialCapture(spec.tgt_uart, mode="chunk_ansi")
    ctrl_cap.start()
    tgt_cap.start()
    if ctrl_cap.error:
        log(f"[ERROR] {ctrl_cap.error}")
        return Acquired(ok=False, err=ctrl_cap.error)
    if tgt_cap.error:
        log(f"[ERROR] {tgt_cap.error}")
        return Acquired(ok=False, err=tgt_cap.error)

    # 两遍烧录法（SPEC_REFERENCE §6.2.3 #1）：prime 轮冲刷 I-cache 旧镜像，
    # 输出丢弃；pass-2 为正式轮，采集前清空行缓冲。
    log("[RUN] prime pass (output discarded) ...")
    run_pass_iwdg(spec, spec.case, ctrl_cap, tgt_cap, spec.case_timeout, bash)
    ctrl_cap.clear()
    tgt_cap.clear()

    log("[RUN] capture pass ...")
    _proc_lines, timed_out, _rc = run_pass_iwdg(spec, spec.case, ctrl_cap,
                                                tgt_cap, spec.case_timeout, bash)

    ctrl_lines = list(ctrl_cap.lines)
    tgt_lines = list(tgt_cap.lines)
    ctrl_cap.stop()
    tgt_cap.stop()
    ctrl_lines = [ln for _, ln in ctrl_lines]
    tgt_lines = [ln for _, ln in tgt_lines]

    # L1→L2 边界（L0 归档形状）：两核原始行**分核**归一；`timed_out` 是未归档的
    # 采集事实（facts）⇒ 只作报告注释项，不参与判定。
    lines_by_core = {"CTRL": ctrl_lines, "TGT": tgt_lines}
    # 单出口：从核行只在主核流里 ⇒ 先按核信封分拣回从核列表，再归档 / 交 L2
    lines_by_core = split_relay_lines(lines_by_core, spec.relay_prefixes,
                                      strip=spec.relay_strip)
    session = _resolve_session(spec.session)
    cdir = None
    if spec.archive and session is not None:
        cdir = archive(session, spec.driver, spec.case, lines_by_core,
                       board=spec.board,
                       ports={"CTRL": spec.ctrl_uart, "TGT": spec.tgt_uart},
                       timeout=spec.case_timeout, script=spec.script,
                       split_from_primary=bool(spec.relay_prefixes),
                       envelope_stripped=bool(spec.relay_strip))
        if spec.extra_logs:
            # 本项目独有的旁证文件（cpu1/cpu2 合并文本）；与 L2 的报告输出无关。
            # 用**分拣后**的列表 ⇒ 与归档同形（从核行不再混在主核段里）。
            logs = session / "logs"
            logs.mkdir(parents=True, exist_ok=True)
            (logs / f"{spec.driver}_{spec.case}.log").write_text(
                "# cpu1\n" + "\n".join(lines_by_core.get("TGT", []))
                + "\n\n# cpu2\n" + "\n".join(lines_by_core.get("CTRL", [])),
                encoding="utf-8")
    return Acquired(ok=True, lines=lines_by_core, session=session, case_dir=cdir,
                    facts={"timed_out": timed_out, "case_timeout": spec.case_timeout})
