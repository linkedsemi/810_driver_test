#!/usr/bin/env python3
"""环境预检（链路级）：串口 → DebugServer → 双核 load → 双核 boot banner。

全部通过 = exit 0；任一 FAIL = exit 2（批量中止，交 agent 排查 qsh-debug）。
范围不含外设接线探测（4 个 IO 检测后置到独立 IO 测试工程；IO 固定接死，
所有测试共用同一硬件环境）。

四级检查：
  1. 串口枚举：CTRL/TGT 端口必须存在且可打开（串口被占用 = FAIL）
  2. DebugServer：GDB 连 127.0.0.1:1025 成功即在线（§10.3 在线判定=能连，
     不用 netstat）
  3/4. 双核 boot：单遍双核 gdb load（两个 gdb 会话：cpu1 `load` ELF@0x100c0000、
     cpu2 `restore` bin@0x10000000，reset→load→set $pc→continue，结构与
     scripts/test_common.sh load_and_run 一致），两侧 UART 各自收到本固件
     banner 与 ZT_BOOT（两行分列判据，§A2——固件把 banner 与 ZT_BOOT 打在
     两行，同行为真判据永远不会命中）

自愈（run_i2c_pure_tests.py 已迁移实现的原样逻辑）：
  - DebugServer 探测连接会留残留（§10.17）→ 双核 load 前 clean_gdb_residue
    （先杀残留 gdb，ESTABLISHED 未释放或 server 已死才按需重启）
  - load 失败 / 双路均无 banner（§10.22 load_fail）→ 重启 DebugServer
    （含 5s 稳定期，§10.20）重试 1 次；重试仍失败才判 FAIL

用法：
  python regression/precheck/run_precheck.py [--session-dir <dir>] [--timeout 40]

环境变量：
  PRE_CTRL_UART   controller UART（默认 COM15，MCP2200 = cpu2 uart5）
  PRE_TGT_UART    target UART（默认 COM16，CH342 = cpu1 uart1）
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import List, Optional, Tuple

try:
    import serial
    import serial.tools.list_ports as list_ports
except ImportError as exc:
    print(f"ERROR: pyserial is required: {exc}", file=sys.stderr)
    sys.exit(2)

# ---- 路径与工具 ----

PRE_DIR = Path(__file__).resolve().parent
CTRL_FW = PRE_DIR / "fw" / "cpu2_controller"
TGT_FW = PRE_DIR / "fw" / "cpu1_target"
TARGET_ELF = TGT_FW / "build" / "zephyr" / "zephyr.elf"
CONTROLLER_BIN = CTRL_FW / "build" / "zephyr" / "zephyr.bin"

GDB = shutil.which("riscv64-unknown-elf-gdb") or "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
TARGET_ROM_START = "0x100c0000"      # cpu1 入口（§3，与 test_common.sh 一致）
CONTROLLER_LOAD_ADDR = "0x10000000"  # cpu2 RAM 基址（§3）
DEBUGSERVER_EXE = r"C:\uaer_app\debug_server\bin\DebugServerConsole.exe"

CTRL_UART = os.environ.get("PRE_CTRL_UART") or None   # None = auto-discover
TGT_UART  = os.environ.get("PRE_TGT_UART")  or None
CTRL_BANNER = "[PRE CK CTRL]"
TGT_BANNER = "[PRE CK TGT]"
CHECKS: List[Tuple[str, bool, str]] = []   # [(name, passed, detail)]
# 自动发现结果（由 gdb_flash_and_capture 填充，check_dual_boot / main 读取）
_discovered_ctrl: Optional[str] = None
_discovered_tgt: Optional[str] = None

# Zephyr console shell 重绘提示符会向串口发 ANSI 转义，token 行首可能混入
# （§10.5），先剥离再匹配（trng dbg_run.py 同款）。
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def win_path(p) -> str:
    """gdb 是 Windows 原生程序，不吃 MSYS 的 /d/... 路径（§10.28）→ cygpath -m。"""
    try:
        out = subprocess.run(["cygpath", "-m", str(p)],
                             capture_output=True, text=True, timeout=10)
        if out.returncode == 0 and out.stdout.strip():
            return out.stdout.strip()
    except Exception:
        pass
    return str(p).replace("\\", "/")


# ---- 串口枚举（trng_test dbg_run.py discover_ports 原样逻辑）----

def serial_enumerate() -> List[str]:
    """枚举非 CKLink 串口（CKLink 自身 CDC 口 VID 32BF:B210 排除，§0）。"""
    ports = []
    for p in list_ports.comports():
        hwid = (p.hwid or "").lower()
        desc = (p.description or "").lower()
        if "32bf" in hwid or "cklink" in desc:
            continue  # CKLink debugger's own port, not board UART
        ports.append(p.device)
    return ports


def _serial_openable(name: str) -> Tuple[bool, str]:
    """端口存在且可打开（被其他进程占用 = FAIL 依据）。"""
    try:
        ser = serial.Serial(name, 115200, timeout=0.1)
        ser.close()
        return True, ""
    except Exception as exc:
        return False, str(exc)


# ---- DebugServer 环境管理（run_i2c_pure_tests.py 原样逻辑）----

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
                rows.append((parts[3], parts[4], parts[1]))
    except Exception:
        pass
    return rows


def _kill_residual_gdb() -> None:
    """精准杀 1025 ESTABLISHED 连接对端（是 gdb 就 taskkill）。

    ★ Python subprocess 无 MSYS 路径转换层：tasklist/taskkill 参数必须用
    单斜杠（/FI /FO /F /PID），双斜杠 //F 会被 Windows 程序拒绝（§10.16）。
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
    留下连接残留——detach 不能真正释放连接，唯一可靠的方式是重启进程
    （§10.17）。CKLink 初始化耗时随机，不能只固定 sleep → 日志轮询。
    """
    print("[precheck] restarting DebugServer (residue)", file=sys.stderr)
    try:
        subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=15)
    except Exception:
        pass   # taskkill 超时/失败不阻断自愈流程（netstat 精确杀兜底，§10.17）
    time.sleep(1)
    # taskkill /IM 可能杀不净（无效实例占 1025，新实例绑定失败卡在初始化）
    # → 用 netstat 精确杀 1025 的 LISTENING 进程兜底。
    for state, local_pid, _ in _netstat_1025():
        if state == "LISTENING" and local_pid.isdigit():
            try:
                subprocess.run(["taskkill", "/F", "/PID", local_pid],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=15)
            except Exception:
                pass
    log_path = os.path.join(tempfile.gettempdir(), "debug_server.log")
    try:
        os.remove(log_path)
    except OSError:
        pass
    try:
        subprocess.Popen(
            [DEBUGSERVER_EXE],
            cwd=str(Path(DEBUGSERVER_EXE).parent),
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
        print("[precheck] WARNING: DebugServer 30s 内未打印就绪提示符", file=sys.stderr)
    time.sleep(1)


def clean_gdb_residue(force_restart: bool = False) -> None:
    """清 DebugServer 连接残留（§10.1 高频坑）。

    ★ 用户反馈：无条件重启太频繁（CKLink 开关 1s 3-4 次）→ 按需重启：
    先杀残留 gdb，杀完复查 1025——连接残留仍在（ESTABLISHED 未释放）或
    server 已死（无 LISTENING）才重启；健康则不重启（§10.17）。
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
        need = not any(st == "LISTENING" for st, _, _ in _netstat_1025())
    if need:
        _restart_debugserver()


