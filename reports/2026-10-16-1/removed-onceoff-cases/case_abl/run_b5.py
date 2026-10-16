#!/usr/bin/env python3
"""B5 采集器：烧录 case_abl 固件 + 经串口**发 shell 命令触发扫描** + 带到达时间戳采集。

只有 B5 需要它（其它组开机自跑 marker，用 ../scripts/dbg_run.py --case case_abl 即可）。
理由：B5 的扫描由 shell 线程执行（shell 命令 `scanburst`），须在 shell 起来后**主动发命令**，
dbg_run.py 不发串口输入。

用法:
    python tests/case_abl/run_b5.py --timeout 60 --tgt-uart COM16 --cmd scanburst

产出：stdout 打印带 `+<相对秒>` 前缀的原始行（同 dbg_run.py），供人工/脚本取
SCAN_BURST_BEGIN / SCAN_BURST_END 两行的到达时刻差 = 排空跨度。
"""
import argparse
import os
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

import serial
import serial.tools.list_ports as list_ports

HERE = Path(__file__).resolve().parent
TESTS_DIR = HERE.parent                       # tests/
sys.path.insert(0, str(TESTS_DIR.parents[2] / "common"))
from acquire import dbg_run_preflight  # noqa: E402

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
TARGET_ROM_START = os.environ.get("TARGET_ROM_START", "0x10000000")

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def reader(ser, lines, times, stop):
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
            times.append(time.time())


def discover_ports(explicit):
    if explicit:
        return list(explicit)
    ports = []
    for p in list_ports.comports():
        hwid = (p.hwid or "").lower()
        if "32bf" in hwid or "cklink" in (p.description or "").lower():
            continue
        ports.append(p.device)
    return ports


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", default="case_abl")
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--tgt-uart", default=None)
    ap.add_argument("--cmd", default="scanburst")
    args = ap.parse_args()

    elf = (TESTS_DIR / args.case / "build" / "target" / "zephyr" / "zephyr.elf").as_posix()
    if not Path(elf).exists():
        sys.exit(f"ERROR: not built: {elf}")

    dbg_run_preflight()

    gdb_cmd = [
        GDB,
        "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
        "-ex", "set confirm off",
        "-ex", "set pagination off",
        "-ex", f"file {elf}",
        "-ex", "reset",
        "-ex", "monitor flash erase_address 0x10000000 0x10000",
        "-ex", "set memory-write-packet-size 16384",
        "-ex", "set remotewritesize 16384",
        "-ex", "load",
        "-ex", f"set $pc={TARGET_ROM_START}",
        "-ex", "set {int}0x4002200c = 2",
        "-ex", "continue",
    ]

    def spawn():
        return subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)

    def drain(proc, sink):
        try:
            for ln in proc.stdout:
                sink.append(ln)
        except Exception:
            pass

    def clean_exit(proc):
        try:
            proc.stdin.write("\x03")
            proc.stdin.flush()
            time.sleep(1.0)
            proc.stdin.write("detach\nquit\n")
            proc.stdin.flush()
            proc.wait(timeout=15)
        except Exception:
            proc.kill()
            proc.wait()

    # ---- prime pass（输出丢弃，I-cache 热身）----
    print("[b5] prime flash ...", flush=True)
    p = spawn()
    g = []
    threading.Thread(target=drain, args=(p, g), daemon=True).start()
    try:
        p.wait(timeout=25)
    except subprocess.TimeoutExpired:
        clean_exit(p)
    if not any("Loading section" in x for x in g):
        print("[b5] !!! prime flash failed"); print("".join(g)[-1200:]); sys.exit(2)
    time.sleep(1)

    # ---- capture pass ----
    names = discover_ports([args.tgt_uart] if args.tgt_uart else None)
    if not names:
        sys.exit("ERROR: no candidate COM port")
    print(f"[b5] candidate ports: {names}", flush=True)
    sers = {}
    for n in names:
        try:
            sers[n] = serial.Serial(n, 115200, timeout=0.1)
        except serial.SerialException as e:
            print(f"[b5] WARNING cannot open {n}: {e}", flush=True)
    if not sers:
        sys.exit("ERROR: no port openable")

    lines, times, stop = [], [], threading.Event()
    threads = []
    for n, s in sers.items():
        t = threading.Thread(target=reader, args=(s, lines, times, stop), daemon=True)
        t.start(); threads.append(t)

    print(f"[b5] capture pass, flash + run {args.timeout}s ...", flush=True)
    proc = spawn()
    gout = []
    threading.Thread(target=drain, args=(proc, gout), daemon=True).start()

    # 等 shell 提示符出现（或 8s），再发命令
    t_start = time.time()
    sent = False
    while time.time() - t_start < args.timeout:
        if not sent and (time.time() - t_start) > 2.0 and len(lines) > 0:
            time.sleep(1.0)
            try:
                sers[next(iter(sers))].write(b"\r")
                time.sleep(0.5)
                cmd = (args.cmd + "\r").encode()
                sers[next(iter(sers))].write(cmd)
                print(f"[b5] sent shell cmd: {args.cmd!r} at +{time.time()-t_start:.2f}s", flush=True)
            except Exception as e:
                print(f"[b5] send failed: {e}", flush=True)
            sent = True
        if proc.poll() is not None:
            break
        time.sleep(0.2)

    clean_exit(proc)
    stop.set()
    for t in threads:
        t.join(timeout=1)
    for s in sers.values():
        s.close()

    ok = any("Loading section" in x for x in gout)
    print(f"[b5] flash {'OK' if ok else 'FAILED'}", flush=True)

    t0 = times[0] if times else None
    for i, ln in enumerate(lines):
        if t0 is not None and i < len(times):
            print(f"+{times[i]-t0:8.3f}  {ln}", flush=True)
        else:
            print(ln, flush=True)
    if not ok:
        print("".join(gout)[-1500:])


if __name__ == "__main__":
    main()
