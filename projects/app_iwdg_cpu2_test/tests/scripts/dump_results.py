#!/usr/bin/env python3
"""app_iwdg_cpu2_test - 从 GDB 读内存取测试结果（串口不可用时的兜底通道）。

双核工程：cpu1(TGT, 常驻监督) 与 cpu2(CTRL, 被测核) **各自**链接一份
common/test_results.{h,c}，符号同名地址不同，故按 elf 分别 dump。

固件把每项测试记账到公共模块的全局数组（int8_t test_results[]：
-1 未跑 / 0 PASS / 1 FAIL；char test_reasons[][]）。本脚本连 DebugServer
直接读内存，不依赖串口（SPEC_REFERENCE §6.2.2 / §6.4.3）。

⚠ TEST_NAMES 必须按 **ztest_unit_test 段链接序**（SORT_BY_NAME，§10.27）
与 glue 记账 idx 对齐，且**逐 case 不同**（各 case 编入的套件不同）。
本文件不提供默认值：未登记的 case 直接报错退出，避免"读本工程内存却按
别的名字映射"造成假 PASS（KNOWN_ISSUES #59）。
回填方法（构建后）：
    riscv64-unknown-elf-nm --numeric-sort <elf> | grep _ztest_unit_test

用法:
    python dump_results.py --case case_non_destructive            # 只读当前内存
    python dump_results.py --case case_non_destructive --json     # 回归兜底合并用
    python dump_results.py --case case_non_destructive --run 30   # 先两遍烧录跑 30s 再读
    python dump_results.py --case case_non_destructive --raw
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

# ---- F1/F2/F3 判定框架收敛点：哨兵状态词表与分类见 common/judge.py ----
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "common"))
from judge import (MEM_INVALID, MEM_NO_SENTINEL, MEM_OK,  # noqa: E402
                   MEM_UNAVAILABLE, sentinel_state)

# tests/scripts/dump_results.py -> parents[1] = app_iwdg_cpu2_test/tests
TESTS_DIR = Path(__file__).resolve().parents[1]
SAMPLE_ROOT = TESTS_DIR.parent

GDB = "riscv64-unknown-elf-gdb"
NM = "riscv64-unknown-elf-nm"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"

# 双核 2os：cpu1 入口 0x100c0000（SPEC_REFERENCE §6.1.10）；
# 2os 时须显式覆盖（dump_results 默认值是 1os 语义 0x10000000）
TARGET_ROM_START = int(os.environ.get("TARGET_ROM_START", "0x100c0000"), 0)

# 公共模块统一符号名（common/test_results.h）
RESULT_SYM = "test_results"
REASON_SYM = "test_reasons"
# 有效性哨兵（test_results.h，2026-09-18 加）：tr_init() 填完槽位后置 TR_VALID_MAGIC。
# 未置位 = 固件没跑到 tr_init（挂在内核初始化 / POST_KERNEL / 早期崩溃）
# -> test_results[] 停在 .bss 零值，而零值语义就是 TR_PASS -> 必须判"结果无效"。
# 旧 ELF 里没有这个符号 -> nm 查不到 -> 同样判无效（优雅降级，不崩）。
VALID_SYM = "test_results_valid"
TR_VALID_MAGIC = 0x5A

# ⚠ 未实机校准：以下顺序取自构建后 nm 的链接序，实机回归前再核一次。
TEST_NAMES_BY_CASE = {
    # case_non_destructive：cpu2 侧 app_iwdg 套件（16 项）
    # 链接序取自 `nm --numeric-sort .../build/controller/zephyr/zephyr.elf |
    # grep _ztest_unit_test__app_iwdg__`（2026-09-17 首次构建实测）
    "case_non_destructive": [
        "test_feed_within_window_no_reset",
        "test_install_accepts_reset_cpu_core_flag",
        "test_install_invalid_window_max_zero",
        "test_install_invalid_window_min_nonzero",
        "test_install_rejects_reset_soc_flag",
        "test_install_rejects_unmatched_flags",
        "test_install_with_callback_accepted",
        "test_install_without_callback_accepted",
        "test_load_ticks_1000ms",
        "test_load_ticks_65535ms",
        "test_load_ticks_max_no_wrap",
        "test_load_ticks_min_timeout",
        "test_setup_enables_ctrl_rst_en",
        "test_setup_pause_in_sleep_enotsup",
        "test_setup_twice_ebusy",
        "test_setup_without_timeout_enotsup",
    ],
    # case_quick_enable：cpu2 侧 app_iwdg_quick 套件（1 项）
    "case_quick_enable": [
        "test_quick_enable_boot_state_feed_disable",
    ],
    # case_quick_invalid：cpu2 侧 app_iwdg_quick_invalid 套件（1 项）
    "case_quick_invalid": [
        "test_quick_timeout_zero_leaves_device_unready",
    ],
}


def log(args, msg):
    if args.json:
        print(msg, file=sys.stderr, flush=True)
    else:
        print(msg, flush=True)


def nm_addr(elf, sym):
    out = subprocess.run([NM, elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == sym:
            return parts[0]
    return None


def gdb_session(extra_ex):
    cmd = [
        GDB, "-q", "-batch",
        "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
        "-ex", "set confirm off",
        "-ex", "set pagination off",
        "-ex", "interrupt",
    ] + extra_ex + ["-ex", "detach", "-ex", "quit"]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        sys.exit("ERROR: gdb dump timed out (DebugServer 残留? 先做干净连接或重启)")
    return proc.stdout


def parse_x_bytes(text, count):
    vals = []
    for line in text.splitlines():
        if ":" not in line or line.strip().startswith("$"):
            continue
        body = line.split(":", 1)[1]
        vals.extend(int(v) for v in re.findall(r"-?\d+", body))
        if len(vals) >= count:
            break
    return vals[:count]


def dump_core(elf, names, args):
    """读一个核的结果数组，返回 (results, reasons, mem_state, note)。

    mem_state != MEM_OK = 该核内存结果不可信，调用方必须清空该核结果，禁止当成
    全 PASS。两态见 common/judge.py：
      * `no_sentinel_symbol` = ELF 无该符号 = **旧构建，需重新编译**
      * `invalid`            = 符号在但未置位 = 真的没跑到 tr_init
    """
    res = nm_addr(elf, RESULT_SYM)
    reason = nm_addr(elf, REASON_SYM)
    valid = nm_addr(elf, VALID_SYM)   # None = 旧 ELF 无哨兵符号
    if res is None or reason is None:
        log(args, f"[dump] WARNING: symbol not found in {elf}: "
                  f"{RESULT_SYM}={res} {REASON_SYM}={reason}")
        return {}, {}, MEM_UNAVAILABLE, "结果/原因符号缺失，内存兜底不可用。"

    count = len(names) if names else args.count
    _ex = []
    if valid is not None:
        # 哨兵与结果数组同一 gdb 会话读（少一次连接 = 少一次 DebugServer 残留）
        _ex += ["-ex", f"x/1db 0x{valid}"]
    _ex += ["-ex", f"x/{count}db 0x{res}"]
    out = gdb_session(_ex)
    if args.raw:
        print(f"\n===== GDB RAW {elf} =====")
        print(out)

    raw_vals = parse_x_bytes(out, count + (0 if valid is None else 1))
    valid_ok = False
    if valid is not None:
        valid_ok = bool(raw_vals) and raw_vals[0] == TR_VALID_MAGIC
        vals = raw_vals[1:]
    else:
        vals = raw_vals
    _state, _note = sentinel_state(valid is not None, valid_ok)
    if _state != MEM_OK:
        # 哨兵未置位：槽位是 .bss 零值（= TR_PASS），读出来就是假 PASS
        log(args, f"[dump] {elf}: {_note}")
        return {}, {}, _state, _note
    results = {}
    for i, v in enumerate(vals):
        name = names[i] if i < len(names) else f"test_{i}"
        results[name] = "NOT_RUN" if v < 0 else ("PASS" if v == 0 else "FAIL")

    fail_names = [n for n, s in results.items() if s == "FAIL"]
    fail_ex = []
    for i, v in enumerate(vals):
        if v == 1:
            fail_ex += ["-ex", f"x/s 0x{int(reason, 16) + i * args.reason_stride:X}"]

    reasons = {}
    if fail_ex:
        rtext = gdb_session(fail_ex)
        if args.raw:
            print(f"\n===== FAIL REASONS RAW {elf} =====")
            print(rtext)
        matches = re.findall(r'0x[0-9a-f]+:\s+"([^"]*)"', rtext)
        for name, r in zip(fail_names, matches):
            reasons[name] = r

    if len(vals) < count:
        log(args, f"[dump] WARNING: {elf} 只解析到 {len(vals)}/{count} 个槽，"
                  f"（TEST_NAMES 未登记或未同步？）")
    return results, reasons, MEM_OK, ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", default="case_non_destructive")
    ap.add_argument("--run", type=float, default=None, help="先烧录并运行 N 秒再 dump")
    ap.add_argument("--count", type=int, default=64)
    ap.add_argument("--reason-stride", type=int, default=96)
    ap.add_argument("--raw", action="store_true")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    if args.case == "case_reset":
        sys.exit("ERROR: case_reset 是破坏性自定义 main（无 ztest 记账），"
                 "不适用内存兜底；请用 case_reset/run_reset.sh 并按串口标记判定。")

    if args.case not in TEST_NAMES_BY_CASE:
        sys.exit(f"ERROR: case '{args.case}' 未登记 TEST_NAMES；"
                 f"请先按 nm 链接序回填（见本文件头注释），否则会假 PASS。")
    names = TEST_NAMES_BY_CASE[args.case]

    for tool in (GDB, NM):
        if shutil.which(tool) is None:
            sys.exit(f"ERROR: {tool} not in PATH (Xuantie toolchain?)")

    build = TESTS_DIR / args.case / "build"
    tgt_elf = (build / "target" / "zephyr" / "zephyr.elf").as_posix()
    ctrl_elf = (build / "controller" / "zephyr" / "zephyr.elf").as_posix()
    for elf in (tgt_elf, ctrl_elf):
        if not Path(elf).exists():
            sys.exit(f"ERROR: not built: {elf}")

    if args.run:
        # 复用 case 的 test.sh -r（两遍烧录法在 load_and_run 内）
        test_sh = (TESTS_DIR / args.case / "test.sh")
        env = dict(os.environ)
        env["I3C_TEST_ROOT"] = str(SAMPLE_ROOT).replace("\\", "/")
        env["SKIP_LOAD"] = "y"
        env["AGENT_RUN"] = "y"
        env["RUN_TIMEOUT"] = "0"
        proc = subprocess.Popen(["bash", str(test_sh).replace("\\", "/"), "-r"],
                                cwd=str(SAMPLE_ROOT).replace("\\", "/"), env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        log(args, f"[dump] flashing + running {args.run}s ...")
        try:
            proc.wait(timeout=args.run)
        except subprocess.TimeoutExpired:
            pass
        try:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                           capture_output=True, timeout=10)
        except Exception:
            proc.kill()
        time.sleep(1)

    log(args, "[dump] connecting to read memory ...")
    ctrl_results, ctrl_reasons, ctrl_state, ctrl_note = dump_core(ctrl_elf, names, args)
    tgt_results, tgt_reasons, tgt_state, tgt_note = dump_core(tgt_elf, [], args)
    reasons = dict(tgt_reasons)
    reasons.update(ctrl_reasons)
    # 有效性门（2026-09-18；F2/F3 拆两态）：某核内存结果不可信 => 清空该核，以免
    # 把 .bss 零值（= TR_PASS）读成假 PASS。两态文案见 common/judge.py
    # （"ELF 无符号 = 旧构建需重编" vs "符号在但未置位 = 内存不可信"）。
    _bad = [(lbl, st, nt)
            for lbl, st, nt in (("CTRL", ctrl_state, ctrl_note),
                                ("TGT", tgt_state, tgt_note)) if st != MEM_OK]
    valid_ok = not _bad
    _mem_state = (MEM_NO_SENTINEL if any(st == MEM_NO_SENTINEL for _, st, _ in _bad)
                  else (MEM_INVALID if _bad else MEM_OK))
    _note = "" if valid_ok else " ".join(f"{l}: {nt}" for l, _, nt in _bad)
    if not valid_ok:
        log(args, "[dump] " + _note)
        ctrl_results, tgt_results, reasons = {}, {}, {}

    if args.json:
        print(json.dumps({"valid": valid_ok, "mem_state": _mem_state,
                          "TGT": tgt_results, "CTRL": ctrl_results,
                          "reasons": reasons, "note": _note}))
        return

    if not valid_ok:
        print("\n===== RESULTS FROM MEMORY =====")
        print("!!! " + _note + " !!!")
        return

    for label, res in (("CTRL (cpu2, 被测核)", ctrl_results),
                       ("TGT (cpu1, 常驻监督)", tgt_results)):
        print(f"\n===== RESULTS FROM MEMORY — {label} =====")
        if not res:
            print("  (无数据)")
            continue
        passed = ran = 0
        for name, st in res.items():
            if st == "PASS":
                passed += 1
            if st != "NOT_RUN":
                ran += 1
            print(f"  {name:<52} {st}")
        print(f"  -> {passed}/{ran} PASS")

    if ctrl_reasons:
        print("\n===== FAIL REASONS FROM MEMORY =====")
        for name, r in ctrl_reasons.items():
            print(f"  {name}: {r}")


if __name__ == "__main__":
    main()