def restart_debugserver() -> None:
    """重启 XuanTie DebugServer 并等稳定期（§10.20：新实例立即连跑会
    间歇性 load_fail/断连，5s 稳定期后再连）。"""
    _restart_debugserver()
    time.sleep(5)   # 稳定期：新实例 CKLink 初始化/总线状态稳定后再连跑


def gdb_connect_ok() -> bool:
    """DebugServer 在线判定 = GDB 能连 1025（§10.3：勿用 netstat）。"""
    try:
        result = subprocess.run(
            [GDB, "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
             "-ex", "detach", "-ex", "quit"],
            capture_output=True, text=True, timeout=15)
        out = (result.stdout or "") + (result.stderr or "")
        return "Remote debugging" in out
    except Exception:
        return False


# ---- 双核 gdb 加载（scripts/test_common.sh load_and_run 两会话结构）----

def _gdb_session_a() -> str:
    """会话 A：cpu1 target ELF `load` @0x100c0000 → set $pc → detach。

    同一 gdb 会话里连续两个大块写（load ELF 后 restore bin）会卡死
    （DebugServer 对同一 RSP 会话的第二次大块写不回应，实测必现）→
    拆成两个独立 gdb 会话。reset 只复位 CPU 不清 RAM，会话 B 的 reset
    不影响会话 A 已 load 的内容。
    """
    cmd = [GDB, "-batch",
           "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
           "-ex", "set output-radix 16",
           "-ex", "set confirm off",
           "-ex", "set pagination off",
           "-ex", f"set $ROM_START = {TARGET_ROM_START}",
           "-ex", f"file {win_path(TARGET_ELF)}",
           "-ex", "reset",
           "-ex", "load",
           "-ex", "set $pc=$ROM_START",
           "-ex", "set mem inaccessible-by-default on",
           "-ex", "set print pretty on",
           "-ex", "detach"]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired as exc:
        # gdb 挂起（DebugServer 卡死/板未复位）：subprocess.run 已终止子进程。
        # 不抛出——超时按 load 失败路径处理（§10.22 自愈重试 1 次），
        # 保住 exit 0/2 契约与 precheck_report 写入；残留连接由重启清理。
        out = (exc.stdout or "") + (exc.stderr or "")
        return out + "\n[precheck] 会话 A 超时（120s 无响应，gdb 已终止）"
    except Exception as exc:
        # 其他异常（如 gdb 进程失败）：同样不抛出，走 load 失败路径
        return f"[precheck] 会话 A 异常: {exc}"
    return (result.stdout or "") + (result.stderr or "")


