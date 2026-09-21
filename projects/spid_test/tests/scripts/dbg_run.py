#!/usr/bin/env python3
"""One-shot debug runner (single-core): GDB-load cpu1 ELF, capture UART, parse ztest.

TPM SPI slave (PFR/TPM) 单核工程版：从 trng_test（单核样板）dbg_run.py 改造，只加载 cpu1 ELF；
角色只有 TGT。

Usage:
    python scripts/dbg_run.py [--case case_default] [--timeout 20] [--raw]
    python scripts/dbg_run.py --case case_default --tgt-uart COM16

UART role discovery (dynamic):
    - By default all non-CKLink COM ports are opened; after the reset the
      firmware prints its banner on the matching UART:
          "CPU1 SPID"  -> target (uart1)
      so the port role is resolved from actual output, no hardcoding.
    - Explicit override: --tgt-uart COM16.

Assumes firmware is already built under tests/<case>/build/target.
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

# tests/scripts/dbg_run.py -> parents[1] = spid_test/tests
TESTS_DIR = Path(__file__).resolve().parents[1]

sys.path.insert(0, str(TESTS_DIR.parents[2] / "common"))
from ztest_report import parse_ztest   # noqa: E402
from acquire import dbg_run_preflight  # noqa: E402

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
# 用户需求#8：单核一律 1os 构建，cpu1 入口 = 0x10000000（readelf 实测）；
# 2os 构建入口 0x100c0000，需显式 TARGET_ROM_START=0x100c0000 环境变量覆盖
TARGET_ROM_START = os.environ.get("TARGET_ROM_START", "0x10000000")

TGT_BANNERS = ("CPU1 SPID", "spid_test")

# Zephyr console 重绘提示符会向串口发 ANSI 转义，行首可能混入转义序列，先剥离再匹配。
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
            line = ANSI_RE.sub("", line.decode("utf-8", "replace").rstrip("\r"))
            lines.append(line)


def discover_ports(explicit=None):
    """Return the list of COM port names to capture.

    explicit: optional list of --tgt-uart values; they are opened first.
    Otherwise enumerate all physical ports and drop the CKLink debugger's
    own CDC port (its USB VID:PID 32BF:B210).
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
    ap.add_argument("--case", default="case_default")
    ap.add_argument("--timeout", type=float, default=20)
    ap.add_argument("--raw", action="store_true", help="dump all captured lines")
    ap.add_argument("--tgt-uart", default=None, help="target UART, e.g. COM16")
    args = ap.parse_args()

    build = TESTS_DIR / args.case / "build"
    target_elf = (build / "target" / "zephyr" / "zephyr.elf").as_posix()
    if not Path(target_elf).exists():
        sys.exit(f"ERROR: not built: {target_elf}")

    dbg_run_preflight()   # F1：DS 不健康先自愈，起不来则明确中止（不退化 error 138）
    names = discover_ports([p for p in (args.tgt_uart,) if p])
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

    # 用户需求#7：单核测试按住 cpu2 复位（SYSC_SEC_CPU.APP_CPU_SRST=
    # 0x4002200c 是 W1S/W1C 寄存器：写 2=保持复位、写 1=释放、写 0 无效果），
    # 防他方 RAM 残留镜像串扰本固件采集；跑完不释放（双核流程自会写 1 释放）。
    # 与 scripts/test_common.sh load_and_run_single 同款（T6 已加）。
    gdb_cmd = [
        GDB,
        "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
        "-ex", "set confirm off",
        "-ex", "set pagination off",
        "-ex", f"file {target_elf}",
        "-ex", "reset",
        "-ex", "monitor flash erase_address 0x10000000 0x10000",
        "-ex", "set memory-write-packet-size 16384",
        "-ex", "set remotewritesize 16384",
        "-ex", "load",
        "-ex", f"set $pc={TARGET_ROM_START}",
        "-ex", "set {int}0x4002200c = 2",
        "-ex", "continue",
    ]

    def flash_once(run_secs):
        """烧录并运行 run_secs 秒。

        超时后必须干净退出（interrupt + detach + quit），不能 proc.kill():
        kill 会让 DebugServer 认为连接仍存在（连接残留），下次 target
        remote 能连上但 reset/load 实际无效 → 固件不跑、串口 0 行。
        详见 qsh-debug §2.3。
        """
        proc = subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        # ★ 不能用 communicate()：它会关闭 proc.stdin，之后再写 "\x03" 必抛 ValueError
        # → 优雅中断 (interrupt+detach+quit) 直接退化成 proc.kill() → DebugServer 连接残留
        # （qsh-debug §2.3）。改 drain 线程持续排空 stdout， stdin 全程保持可写，\x03 才真发得出去。
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
    # ★ capture 轮必须持续排空 gdb stdout：Windows 匿名管道缓冲写满后 gdb 阻塞
    # 在写 stdout 上 → DebugServer 会话停摆 → 目标被中途 halt，长用例尾部
    # token（PASS / PROJECT EXECUTION）永远采不到。见 qsh-test-gen §1.5。
    gdb_out = []

    def _drain_gdb():
        try:
            for ln in proc.stdout:
                gdb_out.append(ln)
        except Exception:
            pass
    threading.Thread(target=_drain_gdb, daemon=True).start()

    time.sleep(args.timeout)  # target runs while gdb sits in 'continue'
    try:
        proc.stdin.write("\x03")   # 干净退出，避免连接残留（见 flash_once 注释）
        proc.stdin.flush()
        time.sleep(1.0)
        proc.stdin.write("detach\nquit\n")
        proc.stdin.flush()
        proc.wait(timeout=15)
        out = "".join(gdb_out)
    except Exception:
        proc.kill()
        out = "".join(gdb_out)
    if "Loading section" in out:
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

    # Identify role by banner (dynamic discovery, no hardcoding)
    role = {}
    for name, p in ports.items():
        text = "\n".join(p["lines"])
        role[name] = "TGT" if any(b in text for b in TGT_BANNERS) else "?"

    if args.tgt_uart and args.tgt_uart in role:
        role[args.tgt_uart] = "TGT"

    print("\n===== UART ROLE DISCOVERY =====", flush=True)
    for name in ports:
        print(f"  {name} -> {role[name]}", flush=True)
    if "?" in role.values():
        print("  (WARNING: un-identified port(s); no firmware banner seen there)", flush=True)

    print("\n===== SUMMARY =====")
    for name, p in ports.items():
        r = parse_ztest(p["lines"], tgt_banners=TGT_BANNERS)
        print(f"--- {name} ({role[name]}): {len(p['lines'])} lines "
              f"boot={r['boot_seen']} exec={r['execution']} ---")
        for tname, t in sorted(r["tests"].items()):
            extra = f"  reason: {t['reason']}" if t["reason"] else ""
            print(f"  {tname:<24} {t['result']}{extra}")
        for tname, kv in sorted(r["infos"].items()):
            print(f"  {tname:<24} INFO " + ", ".join(f"{k}={v}" for k, v in kv.items()))

    if args.raw:
        for name, p in ports.items():
            print(f"\n===== RAW {name} ({role[name]}) =====")
            for ln in p["lines"]:
                print(ln)


if __name__ == "__main__":
    main()
