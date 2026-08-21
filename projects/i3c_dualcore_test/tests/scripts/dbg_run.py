#!/usr/bin/env python3
"""One-shot debug runner: GDB-load both cores, capture UART, parse tokens.

Usage:
    python tests/dbg_run.py [--case case_pure_i3c_6m25] [--timeout 15] [--raw]
    python tests/dbg_run.py --case case_pure_i3c_12m5 --ctrl-uart COM16 --tgt-uart COM17

UART role discovery (dynamic):
    - By default all non-CKLink COM ports are opened; after the flash/reset
      the firmware prints its banner on the matching UART:
          "CPU2 MASTER" / "i3c_master_test"  -> controller (uart5)
          "CPU1 SLAVE"  / "i3c_slave_test"   -> target (uart1)
      so port roles are resolved from actual output, no hardcoding.
    - Explicit override: --ctrl-uart COM16 --tgt-uart COM17.

Assumes firmware is already built under tests/<case>/build/{controller,target}.
"""
import argparse
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

import serial
import serial.tools.list_ports as list_ports

# tests/scripts/dbg_run.py -> parents[1] = i3c_dualcore_test/tests
TESTS_DIR = Path(__file__).resolve().parents[1]
GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
TARGET_ROM_START = "0x100c0000"
CONTROLLER_LOAD_ADDR = "0x10000000"

CTRL_BANNERS = ("CPU2 MASTER", "i3c_master_test")
TGT_BANNERS = ("CPU1 SLAVE", "i3c_slave_test")

# Zephyr console shell 重绘提示符会向串口发 ANSI 转义，token 行首可能混入
# （如 \x1b[1;32muart:~$ \x1b[8D\x1b[J），先剥离再匹配。
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
TOKEN_RE = re.compile(r"I3C_TEST:(CTRL|TGT):(START|PASS|FAIL):(\w+)(?::(.*))?\s*")
SUMMARY_RE = re.compile(r"I3C_TEST:(CTRL|TGT):SUMMARY:(.*)")
DONE_RE = re.compile(r"I3C_TEST:DONE:(PASS|FAIL)")


