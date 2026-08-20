#!/usr/bin/env python3
"""One-shot debug runner: GDB-load both cores, capture COM15/COM16, parse tokens.

Usage:
    python tests/dbg_run.py [--case case_pure_i3c_6m25] [--timeout 15] [--raw]

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

TESTS_DIR = Path(__file__).resolve().parent
GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "localhost", "1025"
TARGET_ROM_START = "0x100c0000"
CONTROLLER_LOAD_ADDR = "0x10020000"

TOKEN_RE = re.compile(r"^I3C_TEST:(CTRL|TGT):(START|PASS|FAIL):(\w+)(?::(.*))?\s*$")
SUMMARY_RE = re.compile(r"^I3C_TEST:(CTRL|TGT):SUMMARY:(.*)$")
DONE_RE = re.compile(r"^I3C_TEST:DONE:(PASS|FAIL)\s*$")


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
            lines.append(line.decode("utf-8", "replace").rstrip("\r"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", default="case_pure_i3c_6m25")
    ap.add_argument("--timeout", type=float, default=15)
    ap.add_argument("--raw", action="store_true", help="dump all captured lines")
    args = ap.parse_args()

    build = TESTS_DIR / args.case / "build"
    target_elf = (build / "target" / "zephyr" / "zephyr.elf").as_posix()
    controller_bin = (build / "controller" / "zephyr" / "zephyr.bin").as_posix()
    for f in (target_elf, controller_bin):
        if not Path(f).exists():
            sys.exit(f"ERROR: not built: {f}")

    ports = {}
    for name in ("COM15", "COM16"):
        ports[name] = {"lines": [], "stop": threading.Event()}
    try:
        sers = {name: serial.Serial(name, 115200, timeout=0.1) for name in ports}
    except serial.SerialException as e:
        sys.exit(f"ERROR opening serial: {e}")

    threads = []
    for name, ser in sers.items():
        t = threading.Thread(target=reader, args=(ser, ports[name]["lines"], ports[name]["stop"]), daemon=True)
        t.start()
        threads.append(t)

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
        proc = subprocess.Popen(gdb_cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        try:
            out, _ = proc.communicate(timeout=run_secs)
        except subprocess.TimeoutExpired:
            proc.kill()
            out, _ = proc.communicate()
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

    print(f"[dbg] flashing again (capture pass), capturing {args.timeout}s ...", flush=True)
    proc = subprocess.Popen(gdb_cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    time.sleep(args.timeout)  # target runs while gdb sits in 'continue'
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

    # Identify roles by banner
    role = {}
    for name, p in ports.items():
        text = "\n".join(p["lines"])
        if "CPU2 MASTER" in text or "i3c_master_test" in text:
            role[name] = "CTRL"
        elif "CPU1 SLAVE" in text or "i3c_slave_test" in text:
            role[name] = "TGT"
        else:
            role[name] = "?"

    print("\n===== SUMMARY =====")
    for name, p in ports.items():
        print(f"--- {name} ({role[name]}): {len(p['lines'])} lines ---")
        for ln in p["lines"]:
            if TOKEN_RE.match(ln) or SUMMARY_RE.match(ln) or DONE_RE.match(ln):
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
