#!/usr/bin/env python3
"""手动验证：单遍烧录 + 串口采集，gdb 用 \x03 interrupt + detach 干净退出。

与 dbg_run.py 的区别：不 kill gdb（避免 DebugServer 连接残留），
用于区分"连接残留导致不跑" vs "镜像本身跑不起来"。

用法:
    python scripts/vrfy_run.py --case case_pure_i3c_12m5 [--timeout 20]
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

TESTS_DIR = Path(__file__).resolve().parents[1]
GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
TARGET_ROM_START = "0x100c0000"
CONTROLLER_LOAD_ADDR = "0x10000000"
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


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
            lines.append(ANSI_RE.sub("", line.decode("utf-8", "replace").rstrip("\r")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", default="case_pure_i3c_6m25")
    ap.add_argument("--timeout", type=float, default=20)
    args = ap.parse_args()

    build = TESTS_DIR / args.case / "build"
    target_elf = (build / "target" / "zephyr" / "zephyr.elf").as_posix()
    controller_bin = (build / "controller" / "zephyr" / "zephyr.bin").as_posix()

    names = []
    for p in list_ports.comports():
        hwid = (p.hwid or "").lower()
        desc = (p.description or "").lower()
        if "32bf" in hwid or "cklink" in desc:
            continue
        names.append(p.device)
    print(f"[vrfy] ports: {names}")

    sers, threads, lines = {}, [], {}
    for name in names:
        try:
            sers[name] = serial.Serial(name, 115200, timeout=0.1)
            lines[name] = []
            stop = threading.Event()
            t = threading.Thread(target=reader, args=(sers[name], lines[name], stop), daemon=True)
            t.start()
            threads.append((name, t, stop))
        except serial.SerialException as e:
            print(f"[vrfy] WARNING: cannot open {name}: {e}")

    gdb_cmd = [
        GDB, "-q",
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
    proc = subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    print(f"[vrfy] running {args.timeout}s ...")
    time.sleep(args.timeout)
    try:
        proc.stdin.write("\x03")  # interrupt: 目标暂停，不留残留
        proc.stdin.flush()
        time.sleep(1.5)
        proc.stdin.write("detach\nquit\n")
        proc.stdin.flush()
        out, _ = proc.communicate(timeout=10)
    except Exception:
        proc.kill()
        out, _ = proc.communicate()

    for name, t, stop in threads:
        stop.set()
        t.join(timeout=1)
        sers[name].close()

    print("\n===== UART OUTPUT =====")
    for name in names:
        if name not in lines:
            continue
        print(f"--- {name}: {len(lines[name])} lines ---")
        for ln in lines[name][:80]:
            print("  " + ln)
        if len(lines[name]) > 80:
            print(f"  ... ({len(lines[name]) - 80} more)")

    print("\n===== GDB TAIL =====")
    print(out[-800:])


if __name__ == "__main__":
    main()