def _gdb_session_b_proc():
    """会话 B：reset → restore cpu2 bin @0x10000000 → set $pc=入口 → continue。

    返回 `(gdb 子进程, 输出行列表)`（continue 挂住等目标，由调用方在采集结束后
    干净退出：interrupt → detach → quit；proc.kill 会留连接残留，§10.1）。

    ★ stdout 必须**从 Popen 那一刻起持续排空**（drain 线程），不能等收尾时才读：
    `restore` 把 cpu2 镜像的分段进度整段打到 stdout，采集窗内无人读该管道 ⇒
    Windows 匿名管道缓冲写满 ⇒ gdb 阻塞在写 stdout ⇒ DebugServer 会话停摆
    目标被中途 halt（**KNOWN_ISSUES #97 的同一机制**；本轮同族第 4 处）。
    ★ 收尾也不得用 `communicate()` 驱动：`communicate()` 会 close stdin（#101）
    ⇒ 其后的 `\\x03` 发不出去 ⇒ interrupt+detach 退化成 kill ⇒ 连接残留（§10.1）。
    故统一用「drain 线程 + `proc.wait()`」。
    """
    cmd = [GDB,
           "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
           "-ex", "set output-radix 16",
           "-ex", "set confirm off",
           "-ex", "set pagination off",
           "-ex", f"set $ROM_START = {TARGET_ROM_START}",
           "-ex", "reset",
           "-ex", f"restore {win_path(CONTROLLER_BIN)} binary {CONTROLLER_LOAD_ADDR}",
           "-ex", "set $pc=$ROM_START",
           "-ex", "set mem inaccessible-by-default on",
           "-ex", "set print pretty on",
           "-ex", "continue"]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    lines: List[str] = []

    def _drain():
        try:
            for ln in proc.stdout:
                lines.append(ln)
        except Exception:                      # noqa: BLE001  管道关闭即自然结束
            pass

    threading.Thread(target=_drain, daemon=True).start()
    return proc, lines


