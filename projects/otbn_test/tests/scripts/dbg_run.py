#!/usr/bin/env python3
"""One-shot debug runner (single-core): GDB-load cpu1 ELF, capture UART, parse ztest output.

单核工程版：从 i3c_dualcore_test 样板 dbg_run.py 改造，去掉 controller 核
（无 restore bin），只加载 cpu1 ELF；角色只有 TGT。

Usage:
    python scripts/dbg_run.py [--case case_default] [--timeout 15] [--raw]
    python scripts/dbg_run.py --case case_default --tgt-uart COM16
    python scripts/dbg_run.py --no-load --timeout 90   # 免烧重跑（镜像已在 RAM）
    python scripts/dbg_run.py --wpacket-size 0         # 关闭写包提速（默认 16384）

UART role discovery (dynamic):
    - By default all non-CKLink COM ports are opened; after the flash/reset
      the firmware prints its banner on the matching UART:
          "CPU1 OTBN" / "otbn_test"  -> target (uart1)
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

# tests/scripts/dbg_run.py -> parents[1] = otbn_test/tests
TESTS_DIR = Path(__file__).resolve().parents[1]
GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
# 用户需求#8：单核一律 1os 构建，cpu1 入口 = 0x10000000（readelf 实测）；
# 2os 构建入口 0x100c0000，需显式 TARGET_ROM_START=0x100c0000 环境变量覆盖
TARGET_ROM_START = os.environ.get("TARGET_ROM_START", "0x10000000")

# common/ 在 810_driver_test 级（trng/rtc dbg_run.py 同款，与 run 脚本的
# REPORTS_ROOT.parent/"common" 形式等价）
sys.path.insert(0, str(TESTS_DIR.parents[2] / "common"))
from ztest_report import parse_ztest   # noqa: E402

TGT_BANNERS = ("CPU1 OTBN", "otbn_test", "OTBN test")

# Zephyr console shell 重绘提示符会向串口发 ANSI 转义，ztest 行首可能混入
# （如 \x1b[1;32muart:~$ \x1b[8D\x1b[J），先剥离再匹配。
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
# ztest 原生逐项输出的完成/活动标记（parse_ztest 数据源）
ZT_BOOT_RE = re.compile(r"ZT_BOOT TGT")
EXEC_RE = re.compile(r"PROJECT EXECUTION (SUCCESSFUL|FAILED)")
ZTEST_ACTIVITY_RE = re.compile(r"(ZT_BOOT|START - | PASS - | FAIL - | SKIP - "
                               r"|PROJECT EXECUTION|Assertion failed)")


def _own_port(p):
    """§10.30 串扰免疫：已见本固件 ZT_BOOT 且带本固件 banner 的端口才算我方
    （他方固件/残留镜像的 PROJECT EXECUTION 不参与 completion/早停判定）。"""
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
    ap.add_argument("--timeout", type=float, default=600,
                    help="采集窗口硬上限（秒）。套件 DONE 后 5s 静默即提前结束，"
                         "正常不会跑满；仅卡死时才用满")
    ap.add_argument("--raw", action="store_true", help="dump all captured lines")
    ap.add_argument("--tgt-uart", default=None, help="target UART, e.g. COM16")
    ap.add_argument("--no-load", action="store_true",
                    help="跳过全部 load：目标 RAM 必须已持有本镜像（此前 load 过、"
                         "未断电）。attach 后 set $pc=入口 重跑套件，同镜像迭代"
                         "0 烧录（实测一轮 57s，与烧录轮结果逐项一致）")
    ap.add_argument("--wpacket-size", type=int, default=None,
                    help="gdb memory-write-packet-size（字节）。默认 16384："
                         "实测 CKLink load 1KB/s → 41KB/s（41x）；0 = 关闭")
    args = ap.parse_args()

    build = TESTS_DIR / args.case / "build"
    target_elf = (build / "zephyr" / "zephyr.elf").as_posix()
    if not Path(target_elf).exists():
        sys.exit(f"ERROR: not built: {target_elf}")

    names = discover_ports([p for p in (args.tgt_uart,) if p])
    ports = {name: {"lines": [], "stop": threading.Event()} for name in names}

    def start_capture():
        """Open ports and start reader threads (single-pass capture: 串口在
        continue 前打开，从 boot banner 开始整轮采集)。"""
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

    # gdb stdout 实时读取（load 完成标志 = "Transfer rate" 行）。
    # ★ 必须等 load 真正结束再 continue：本镜像 ~192KB，CKLink 实测
    # ~1KB/s → load 要 ~190s！固定 sleep 会在 load 中途 interrupt，
    # 打断烧写（run16 实测：sleep(20) 时 load 才走到 10%）。
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

    # ★ 单遍烧录 + 首轮采集（单会话）。两遍烧录法（prime+capture）的分工是
    # pass-1 用 reset 冲刷 I-cache、pass-2 重新 load 恢复干净 .data/.bss；
    # 但单会话里 interrupt 后的第二个 load 实测两度静默挂死（run17、wpacket
    # 实验），且 capture 轮不能发 reset（中途 reset 落 ROM，PC 0x0100_0000）。
    # 改为 reset 后单次 load 直接采首轮：reset 已冲刷 I-cache（§10.4 的
    # "慢一拍"场景被 reset 覆盖），load 后状态天然干净，无需第二遍。
    proc = subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    gt = threading.Thread(target=gdb_reader, daemon=True)
    gt.start()

    # 写包参数（默认开）：实测 CKLink load 1KB/s → 41KB/s（41x）。
    # stub 不接受时回 E22，wait_load_done 立即失败。
    wpacket = args.wpacket_size if args.wpacket_size is not None else 16384
    if wpacket > 0:
        gdb_send(proc, f"set memory-write-packet-size {wpacket}",
                 f"set remotewritesize {wpacket}")

    if args.no_load:
        # ★ 免烧重跑：镜像已在 RAM（此前 load 过，板未断电）。attach 后
        # set $pc=入口 直接重跑（复位向量重新初始化 .bss/.data，等效软复位，
        # 实测 rerun1：57s 完整一轮，结果与烧录轮逐项一致）。省去 2×~190s
        # 的 CKLink load（1KB/s），同镜像迭代验证成本 = 纯套件时间。
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
        # 用户需求#7：按住 cpu2（APP_CPU_SRST=2，W1S/W1C 保持复位：写 1=释放、
        # 写 2=保持、写 0 无效果）再放行 cpu1；跑完不释放（双核流程自会写 1 释放）
        gdb_send(proc, "set {int}0x4002200c = 2",
                 f"set $pc={TARGET_ROM_START}", "continue")
    else:
        print(f"[dbg] flash {args.case} (single pass: reset + load + "
              f"capture {args.timeout}s) ...", flush=True)
        gdb_send(proc, "reset", "monitor flash erase_address 0x10000000 0x10000", "load")
        if not wait_load_done():
            print("[dbg] !!! load timeout/error, gdb tail:", flush=True)
            print("\n".join(gdb_out[-15:]), flush=True)
            proc.kill()   # 孤儿 gdb 会占住 DebugServer 连接，必须带走
            sys.exit(2)
        for l in gdb_out:
            if "Transfer rate" in l:
                print(f"[dbg] load done: {l}", flush=True)
        sers, threads = start_capture()
        # 用户需求#7：按住 cpu2（APP_CPU_SRST=2，W1S/W1C 保持复位：写 1=释放、
        # 写 2=保持、写 0 无效果）再放行 cpu1；跑完不释放（双核流程自会写 1 释放）
        gdb_send(proc, "set {int}0x4002200c = 2",
                 f"set $pc={TARGET_ROM_START}", "continue")

    # 完成标记提前收尾（静默判据）：PROJECT EXECUTION 出现后再静默 5s 即结束；
    # 无完成标记且连续 60s 无 ztest 逐项输出 → 判定套件卡死，提前结束（硬上限
    # args.timeout 兜底）；90s 无 ZT_BOOT = 环境问题。
    deadline = time.time() + args.timeout
    start_at = time.time()
    done_at = None
    last_byte_at = time.time()
    seen_lines = 0
    own_seen = 0
    last_progress_log = time.time()
    while time.time() < deadline:
        # 新 ztest 逐项输出行实时回显（采集不再盲等，可 tail -f 观察进度）
        all_lines = [l for p in ports.values() for l in p["lines"]]
        token_lines = [l for l in all_lines if ZTEST_ACTIVITY_RE.search(l)]
        n = len(token_lines)
        if n != seen_lines:
            for l in token_lines[seen_lines:]:
                print(f"  [{int(time.time()-start_at):4d}s] {l}", flush=True)
            seen_lines = n
        # §10.30 串扰防线：完成/卡死/BOOT 判定只认我方端口（ZT_BOOT+banner），
        # 外来端口只回显不判定；我方 token 变化才刷新 idle 计时。
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
        elif done_at is None and own_seen > 0 and time.time() - last_byte_at >= 60:
            print(f"[dbg] WARNING: 60s no ztest output (t={int(time.time()-start_at)}s), "
                  "suite stalled? -> early stop", flush=True)
            break
        time.sleep(0.5)
    else:
        print(f"[dbg] capture window expired ({args.timeout}s, no PROJECT EXECUTION)", flush=True)
    try:
        proc.stdin.write("\x03")   # 干净退出，避免连接残留（§10.1）
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
    # gdb 完整输出存文件，失败时打印尾部（stdout 由 gdb_reader 线程收集）
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
            print(f"  {tname:<20} {t['result']}{extra}")
        for tname, kv in sorted(r["infos"].items()):
            print(f"  {tname:<20} INFO " + ", ".join(f"{k}={v}" for k, v in kv.items()))
        # key debug lines
        for ln in p["lines"]:
            if any(k in ln for k in ("Assertion failed", "keygen_ms",
                                     "progress_rng_calls", "nbits=", "not ready")):
                print("  * " + ln)

    if args.raw:
        for name, p in ports.items():
            print(f"\n===== RAW {name} ({role[name]}) =====")
            for ln in p["lines"]:
                print(ln)


if __name__ == "__main__":
    main()
