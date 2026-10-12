#!/usr/bin/env python3
"""One-shot debug runner (single-core): GDB-load cpu1 ELF, capture UART, parse ztest output.

单核工程版：从 otbn_selftest 的 dbg_run.py 移植（同结构，前缀 OTBN_KEYGEN）。
单遍烧录（reset + load 后直接采首轮）；免烧重跑 --no-load；
写包提速 --wpacket-size（默认 16384）。

Usage:
    python scripts/dbg_run.py [--case case_default] [--timeout 120] [--raw]
    python scripts/dbg_run.py --case case_default --tgt-uart COM16
    python scripts/dbg_run.py --no-load --timeout 300   # 免烧重跑（镜像已在 RAM）
    python scripts/dbg_run.py --wpacket-size 0          # 关闭写包提速（默认 16384）

UART role discovery (dynamic):
    - By default all non-CKLink COM ports are opened; after the flash/reset
      the firmware prints its banner on the matching UART:
          "CPU1 OTBN_KEYGEN" / "otbn_keygen"  -> target (uart1)
      so the port role is resolved from actual output, no hardcoding.
    - Explicit override: --tgt-uart COM16.

Assumes firmware is already built under tests/<case>/build (build_subdir="").
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

# tests/scripts/dbg_run.py -> parents[1] = otbn_keygen_test/tests
TESTS_DIR = Path(__file__).resolve().parents[1]
GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
# 用户需求#8：单核一律 1os 构建，cpu1 入口 = 0x10000000（readelf 实测）
TARGET_ROM_START = os.environ.get("TARGET_ROM_START", "0x10000000")

# common/ 在 810_driver_test 级
sys.path.insert(0, str(TESTS_DIR.parents[2] / "common"))
from ztest_report import parse_ztest   # noqa: E402
from acquire import dbg_run_preflight  # noqa: E402

TGT_BANNERS = ("CPU1 OTBN_KEYGEN", "otbn_keygen", "OTBN keygen")

# Zephyr console shell 重绘提示符会向串口发 ANSI 转义，ztest 行首可能混入，先剥离再匹配。
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
# ztest 原生逐项输出的完成/活动标记（parse_ztest 数据源）
ZT_BOOT_RE = re.compile(r"ZT_BOOT TGT")
EXEC_RE = re.compile(r"PROJECT EXECUTION (SUCCESSFUL|FAILED)")
ZTEST_ACTIVITY_RE = re.compile(r"(ZT_BOOT|START - | PASS - | FAIL - | SKIP - "
                               r"|PROJECT EXECUTION|Assertion failed)")


def _own_port(p):
    """串扰免疫：已见本固件 ZT_BOOT 且带本固件 banner 的端口才算我方。"""
    pl = p["lines"]
    return (any(ZT_BOOT_RE.search(l) for l in pl)
            and any(b in l for b in TGT_BANNERS for l in pl))


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
    ap.add_argument("--timeout", type=float, default=900,
                    help="采集窗口硬上限（秒）。套件 DONE 后 5s 静默即提前结束，"
                         "正常不会跑满；RSA-4096 keygen 慢，勿小于 600")
    ap.add_argument("--raw", action="store_true", help="dump all captured lines")
    ap.add_argument("--tgt-uart", default=None, help="target UART, e.g. COM16")
    ap.add_argument("--no-load", action="store_true",
                    help="跳过全部 load：目标 RAM 必须已持有本镜像")
    ap.add_argument("--wpacket-size", type=int, default=None,
                    help="gdb memory-write-packet-size（字节）。默认 16384")
    ap.add_argument("--stall", type=float, default=300,
                    help="无 token 卡死提前收尾阈值（秒）；keygen 两项之间可能长期无输出")
    args = ap.parse_args()

    build = TESTS_DIR / args.case / "build"
    target_elf = (build / "zephyr" / "zephyr.elf").as_posix()
    if not Path(target_elf).exists():
        sys.exit(f"ERROR: not built: {target_elf}")

    dbg_run_preflight()   # F1：DS 不健康先自愈，起不来则明确中止
    names = discover_ports([p for p in (args.tgt_uart,) if p])
    ports = {name: {"lines": [], "stop": threading.Event()} for name in names}

    def start_capture():
        """Open ports and start reader threads (single-pass capture)."""
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
    ]

    def gdb_send(proc, *cmds):
        for c in cmds:
            proc.stdin.write(c + "\n")
        proc.stdin.flush()

    gdb_out = []
    gdb_out_stop = threading.Event()

    def gdb_reader():
        while not gdb_out_stop.is_set():
            line = proc.stdout.readline()
            if not line:
                break
            gdb_out.append(line.rstrip("\n"))

    def wait_load_done(timeout_s=420):
        """等 gdb 打出 'Transfer rate'（load 完成标志）。True=完成。"""
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if any("Transfer rate" in l for l in gdb_out):
                return True
            if any(("Remote failure" in l) or ("E22" in l) for l in gdb_out):
                return False   # 协议错误（如写包过大被 stub 拒绝），立即失败
            time.sleep(0.5)
        return False

    # 单遍烧录 + 首轮采集（单会话）。
    proc = subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    gt = threading.Thread(target=gdb_reader, daemon=True)
    gt.start()

    wpacket = args.wpacket_size if args.wpacket_size is not None else 16384
    if wpacket > 0:
        gdb_send(proc, f"set memory-write-packet-size {wpacket}",
                 f"set remotewritesize {wpacket}")

    if args.no_load:
        print(f"[dbg] no-load mode: re-run RAM image (0 flash, "
              f"capture {args.timeout}s) ...", flush=True)
        gdb_send(proc, "info registers pc", f"x/4wx {TARGET_ROM_START}")
        time.sleep(2.0)
        img_ok = any(
            re.search(rf"{TARGET_ROM_START}\s*<[^>]*>:\s*0x(?!0+[ \t])", l)
            for l in gdb_out)
        if not img_ok:
            print("[dbg] !!! no-load sanity failed — RAM image missing? "
                  "(board powered off since last load). gdb tail:", flush=True)
            print("\n".join(gdb_out[-10:]), flush=True)
            proc.kill()
            sys.exit(2)
        sers, threads = start_capture()
        gdb_send(proc, "set {int}0x4002200c = 2",
                 f"set $pc={TARGET_ROM_START}", "continue")
    else:
        print(f"[dbg] flash {args.case} (single pass: reset + load + "
              f"capture {args.timeout}s) ...", flush=True)
        gdb_send(proc, "reset", "monitor flash erase_address 0x10000000 0x10000", "load")
        if not wait_load_done():
            print("[dbg] !!! load timeout/error, gdb tail:", flush=True)
            print("\n".join(gdb_out[-15:]), flush=True)
            proc.kill()
            sys.exit(2)
        for l in gdb_out:
            if "Transfer rate" in l:
                print(f"[dbg] load done: {l}", flush=True)
        sers, threads = start_capture()
        gdb_send(proc, "set {int}0x4002200c = 2",
                 f"set $pc={TARGET_ROM_START}", "continue")

    # 完成标记提前收尾（静默判据）：PROJECT EXECUTION 出现后再静默 5s 即结束；
    # 无完成标记且连续 args.stall 无 ztest 逐项输出 → 判定套件卡死；90s 无 ZT_BOOT = 环境问题。
    deadline = time.time() + args.timeout
    start_at = time.time()
    done_at = None
    last_byte_at = time.time()
    seen_lines = 0
    own_seen = 0
    last_progress_log = time.time()
    while time.time() < deadline:
        all_lines = [l for p in ports.values() for l in p["lines"]]
        token_lines = [l for l in all_lines if ZTEST_ACTIVITY_RE.search(l)]
        n = len(token_lines)
        if n != seen_lines:
            for l in token_lines[seen_lines:]:
                print(f"  [{int(time.time()-start_at):4d}s] {l}", flush=True)
            seen_lines = n
        own_lines = [l for p in ports.values() if _own_port(p) for l in p["lines"]]
        own_tokens = [l for l in own_lines if ZTEST_ACTIVITY_RE.search(l)]
        if len(own_tokens) != own_seen:
            own_seen = len(own_tokens)
            last_byte_at = time.time()
        if time.time() - last_progress_log >= 30:
            tail_ln = all_lines[-1][:100] if all_lines else "(none)"
            print(f"[dbg] t={int(time.time()-start_at)}s tokens={seen_lines} "
                  f"lines={len(all_lines)} idle={int(time.time()-last_byte_at)}s "
                  f"last={tail_ln!r}", flush=True)
            last_progress_log = time.time()
        txt = "\n".join(own_lines)
        if EXEC_RE.search(txt):
            if done_at is None:
                done_at = time.time()
            elif time.time() - done_at >= 5:
                print("[dbg] PROJECT EXECUTION + 5s idle -> early stop "
                      f"(elapsed {int(time.time() - start_at)}s)", flush=True)
                break
        elif own_seen == 0 and time.time() - start_at >= 90:
            print("[dbg] ERROR: 90s no ZT_BOOT — 测试环境问题"
                  "（烧录链路/连接残留），非测试失败", flush=True)
            break
        elif done_at is None and own_seen > 0 and time.time() - last_byte_at >= args.stall:
            print(f"[dbg] WARNING: {int(args.stall)}s no ztest output "
                  f"(t={int(time.time()-start_at)}s), suite stalled? -> early stop", flush=True)
            break
        time.sleep(0.5)
    else:
        print(f"[dbg] capture window expired ({args.timeout}s, no PROJECT EXECUTION)", flush=True)
    try:
        proc.stdin.write("\x03")   # 干净退出，避免连接残留
        proc.stdin.flush()
        time.sleep(1.0)
        proc.stdin.write("detach\nquit\n")
        proc.stdin.flush()
        proc.wait(timeout=10)
    except Exception:
        proc.kill()
        proc.wait()
    gdb_out_stop.set()
    gt.join(timeout=2)
    out = "\n".join(gdb_out)
    with open(TESTS_DIR / "dbg_gdb_last.log", "w", encoding="utf-8") as f:
        f.write(out)
    if not args.no_load:
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
            print(f"  {tname:<32} {t['result']}{extra}")
        for tname, kv in sorted(r["infos"].items()):
            print(f"  {tname:<32} INFO " + ", ".join(f"{k}={v}" for k, v in kv.items()))
        for ln in p["lines"]:
            if any(k in ln for k in ("Assertion failed", "ret=", "mbedtls",
                                     "not ready")):
                print("  * " + ln)

    if args.raw:
        for name, p in ports.items():
            print(f"\n===== RAW {name} ({role[name]}) =====")
            for ln in p["lines"]:
                print(ln)


if __name__ == "__main__":
    main()