def _reader(ser, lines, stop):
    """串口后台采集线程（trng dbg_run.py 原样逻辑）。"""
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


def _boot_seen(lines: List[str], banner: str, boot_tag: str) -> bool:
    """banner 与 ZT_BOOT 打在两行 → 分列判据（§A2 修正同行为真判据 bug）。"""
    return any(banner in ln for ln in lines) and any(boot_tag in ln for ln in lines)


def gdb_flash_and_capture(timeout: int):
    """单遍双核 load + 多串口自动采集（CTRL/TGT 角色按 banner 自动识别）。

    若 CTRL_UART/TGT_UART 已由环境变量显式指定，直接用；否则打开所有
    非 CKLink 串口并行采集，capture 结束后按 banner 匹配分出 CTRL/TGT
    （找不到的留 None，由上层判 FAIL）。
    返回 (ctrl_lines, tgt_lines, load_ok, gdb_tail)。
    同时写入全局 _discovered_ctrl/_discovered_tgt 供 check_dual_boot 等使用。
    """
    global _discovered_ctrl, _discovered_tgt

    print(f"[precheck] gdb 会话 A: load {TARGET_ELF.name} (cpu1)", flush=True)
    out_a = _gdb_session_a()
    load_ok = "Loading section" in out_a
    if not load_ok:
        return [], [], False, out_a

    # ---- 确定要打开的串口 ----
    if CTRL_UART and TGT_UART:
        names = [CTRL_UART, TGT_UART]
    else:
        names = serial_enumerate()

    sers = {}
    for name in names:
        try:
            sers[name] = serial.Serial(name, 115200, timeout=0.1)
        except serial.SerialException as exc:
            print(f"[precheck] WARNING: cannot open {name}: {exc}", flush=True)
    ports = {name: {"lines": [], "stop": threading.Event()} for name in names}
    threads = []
    for name, ser in sers.items():
        t = threading.Thread(target=_reader,
                             args=(ser, ports[name]["lines"], ports[name]["stop"]),
                             daemon=True)
        t.start()
        threads.append(t)

    print(f"[precheck] gdb 会话 B: restore {CONTROLLER_BIN.name} (cpu2) + continue, "
          f"capture {timeout}s", flush=True)
    proc, out_b_lines = _gdb_session_b_proc()
    deadline = time.time() + timeout

    # 提前退出判据：若角色已知，双核 banner 齐了即停；否则至少跑满 timeout
    def _early_stop() -> bool:
        if CTRL_UART and TGT_UART:
            return (_boot_seen(ports.get(CTRL_UART, {}).get("lines", []), CTRL_BANNER, "ZT_BOOT CTRL")
                    and _boot_seen(ports.get(TGT_UART, {}).get("lines", []), TGT_BANNER, "ZT_BOOT TGT"))
        return False

    while time.time() < deadline:
        if proc.poll() is not None:
            break
        if _early_stop():
            break
        time.sleep(0.2)

    # 干净退出（interrupt → detach → quit），避免连接残留（§10.1）。
    # ★ 用 proc.wait() 而不是 communicate()：communicate() 会 close stdin，
    #   后续无 stdin 可用（#101）。输出由 drain 线程持续收集，见 _gdb_session_b_proc。
    if proc.poll() is None:
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
    out_b = "".join(out_b_lines)
    load_ok = load_ok and "Restoring" in out_b

    for name in ports:
        ports[name]["stop"].set()
    for t in threads:
        t.join(timeout=1)
    for ser in sers.values():
        ser.close()

    # ---- banner 角色自动识别 ----
    def _has_banner(lines: List[str], banner: str) -> bool:
        return any(banner in ln for ln in lines)

    if CTRL_UART and TGT_UART:
        ctrl_port, tgt_port = CTRL_UART, TGT_UART
    else:
        ctrl_port = tgt_port = None
        for name in names:
            lines = ports.get(name, {}).get("lines", [])
            if ctrl_port is None and _has_banner(lines, CTRL_BANNER):
                ctrl_port = name
            if tgt_port is None and _has_banner(lines, TGT_BANNER):
                tgt_port = name

    _discovered_ctrl = ctrl_port
    _discovered_tgt = tgt_port
    print(f"[precheck] role discovery: CTRL={ctrl_port}  TGT={tgt_port}", flush=True)

    ctrl_lines = ports.get(ctrl_port, {}).get("lines", []) if ctrl_port else []
    tgt_lines  = ports.get(tgt_port,  {}).get("lines", []) if tgt_port  else []
    return (ctrl_lines, tgt_lines, load_ok, (out_a + out_b)[-600:])


