#!/usr/bin/env python3
"""从 GDB 读内存取测试结果——串口不可用/串口 token 被刷掉时的兜底通道。

IOPMP 单核工程版：只读 cpu1（TGT）elf 的公共结果数组。
从 i3c_dualcore_test 样板 dump_results.py 改造（去掉 controller 核）。

固件把每项测试的结果记账到 810_driver_test/common/test_results.h 公共模块的
**全局数组**（int8_t test_results[]：-1 未跑 / 0 PASS / 1 FAIL；char
test_reasons[][]：失败原因）。本脚本连 DebugServer 直接读内存，不依赖串口。

实现要点（gdb 无 Python，全部用 -ex 命令链）：
    - 用 riscv64-unknown-elf-nm 解析符号地址（cpu1 ELF 链接地址=加载地址，
      直接可读），gdb 只做裸地址内存读，不 add-symbol-file。
    - 中断目标用 "interrupt"（等价 \x03）；命令出错不中断后续 -ex。
    - 读数组用 x/Ndb（有符号十进制字节），每行 "地址: 数值..."，split(':') 解析。
    - 对 FAIL 项再 x/s 读 reason（reason 行宽 96，地址 = 基址 + i*96）。

用法:
    python dump_results.py --case case_default                 # 只读当前内存
    python dump_results.py --case case_default --json          # JSON 输出（回归兜底合并用，CLAUDE.md §12.3）
    python dump_results.py --case case_default --run 35        # 先烧录跑 35s 再读
    python dump_results.py --case ... --count 9 --reason-stride 96
    python dump_results.py --case ... --raw                    # 打印 gdb 原始输出

--json 输出结构（stdout 仅此对象，信息行走 stderr）:
    {"TGT": {"<name>": "PASS|FAIL|NOT_RUN", ...}, "reasons": {"<name>": "失败原因"}}
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

TESTS_DIR = Path(__file__).resolve().parents[1]
GDB = "riscv64-unknown-elf-gdb"
NM = "riscv64-unknown-elf-nm"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
# 用户需求#8：单核一律 1os 构建，cpu1 入口 = 0x10000000（readelf 实测）；
# 2os 构建入口 0x100c0000，需显式 TARGET_ROM_START=0x100c0000 环境变量覆盖
TARGET_ROM_START = int(os.environ.get("TARGET_ROM_START", "0x10000000"), 0)

# 链接序 = ztest_unit_test 段 SORT_BY_NAME 字典序（§10.27：完整段名带尾
# 下划线比较，数字 '0'-'9' 排在 '_' 之前——勿用 plain nm 名字典序，与
# 真实链接序可能相反）。与 glue 的 tr_record idx（注册顺序）对齐；每次
# 增删测试项后必须重新核对：
#   nm --numeric-sort <elf> | grep _ztest_unit_test__iopmp__
TEST_NAMES = [
    "test_iopmp_idempotent_init", "test_iopmp_is_enable_after_init",
    "test_iopmp_napot_encode_decode", "test_iopmp_napot_invalid_params",
    "test_iopmp_register_readback",
]

# 公共模块统一符号名（test_results.h）：结果数组 + reason 数组基址
RESULT_SYM = "test_results"
REASON_SYM = "test_reasons"


def log(args, msg):
    """信息行：人读模式走 stdout，--json 模式走 stderr（不污染 JSON）。"""
    if args.json:
        print(msg, file=sys.stderr, flush=True)
    else:
        print(msg, flush=True)


def nm_addr(elf, sym):
    """返回符号在 elf 中的链接地址（十六进制字符串，无 0x 前缀）。"""
    out = subprocess.run([NM, elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == sym:
            return parts[0]
    return None


def gdb_session(extra_ex):
    """开一个 gdb 会话：target remote + interrupt + extra_ex + detach + quit。"""
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
    """从 gdb `x/Ndb 0xADDR` 输出中提取 count 个有符号字节。"""
    vals = []
    for line in text.splitlines():
        if ":" not in line or line.strip().startswith("$"):
            continue
        body = line.split(":", 1)[1]
        vals.extend(int(v) for v in re.findall(r"-?\d+", body))
        if len(vals) >= count:
            break
    return vals[:count]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", default="case_default")
    ap.add_argument("--run", type=float, default=None, help="先两遍烧录并运行 N 秒再 dump")
    ap.add_argument("--count", type=int, default=len(TEST_NAMES))
    ap.add_argument("--reason-stride", type=int, default=96)
    ap.add_argument("--raw", action="store_true")
    ap.add_argument("--json", action="store_true",
                    help="JSON 输出（回归脚本兜底合并用，CLAUDE.md §12.3）")
    args = ap.parse_args()

    for tool in (GDB, NM):
        if shutil.which(tool) is None:
            sys.exit(f"ERROR: {tool} not in PATH (Xuantie toolchain?)")

    build = TESTS_DIR / args.case / "build"
    elf = (build / "target" / "zephyr" / "zephyr.elf").as_posix()
    if not Path(elf).exists():
        sys.exit(f"ERROR: not built: {elf}")

    # 1. 解析符号地址（cpu1 单核）
    res = nm_addr(elf, RESULT_SYM)
    reason = nm_addr(elf, REASON_SYM)
    if res is None or reason is None:
        sys.exit(f"ERROR: symbol not found in {elf}: {RESULT_SYM}={res} {REASON_SYM}={reason}")
    log(args, f"[dump] TGT: {RESULT_SYM}@{res}, {REASON_SYM}@{reason}")

    # 2. 可选：先烧录跑一轮（复用两遍烧录法 + 干净退出，串口全程不开）
    if args.run:
        gdb_cmd = [
            GDB, "-q",
            "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
            "-ex", "set confirm off", "-ex", "set pagination off",
            "-ex", f"file {elf}",
            "-ex", "reset", "-ex", "load",
            "-ex", f"set $pc=0x{TARGET_ROM_START:X}",
            "-ex", "continue",
        ]
        for pass_name in ("prime", "capture"):
            proc = subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            log(args, f"[dump] flashing + running {args.run}s ({pass_name} pass) ...")
            try:
                out, _ = proc.communicate(timeout=args.run)
            except subprocess.TimeoutExpired:
                try:
                    proc.stdin.write("\x03"); proc.stdin.flush(); time.sleep(1.0)
                    proc.stdin.write("detach\nquit\n"); proc.stdin.flush()
                    out, _ = proc.communicate(timeout=10)
                except Exception:
                    proc.kill(); out, _ = proc.communicate()
            if "Loading section" not in out:
                log(args, f"[dump] !!! {pass_name} pass flash failed !!!")
                print(out[-1200:], file=sys.stderr, flush=True)
                sys.exit(2)
            time.sleep(1)

    # 3. dump 会话：interrupt + 读结果数组
    log(args, f"[dump] connecting {GDB_HOST}:{GDB_PORT} to read memory ...")
    out = gdb_session([
        "-ex", f"x/{args.count}db 0x{res}",
    ])
    if args.raw:
        print("\n===== GDB RAW =====")
        print(out)

    # 4. 解析数组 → 结构化结果；FAIL 项再从 reason 数组读原因
    vals = parse_x_bytes(out, args.count)
    results = {}
    for i, v in enumerate(vals):
        name = TEST_NAMES[i] if i < len(TEST_NAMES) else f"test_{i}"
        if v < 0:
            results[name] = "NOT_RUN"
        elif v == 0:
            results[name] = "PASS"
        else:
            results[name] = "FAIL"
    fail_names = [n for n, s in results.items() if s == "FAIL"]
    fail_ex = []
    for i, v in enumerate(vals):
        if v == 1:
            fail_ex += ["-ex", f"x/s 0x{int(reason, 16) + i * args.reason_stride:X}"]
    reasons = {}
    if fail_ex:
        reason_text = gdb_session(fail_ex)
        if args.raw:
            print("\n===== FAIL REASONS RAW =====")
            print(reason_text)
        matches = re.findall(r'0x[0-9a-f]+:\s+"([^"]*)"', reason_text)
        for name, r in zip(fail_names, matches):
            reasons[name] = r

    # 5. 输出：人读表格 / --json 结构化
    if args.json:
        print(json.dumps({"TGT": results, "reasons": reasons}))
        return

    print("\n===== RESULTS FROM MEMORY =====")
    print("--- TGT (cpu1) ---")
    if len(vals) < args.count:
        print("  (WARNING: 解析不到完整数组，gdb 输出见上)")
    passed = ran = 0
    for i, v in enumerate(vals):
        name = TEST_NAMES[i] if i < len(TEST_NAMES) else f"test_{i}"
        st = results[name]
        if st == "PASS":
            passed += 1
        if st != "NOT_RUN":
            ran += 1
        print(f"  {name:<20} {st}")
    print(f"  -> {passed}/{ran} PASS")

    if fail_names:
        print("\n===== FAIL REASONS FROM MEMORY =====")
        for name, r in zip(fail_names, matches if fail_ex else []):
            print(f"  {name}: {r}")


if __name__ == "__main__":
    main()