def reader(ser, lines, stop):
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
    """Return the list of COM port names to capture.

    explicit: optional list of --ctrl-uart/--tgt-uart values; they are opened
    first (and only once). Otherwise enumerate all physical ports and drop the
    CKLink debugger's own CDC port (its USB VID:PID 32BF:B210).
    """
    ports = list(explicit) if explicit else []
    if ports:
        return ports
    for p in list_ports.comports():
        hwid = (p.hwid or "").lower()
        desc = (p.description or "").lower()
        if "32bf" in hwid or "cklink" in desc:
            continue  # CKLink debugger's own port, not board UART
        ports.append(p.device)
    if not ports:
        sys.exit("ERROR: no candidate COM port found (all ports are CKLink?)")
    print(f"[dbg] candidate UART ports: {', '.join(ports)}", flush=True)
    return ports


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", default="case_pure_i3c_6m25")
    ap.add_argument("--timeout", type=float, default=15)
    ap.add_argument("--raw", action="store_true", help="dump all captured lines")
    ap.add_argument("--ctrl-uart", default=None, help="controller UART, e.g. COM16")
    ap.add_argument("--tgt-uart", default=None, help="target UART, e.g. COM17")
    args = ap.parse_args()

    build = TESTS_DIR / args.case / "build"
    target_elf = (build / "target" / "zephyr" / "zephyr.elf").as_posix()
    controller_bin = (build / "controller" / "zephyr" / "zephyr.bin").as_posix()
    for f in (target_elf, controller_bin):
        if not Path(f).exists():
            sys.exit(f"ERROR: not built: {f}")

    explicit = [p for p in (args.ctrl_uart, args.tgt_uart) if p]
    names = discover_ports(explicit)
    ports = {name: {"lines": [], "stop": threading.Event()} for name in names}

    def start_capture():
        """Open ports and start reader threads (capture pass only, so the log
        contains one clean run; the prime pass output is discarded)."""
        sers = {}
        for name in names:
            try:
                sers[name] = serial.Serial(name, 115200, timeout=0.1)
            except serial.SerialException as e:
                print(f"[dbg] WARNING: cannot open {name}: {e}", flush=True)
        if not sers:
            sys.exit("ERROR opening serial: no port could be opened")
        threads = []
        for name, ser in sers.items():
            t = threading.Thread(target=reader, args=(ser, ports[name]["lines"], ports[name]["stop"]), daemon=True)
            t.start()
            threads.append(t)
        return sers, threads

    gdb_cmd = [
        GDB,
        "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
        "-ex", "set confirm off",
        "-ex", "set pagination off",
        "-ex", f"file {target_elf}",
        "-ex", "reset",
        "-ex", "load",
        "-ex", f"restore {controller_bin} binary {CONTROLLER_LOAD_ADDR}",
        "-ex", f"set $pc={TARGET_ROM_START}",
        "-ex", "continue",
    ]

    def flash_once(run_secs):
        """烧录并运行 run_secs 秒。

        超时后必须干净退出（interrupt + detach + quit），不能 proc.kill():
        kill 会让 DebugServer 认为连接仍存在（连接残留），下次 target
        remote 能连上但 reset/load 实际无效 → 固件不跑、串口 0 行。
        详见 CLAUDE.md §10.1 / qsh-debug §2.3。
        """
        proc = subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        try:
            out, _ = proc.communicate(timeout=run_secs)
        except subprocess.TimeoutExpired:
            try:
                proc.stdin.write("\x03")   # interrupt: 暂停目标
                proc.stdin.flush()
                time.sleep(1.0)
                proc.stdin.write("detach\nquit\n")
                proc.stdin.flush()
                out, _ = proc.communicate(timeout=10)
            except Exception:
                proc.kill()                # 兜底（正常路径不走到）
                out, _ = proc.communicate()
        # gdb 完整输出存文件，失败时打印尾部
        with open(TESTS_DIR / "dbg_gdb_last.log", "w", encoding="utf-8") as f:
            f.write(out)
        return out

    print(f"[dbg] flashing {args.case} via GDB (prime pass) ...", flush=True)
    # 两遍烧录法：第一遍加载并短暂运行，让 I-cache 热行内容 = 新镜像；
    # 第二遍再加载同一文件并正式运行+抓取。两遍写入字节完全相同，
    # 即使第二遍启动时取指命中的是第一遍残留的缓存行，行为也完全一致。
    out = flash_once(20)
    if "Loading section" not in out:
        print("[dbg] !!! prime flash failed:", flush=True)
        print(out[-1200:], flush=True)
        sys.exit(2)
    time.sleep(1)

    # 只在 capture 轮开串口采集：prime 轮输出丢弃，保证日志是单轮干净数据
    sers, threads = start_capture()

    print(f"[dbg] flashing again (capture pass), capturing {args.timeout}s ...", flush=True)
    proc = subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    time.sleep(args.timeout)  # target runs while gdb sits in 'continue'
    try:
        proc.stdin.write("\x03")   # 干净退出，避免连接残留（见 flash_once 注释）
        proc.stdin.flush()
        time.sleep(1.0)
        proc.stdin.write("detach\nquit\n")
        proc.stdin.flush()
        out, _ = proc.communicate(timeout=10)
    except Exception:
        proc.kill()
        out, _ = proc.communicate()
    if "Loading section" in out or "Restoring binary" in out:
        print("[dbg] flash OK", flush=True)
    else:
        print("[dbg] !!! FLASH DID NOT HAPPEN — gdb output: !!!", flush=True)
        print(out[-1500:], flush=True)
        sys.exit(2)
    time.sleep(0.3)

    for name in ports:
        ports[name]["stop"].set()
    for t in threads:
        t.join(timeout=1)
    for ser in sers.values():
        ser.close()

    # Identify roles by banner (dynamic discovery: role is determined by the
    # actual UART output after flash/reset, not by port name)
    role = {}
    for name, p in ports.items():
        text = "\n".join(p["lines"])
        if any(b in text for b in CTRL_BANNERS):
            role[name] = "CTRL"
        elif any(b in text for b in TGT_BANNERS):
            role[name] = "TGT"
        else:
            role[name] = "?"

    if args.ctrl_uart and args.ctrl_uart in role:
        role[args.ctrl_uart] = "CTRL"
    if args.tgt_uart and args.tgt_uart in role:
        role[args.tgt_uart] = "TGT"

    print("\n===== UART ROLE DISCOVERY =====", flush=True)
    for name in ports:
        print(f"  {name} -> {role[name]}", flush=True)
    if "?" in role.values():
        print("  (WARNING: un-identified port(s); no firmware banner seen there)", flush=True)

    print("\n===== SUMMARY =====")
    for name, p in ports.items():
        print(f"--- {name} ({role[name]}): {len(p['lines'])} lines ---")
        for ln in p["lines"]:
            if TOKEN_RE.search(ln) or SUMMARY_RE.search(ln) or DONE_RE.search(ln):
                print("  " + ln)
        # key debug lines
        for ln in p["lines"]:
            if any(k in ln for k in ("Slave stop", "prefill", "SERRWARN", "URUN",
                                     "write_requested", "non-opcode", "drain bailed",
                                     "read failed", "read actual")):
                print("  * " + ln)

    if args.raw:
        for name, p in ports.items():
            print(f"\n===== RAW {name} ({role[name]}) =====")
            for ln in p["lines"]:
                print(ln)


if __name__ == "__main__":
    main()