# ---- 四级检查 ----

def check_serial() -> bool:
    """串口预检：显式指定时检查对应端口存在且可开；自动发现时只检查有足够端口。"""
    ports = serial_enumerate()
    if CTRL_UART and TGT_UART:
        ok = CTRL_UART in ports and TGT_UART in ports
        detail = f"ports={ports} CTRL={CTRL_UART} TGT={TGT_UART}"
        if ok:
            ok_ctrl, e_ctrl = _serial_openable(CTRL_UART)
            ok_tgt, e_tgt = _serial_openable(TGT_UART)
            if not (ok_ctrl and ok_tgt):
                ok = False
                detail += f" open: CTRL={ok_ctrl}({e_ctrl}) TGT={ok_tgt}({e_tgt})"
    else:
        # ★ 2026-09-22 用户裁定**放宽**（原规则见下）。
        #
        # 原规则 = 「**全部**非 CKLink 口都必须可开」⇒ 桌面上只要有**一个无关串口**
        # 被别的进程占用就整批拦下（实测：CP210x `COM5` 被占 ⇒ `PermissionError`
        # ⇒ 回归根本跑不起来，只能改走显式端口绕）。
        #
        # 新判据 = 「**可开的口够 2 个**」—— 保住原规则的**本意**：
        #   · 「板子串口根本没枚举出来 / 被占」⇒ 可开口 < 2 ⇒ **仍然 FAIL** ✓
        #   · 多余的无关口被占 ⇒ 只记一行 note，**不再阻断** ✓
        # 至于"开出来的口是不是真的 CTRL/TGT"，由下游的 banner 角色识别兜住
        #（`check_dual_boot` 按 banner 分角色并各自报到 `CHECKS`），
        # 不靠这里"全都可开"来保证。
        openable = [n for n in ports if _serial_openable(n)[0]]
        bad = [n for n in ports if n not in openable]
        ok = len(openable) >= 2
        detail = (f"ports={ports} openable={openable} "
                  f"(auto-discover, need >= 2 openable)")
        if bad:
            detail += f" 不可开(已忽略、不阻断): {bad}"
    CHECKS.append(("串口枚举", ok, detail))
    return ok


def check_debugserver() -> bool:
    """GDB 连接 127.0.0.1:1025 成功即在线（在线判定=能连，不用 netstat）。"""
    ok = gdb_connect_ok()
    CHECKS.append(("DebugServer", ok, f"{GDB_HOST}:{GDB_PORT}"))
    if ok:
        # 探测连接本身留残留（§10.17）→ 双核 load 前按需清理
        clean_gdb_residue()
    return ok


