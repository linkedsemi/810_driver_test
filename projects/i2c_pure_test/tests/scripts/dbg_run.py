#!/usr/bin/env python3
"""One-shot debug runner: GDB-load both cores, capture UART, parse ztest output.

Usage:
    python tests/dbg_run.py [--case case_pure_i2c_400k] [--timeout 15] [--raw]
    python tests/dbg_run.py --case case_pure_i2c_400k --ctrl-uart COM15 --tgt-uart COM16
    python tests/dbg_run.py --xip --bins cpu1.bin@0x08000000,cpu2.bin@0x08100000 --timeout 15

XIP mode (--xip): instead of GDB RAM-loading, program the given bin(s) to the
on-board SPI flash via scripts/flash_program.py (CSKYFlashProgramerConsole),
reset/run from flash and capture UART - same ztest parsing/SUMMARY as RAM
mode.  Flash programming needs exclusive CKLink access: the XuanTie
DebugServer must not be connected while programming (see CLAUDE.md §10/§15);
pass --gdb-reset to reset via DebugServer AFTER programming (it is free then).

UART role discovery (dynamic):
    - By default all non-CKLink COM ports are opened; after the flash/reset
      the firmware prints its banner on the matching UART:
          "CPU2 I2C_PURE_CTRL"  -> controller (uart5, cpu2)
          "CPU1 I2C_PURE_TGT"   -> target (uart1, cpu1)
      so port roles are resolved from actual output, no hardcoding.
    - Explicit override: --ctrl-uart COM15 --tgt-uart COM16.

ztest-native output (migration from token framework):
    逐项判定打印在串口（" PASS - i2c_wave in X.XXX seconds" 等，经
    CONFIG_ZTEST_VERBOSE_OUTPUT=y 门控，§10.26），SUMMARY 段用公共解析器
    common/ztest_report.parse_ztest 汇总（§4.5 记账数组仍可用 dump_results.py
    GDB 兜底读取）。dbg_run.py 是调试工具，不判定 PASS/FAIL，退出码恒 0
    （唯一非零退出 = flash 失败；§10.21）。

Assumes firmware is already built under tests/<case>/build/{controller,target}
(RAM mode) or a zephyr_flash_*.bin exists (XIP mode).
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

# tests/scripts/dbg_run.py -> parents[1] = i2c_pure_test/tests
TESTS_DIR = Path(__file__).resolve().parents[1]
# dbg_run.py at 810_driver_test/projects/<p>/tests/scripts/ -> parents[4] = 810_driver_test
DRIVER_TEST_ROOT = Path(__file__).resolve().parents[4]
FP_SCRIPT = DRIVER_TEST_ROOT / "scripts" / "flash_program.py"

# 公共解析层：810_driver_test/common/（trng 样板同款路径）
sys.path.insert(0, str(TESTS_DIR.parents[2] / "common"))
from ztest_report import parse_ztest  # noqa: E402
from acquire import dbg_run_preflight  # noqa: E402

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
TARGET_ROM_START = "0x100c0000"
CONTROLLER_LOAD_ADDR = "0x10000000"

CTRL_BANNERS = ("CPU2 I2C_PURE_CTRL", "i2c_pure_master_test")
TGT_BANNERS = ("CPU1 I2C_PURE_TGT", "i2c_pure_slave_test")

# Zephyr console shell 重绘提示符会向串口发 ANSI 转义，行首可能混入
# （如 \x1b[1;32muart:~$ \x1b[8D\x1b[J），采集时先剥离再入 lines（§10.5）。
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


def start_capture(names, ports):
    """Open ports and start reader threads (capture pass only, so the log
    contains one clean run; any prime pass output is discarded)."""
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
        t = threading.Thread(target=reader,
                             args=(ser, ports[name]["lines"], ports[name]["stop"]),
                             daemon=True)
        t.start()
        threads.append(t)
    return sers, threads


def finish_summary(ports, sers, threads, ctrl_uart, tgt_uart, raw):
    """Stop capture, identify roles by banner, print ztest SUMMARY (+ raw dump).

    SUMMARY 用 parse_ztest：ZT_BOOT / 逐项 PASS|FAIL / INFO 数值 /
    PROJECT EXECUTION 汇总展示（不判定成败——dbg_run 是调试工具，§10.21）。
    """
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

    if ctrl_uart and ctrl_uart in role:
        role[ctrl_uart] = "CTRL"
    if tgt_uart and tgt_uart in role:
        role[tgt_uart] = "TGT"

    print("\n===== UART ROLE DISCOVERY =====", flush=True)
    for name in ports:
        print(f"  {name} -> {role[name]}", flush=True)
    if "?" in role.values():
        print("  (WARNING: un-identified port(s); no firmware banner seen there)", flush=True)

    print("\n===== SUMMARY (ztest-native) =====")
    for name, p in ports.items():
        print(f"--- {name} ({role[name]}): {len(p['lines'])} lines ---")
        parsed = parse_ztest(p["lines"], ctrl_banners=CTRL_BANNERS,
                             tgt_banners=TGT_BANNERS)
        if parsed["boot_seen"]:
            print(f"  ZT_BOOT seen (banner role={parsed['role']})")
        for tname in sorted(parsed["tests"]):
            t = parsed["tests"][tname]
            reason = f" -- {t['reason']}" if t["reason"] else ""
            print(f"  {tname}: {t['result']}{reason}")
        for tname in sorted(parsed["infos"]):
            kv = ", ".join(f"{k}={v}" for k, v in sorted(parsed["infos"][tname].items()))
            print(f"  INFO {tname}: {kv}")
        if parsed["execution"] is not None:
            print(f"  PROJECT EXECUTION: {parsed['execution']}")
        if not parsed["boot_seen"] and not parsed["tests"] \
                and parsed["execution"] is None:
            print("  (no ztest output parsed on this port)")

    if raw:
        for name, p in ports.items():
            print(f"\n===== RAW {name} ({role[name]}) =====")
            for ln in p["lines"]:
                print(ln)


def run_xip(args):
    """XIP mode: program flash (flash_program.py), reset/run, capture UART.

    Flash programming owns the CKLink exclusively; the DebugServer must not
    be connected meanwhile.  After programming (tool exited), the CKLink is
    free again - then --gdb-reset can reset the board via DebugServer, or the
    flash programmer's --reset-and-run (already passed unless --no-reset-run)
    resets the board on the spot.
    """
    if not args.bins:
        sys.exit("ERROR: --xip requires --bins FILE@ADDR[,FILE@ADDR]")
    bins = []
    for spec in args.bins.split(","):
        if "@" in spec:
            path, _, a = spec.rpartition("@")
            bins.append((path, int(a, 0)))
        else:
            bins.append((spec, 0x08000000))  # default: cpu1 XIP base
    for path, _ in bins:
        if not Path(path).exists():
            sys.exit(f"ERROR: XIP bin not found: {path}")

    explicit = [p for p in (args.ctrl_uart, args.tgt_uart) if p]
    names = discover_ports(explicit)
    ports = {name: {"lines": [], "stop": threading.Event()} for name in names}

    # ---- 1. program flash ---------------------------------------------------
    print(f"[dbg] XIP: programming flash via {FP_SCRIPT.name} ...", flush=True)
    fp_cmd = [sys.executable, str(FP_SCRIPT),
              "--erase", str(args.erase), "--raw"]
    for path, addr in bins:
        fp_cmd += ["--bin", f"{Path(path).as_posix()}@{hex(addr)}"]
    if not args.no_reset_run:
        fp_cmd += ["--reset-and-run"]
    proc = subprocess.run(fp_cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=300)
    if proc.returncode != 0:
        print(proc.stdout[-2000:], flush=True)
        sys.exit(f"ERROR: XIP flash programming failed (rc={proc.returncode})")
    print("[dbg] XIP: flash programming OK", flush=True)

    # ---- 2. optional reset via DebugServer (tool already exited) ------------
    if args.gdb_reset:
        print("[dbg] XIP: reset via DebugServer ...", flush=True)
        reset_cmd = [GDB, "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
                     "-ex", "set confirm off", "-ex", "set pagination off",
                     "-ex", "reset", "-ex", "detach", "-ex", "quit"]
        r = subprocess.run(reset_cmd, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=15)
        if "Remote debugging" not in r.stdout + r.stderr:
            print("[dbg] WARNING: DebugServer not reachable for reset; "
                  "press the board reset button if it did not start", flush=True)

    # ---- 3. capture + summary ------------------------------------------------
    sers, threads = start_capture(names, ports)
    print(f"[dbg] XIP: capturing {args.timeout}s from flash-booted firmware ...",
          flush=True)
    time.sleep(args.timeout)
    finish_summary(ports, sers, threads, args.ctrl_uart, args.tgt_uart, args.raw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", default="case_pure_i2c_400k")
    ap.add_argument("--timeout", type=float, default=15)
    ap.add_argument("--raw", action="store_true", help="dump all captured lines")
    ap.add_argument("--ctrl-uart", default=None, help="controller UART, e.g. COM15")
    ap.add_argument("--tgt-uart", default=None, help="target UART, e.g. COM16")
    ap.add_argument("--xip", action="store_true",
                    help="XIP mode: program flash via flash_program.py, then reset/run")
    ap.add_argument("--bins", default=None,
                    help="XIP: comma-separated FILE@ADDR[,FILE@ADDR] to program to flash")
    ap.add_argument("--erase", type=int, choices=[0, 1, 2, 3], default=0,
                    help="XIP: flash erase type (default 0=chip erase)")
    ap.add_argument("--no-reset-run", action="store_true",
                    help="XIP: do not pass --reset-and-run to flash programmer")
    ap.add_argument("--gdb-reset", action="store_true",
                    help="XIP: reset the board via DebugServer after programming")
    args = ap.parse_args()

    if args.xip:
        return run_xip(args)

    build = TESTS_DIR / args.case / "build"
    target_elf = (build / "target" / "zephyr" / "zephyr.elf").as_posix()
    controller_bin = (build / "controller" / "zephyr" / "zephyr.bin").as_posix()
    for f in (target_elf, controller_bin):
        if not Path(f).exists():
            sys.exit(f"ERROR: not built: {f}")

    explicit = [p for p in (args.ctrl_uart, args.tgt_uart) if p]
    dbg_run_preflight()   # F1：DS 不健康先自愈，起不来则明确中止（不退化 error 138）
    names = discover_ports(explicit)
    ports = {name: {"lines": [], "stop": threading.Event()} for name in names}

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
    sers, threads = start_capture(names, ports)

    print(f"[dbg] flashing again (capture pass), capturing {args.timeout}s ...", flush=True)
    proc = subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    # ★ HW4 fix: gdb stdout 必须在窗口内**持续排空**。Windows 匿名管道缓冲
    # 写满后 gdb 阻塞在写 stdout 上 → DebugServer 会话停摆 → 目标被中途 halt，
    # 长用例（i2c_wave ~10s）的尾部（PASS / PROJECT EXECUTION）永远采不到。
    # 实测：不排空时 gdb 进程挂死、串口 0 字节（tmp/hw4/cap_ts2.txt）。
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
    if "Loading section" in out or "Restoring binary" in out:
        print("[dbg] flash OK", flush=True)
    else:
        print("[dbg] !!! FLASH DID NOT HAPPEN — gdb output: !!!", flush=True)
        print(out[-1500:], flush=True)
        sys.exit(2)
    time.sleep(0.3)

    finish_summary(ports, sers, threads, args.ctrl_uart, args.tgt_uart, args.raw)


if __name__ == "__main__":
    main()
