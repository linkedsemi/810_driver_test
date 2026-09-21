#!/usr/bin/env python3
"""app_iwdg_cpu2_test 单次调试入口（双核）：加载两核镜像，采集双串口，解析 ztest 原生输出。

双核工程：cpu1 = target(常驻监督, uart1)，cpu2 = controller(被测核, uart5)。
**内联 gdb 加载**（★ 2026-09-19 起；**不再委托 `<case>/test.sh -r`**，构建目录可隔离）：
一次 gdb 会话完成 `file <cpu1 ELF>` + `load` + `restore <cpu2 BIN> binary 0x10000000`
（cpu1 入口 0x100c0000），两遍烧录法（prime 冲刷 I-cache、capture 为准）。

⚠ 关于 cpu2 hold（0x4002200c / SYSC_SEC_CPU.APP_CPU_SRST）：
    单核路径 load_and_run_single 会写 `set {int}0x4002200c = 2` 按住 cpu2 复位，
    防他方 RAM 残留镜像串扰采集。**双核路径绝对不加**——本工程的 cpu2 就是被
    测核，按住它整个 case 都跑不起来（见 scripts/test_common.sh 的注释）。
    故本脚本与 run_test.py 只在注释里出现该地址，不做写入。

Usage:
    python scripts/dbg_run.py --case case_non_destructive [--timeout 30] [--raw]
    python scripts/dbg_run.py --case case_quick_enable --ctrl-uart COM15 --tgt-uart COM16
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

# tests/scripts/dbg_run.py -> parents[1] = app_iwdg_cpu2_test/tests
TESTS_DIR = Path(__file__).resolve().parents[1]
SAMPLE_ROOT = TESTS_DIR.parent

sys.path.insert(0, str(SAMPLE_ROOT.parents[1] / "common"))
from ztest_report import parse_ztest  # noqa: E402
from acquire import dbg_run_preflight  # noqa: E402

# 内联 gdb 加载（★ 2026-09-19 对齐模板；qsh-test-gen §1.5「dbg_run 禁委托 test.sh -r」）
GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
TARGET_ROM_START = "0x100c0000"        # 2os cpu1 入口（target ELF）
CONTROLLER_LOAD_ADDR = "0x10000000"    # 2os cpu2 加载址（controller BIN，restore 用）

# 角色 banner 取自本工程固件实际打印：
#   cpu1: LOG_INF("[CPU1 APP_IWDG] ...")（app_iwdg_cpu1_main）
#   cpu2: LOG_INF("[CPU2 APP_IWDG] ...")（app_iwdg_cpu2_main）
#         case_reset 为自定义 main：printk("*** app_iwdg (cpu2) HART reset test ***")
TGT_BANNERS = ("CPU1 APP_IWDG",)
CTRL_BANNERS = ("CPU2 APP_IWDG", "app_iwdg (cpu2)")

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


class Capture:
    def __init__(self, port):
        self.port = port
        self.lines = []
        self._stop = threading.Event()
        self._thread = None
        self._ser = None
        self.error = None

    def start(self):
        try:
            self._ser = serial.Serial(self.port, 115200, timeout=0.1)
        except Exception as exc:
            self.error = f"failed to open {self.port}: {exc}"
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
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
                self.lines.append(ANSI_RE.sub("", line.decode("utf-8", "replace").rstrip("\r")))

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2.0)
        if self._ser and self._ser.is_open:
            self._ser.close()


def discover(explicit, others):
    """返回候选端口：显式给定的优先，其余枚举非 CKLink 端口。"""
    if explicit:
        return [explicit]
    ports = []
    for p in list_ports.comports():
        hwid = (p.hwid or "").lower()
        desc = (p.description or "").lower()
        if "32bf" in hwid or "cklink" in desc:
            continue
        ports.append(p.device)
    return ports


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", default="case_non_destructive")
    ap.add_argument("--timeout", type=float, default=30)
    ap.add_argument("--raw", action="store_true")
    ap.add_argument("--ctrl-uart", default=None, help="cpu2 控制台（uart5）")
    ap.add_argument("--tgt-uart", default=None, help="cpu1 控制台（uart1）")
    args = ap.parse_args()

    case_dir = TESTS_DIR / args.case
    build = case_dir / "build"
    target_elf = build / "target" / "zephyr" / "zephyr.elf"
    controller_bin = build / "controller" / "zephyr" / "zephyr.bin"
    if not target_elf.exists() or not controller_bin.exists():
        sys.exit(f"ERROR: not built (run {case_dir}/test.sh first): {build}")

    ctrl_uart = args.ctrl_uart or os.environ.get("I3C_CTRL_UART")
    tgt_uart = args.tgt_uart or os.environ.get("I3C_TGT_UART")
    dbg_run_preflight()   # F1：DS 不健康先自愈，起不来则明确中止（不退化 error 138）
    ctrl_ports = discover(ctrl_uart, None)
    tgt_ports = discover(tgt_uart, None)

    caps = {}
    for name in dict.fromkeys(ctrl_ports + tgt_ports):
        c = Capture(name)
        c.start()
        if c.error:
            print(f"[dbg] WARNING: {c.error}", flush=True)
            continue
        caps[name] = c

    if not caps:
        sys.exit("ERROR: no UART could be opened")

    # ---- 内联 gdb 加载（★ 2026-09-19 对齐模板）----
    # 原先委托 `<case>/test.sh -r`，会把 TEST_BUILD_DIR 钉在工程内、TEST_ROOT 从
    # test.sh 自身位置反推 ⇒ **构建目录无法隔离**（想构建到工程外就得把源目录一起
    # 镜像，否则 `bash: test.sh: No such file` 秒退）。qsh-test-gen §1.5 已把
    # 「禁委托」写进模板；本工程是唯一残留实例，此处改为内联。
    # ★ 双核契约（见文件头注释）：**不写** `APP_CPU_SRST`（cpu2 就是被测核，
    #   按住它整个 case 起不来）。单核路径才写 2，本脚本不做该写入。
    # 地址已经 hw3 实测确认：cpu1 0x100c0000 / cpu2 0x10000000。
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

    def _load_once(run_secs, label):
        """一遍烧录 + continue 跑 run_secs 秒（超时后 interrupt/detach 干净退出）。"""
        print(f"[dbg] gdb loading both cores ({label} pass), {run_secs}s ...", flush=True)
        proc = subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, encoding="utf-8", errors="replace")
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
        if "Loading section" not in out:
            print(f"[dbg] WARNING: {label} pass 未见 'Loading section'（load 可能失败）：\n"
                  + out[-400:], file=sys.stderr, flush=True)
        return out

    # 两遍烧录法（§10.4）：prime 轮冲刷 I-cache 旧镜像（输出丢弃），capture 轮为准
    _load_once(args.timeout, "prime")
    for c in caps.values():
        c.lines.clear()
    _load_once(args.timeout, "capture")
    time.sleep(0.5)
    for c in caps.values():
        c.stop()

    print("\n===== UART ROLE / RESULT =====", flush=True)
    for name, c in caps.items():
        text = "\n".join(c.lines)
        role = "CTRL" if any(b in text for b in CTRL_BANNERS) else (
            "TGT" if any(b in text for b in TGT_BANNERS) else "?")
        r = parse_ztest(c.lines, ctrl_banners=CTRL_BANNERS, tgt_banners=TGT_BANNERS)
        print(f"--- {name} ({role}): {len(c.lines)} lines "
              f"boot={r['boot_seen']} exec={r['execution']} ---")
        for tname, t in sorted(r["tests"].items()):
            extra = f"  reason: {t['reason']}" if t["reason"] else ""
            print(f"  {tname:<52} {t['result']}{extra}")
        for tname, kv in sorted(r["infos"].items()):
            print(f"  {tname:<52} INFO " + ", ".join(f"{k}={v}" for k, v in kv.items()))
        if role == "?":
            print("  (WARNING: 未识别角色——检查该口是否为本工程串口/banner 常量)")

    if args.raw:
        for name, c in caps.items():
            print(f"\n===== RAW {name} =====")
            for ln in c.lines:
                print(ln)


if __name__ == "__main__":
    main()