def check_dual_boot(timeout: int) -> bool:
    """双核 load 预检固件（单遍），两侧 UART 各自收到 banner + ZT_BOOT。"""
    if not TARGET_ELF.exists() or not CONTROLLER_BIN.exists():
        missing = (TARGET_ELF if not TARGET_ELF.exists() else CONTROLLER_BIN)
        CHECKS.append(("CTRL boot", False, f"固件未构建: {missing}"))
        CHECKS.append(("TGT boot", False, "固件未构建"))
        return False

    ctrl_lines, tgt_lines, load_ok, gdb_tail = gdb_flash_and_capture(timeout)
    if not load_ok or (not ctrl_lines and not tgt_lines):
        # §10.22 load_fail 自愈：重启 DebugServer（含 5s 稳定期）重试 1 次
        print("[precheck] load_fail/无 boot 输出 → 重启 DebugServer 后重试 1 次"
              "（§10.22/§10.20）", file=sys.stderr)
        restart_debugserver()
        clean_gdb_residue()
        ctrl_lines, tgt_lines, load_ok, gdb_tail = gdb_flash_and_capture(timeout)

    if not load_ok:
        tail = " ".join(gdb_tail.split())[-200:]
        CHECKS.append(("CTRL boot", False, f"load 失败（自愈重试后）: {tail}"))
        CHECKS.append(("TGT boot", False, "load 失败，未采集"))
        return False

    ok_ctrl = (any(CTRL_BANNER in ln for ln in ctrl_lines)
               and any("ZT_BOOT CTRL" in ln for ln in ctrl_lines))
    ok_tgt = (any(TGT_BANNER in ln for ln in tgt_lines)
              and any("ZT_BOOT TGT" in ln for ln in tgt_lines))
    # 自动发现模式下，把发现结果同步到全局（main 写 JSON 时读取）
    global CTRL_UART, TGT_UART
    if _discovered_ctrl:
        CTRL_UART = _discovered_ctrl
    if _discovered_tgt:
        TGT_UART = _discovered_tgt
    CHECKS.append(("CTRL boot", ok_ctrl, f"{CTRL_UART} {len(ctrl_lines)} lines"))
    CHECKS.append(("TGT boot", ok_tgt, f"{TGT_UART} {len(tgt_lines)} lines"))
    return ok_ctrl and ok_tgt


def write_report(session_dir: Optional[str]) -> None:
    if not session_dir:
        return
    p = Path(session_dir) / "precheck_report.md"
    p.parent.mkdir(parents=True, exist_ok=True)   # session-dir 可能尚未创建
    lines = ["# 环境预检", "",
             "| 检查项 | 结果 | 说明 |", "|---|---|---|"]
    for name, ok, detail in CHECKS:
        lines.append(f"| {name} | {'PASS' if ok else 'FAIL'} | {detail} |")
    p.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[precheck] report -> {p}")


def main() -> int:
    # 行缓冲输出：run_all.py 逐行读进度
    try:
        sys.stdout.reconfigure(line_buffering=True)
        sys.stderr.reconfigure(line_buffering=True)
    except Exception:
        pass

    ap = argparse.ArgumentParser()
    ap.add_argument("--session-dir", default=None)
    ap.add_argument("--timeout", type=int, default=40)
    args = ap.parse_args()
    print("[precheck] 环境预检（链路级）开始")
    ok = check_serial()
    if ok:
        ok = check_debugserver()
    if ok:
        ok = check_dual_boot(args.timeout)
    write_report(args.session_dir)
    # 预检通过后写端口映射 JSON（run_all 读取后注入给各 case run 脚本）
    if ok and args.session_dir and CTRL_UART and TGT_UART:
        import json
        mapping_path = Path(args.session_dir) / "port_mapping.json"
        mapping_path.parent.mkdir(parents=True, exist_ok=True)
        mapping_path.write_text(json.dumps(
            {"ctrl": CTRL_UART, "tgt": TGT_UART}, indent=2), encoding="utf-8")
        print(f"[precheck] port_mapping -> {mapping_path}", flush=True)
    for name, passed, detail in CHECKS:
        print(f"[precheck] {'PASS' if passed else 'FAIL'} {name}: {detail}")
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
