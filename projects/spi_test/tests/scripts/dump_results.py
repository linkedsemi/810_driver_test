#!/usr/bin/env python3
"""从 GDB 读内存取测试结果——串口不可用/串口输出被刷掉时的兜底通道。

SPI master (spi_ls) 单核工程版：只读 cpu1（TGT）elf 的公共结果数组。
从 trng_test（单核样板）dump_results.py 改造。

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
    python dump_results.py --case case_default --json          # JSON 输出（回归兜底合并用，§12.3）
    python dump_results.py --case case_default --run 35        # 先烧录跑 35s 再读
    python dump_results.py --case ... --raw              # 打印 gdb 原始输出

--json 输出结构（stdout 仅此对象，信息行走 stderr）:
    {"TGT": {"<name>": "PASS|FAIL|NOT_RUN", ...}, "reasons": {"<name>": "失败原因"}
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

# ---- F1/F2/F3 判定框架收敛点：哨兵状态词表与分类见 common/judge.py ----
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "common"))
from judge import (MEM_INVALID, MEM_NO_SENTINEL, MEM_OK,  # noqa: E402
                   sentinel_state)

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
#   nm --numeric-sort <elf> | grep _ztest_unit_test__spi__
# 2026-10-27：删除 `spi_loopback`，新增 `spi_flash` 10 项，链接序排在 `spi` 套件之后。
TEST_NAMES = [
    "concurrent_two_threads", "configure_datasize16", "configure_freq_boundary",
    "configure_lsb_first", "configure_mode_bits", "configure_prescaler_table",
    "device_ready", "error_half_duplex", "error_ti_mode", "hold_on_cs_spe",
    "lock_on_hold_release", "post_reset_state", "reconfigure_same_cfg_prescaler",
    "release_semantics", "transceive_duplex_pair", "transceive_length_sweep",
    "transceive_null_bufs", "transceive_tx_only_rx_only",
    # ---- 新增：spif3 master -> flash4_0 的 flash 访问（套件 spi_flash）----
    "alloc_read_smoke",
    "erase_all_ff", "flash_ready",
    "read_aligned_len_sweep",
    "read_malloc_aligned_len_sweep", "read_malloc_unaligned_len_sweep",
    "read_unaligned_len_sweep",
    "write_aligned_len_sweep",
    "write_malloc_aligned_len_sweep", "write_malloc_unaligned_len_sweep",
    "write_unaligned_len_sweep",
]

# 公共模块统一符号名（test_results.h）：结果数组 + reason 数组基址
RESULT_SYM = "test_results"
REASON_SYM = "test_reasons"
# 有效性哨兵（test_results.h，2026-09-18 加）：tr_init() 填完槽位后置 TR_VALID_MAGIC。
# 未置位 = 固件没跑到 tr_init（挂在内核初始化 / POST_KERNEL / 早期崩溃）
# -> test_results[] 停在 .bss 零值，而零值语义就是 TR_PASS -> 必须判"结果无效"，
# 禁止读成全 PASS（否则固件静默时报告最像好消息）。
# 旧 ELF 里没有这个符号 -> nm 查不到 -> 同样判无效（优雅降级，不崩）。
VALID_SYM = "test_results_valid"
TR_VALID_MAGIC = 0x5A


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
    ap.add_argument("--run", type=float, default=None, help="先烧录并运行 N 秒再 dump")
    ap.add_argument("--count", type=int, default=len(TEST_NAMES))
    ap.add_argument("--reason-stride", type=int, default=96)
    ap.add_argument("--raw", action="store_true")
    ap.add_argument("--json", action="store_true",
                    help="JSON 输出（回归脚本兜底合并用，§12.3）")
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
    valid = nm_addr(elf, VALID_SYM)   # None = 旧 ELF 无哨兵符号
    if res is None or reason is None:
        sys.exit(f"ERROR: symbol not found in {elf}: {RESULT_SYM}={res} {REASON_SYM}={reason}")
    log(args, f"[dump] TGT: {RESULT_SYM}@{res}, {REASON_SYM}@{reason}")

    # 2. 可选：先烧录跑一轮（两遍烧录法 + 干净退出，串口全程不开）
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
                proc.wait(timeout=args.run)
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
                log(args, f"[dump] !!! {pass_name} pass flash failed !!!")
                print(out[-1200:], file=sys.stderr, flush=True)
                sys.exit(2)
            time.sleep(1)

    # 3. dump 会话：interrupt + 读结果数组
    log(args, f"[dump] connecting {GDB_HOST}:{GDB_PORT} to read memory ...")
    _ex = []
    if valid is not None:
        # 哨兵与结果数组同一 gdb 会话读（少一次连接 = 少一次 DebugServer 残留）
        _ex += ["-ex", f"x/1db 0x{valid}"]
    _ex += ["-ex", f"x/{args.count}db 0x{res}"]
    out = gdb_session(_ex)
    if args.raw:
        print("\n===== GDB RAW =====")
        print(out)

    # 4. 解析数组 → 结构化结果；FAIL 项再从 reason 数组读原因
    raw_vals = parse_x_bytes(out, args.count + (0 if valid is None else 1))
    valid_ok = False
    if valid is not None:
        valid_ok = bool(raw_vals) and raw_vals[0] == TR_VALID_MAGIC
        vals = raw_vals[1:]
    else:
        vals = raw_vals
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
    matches = []
    if fail_ex:
        reason_text = gdb_session(fail_ex)
        if args.raw:
            print("\n===== FAIL REASONS RAW =====")
            print(reason_text)
        matches = re.findall(r'0x[0-9a-f]+:\s+"([^"]*)"', reason_text)
        for name, r in zip(fail_names, matches):
            reasons[name] = r

    # 4b. 有效性门（2026-09-18；F2/F3 2026-09-18 拆两态）：分类与文案见 common/judge.py。
    # 旧版把"哨兵符号不存在（旧构建）"和"符号在但未置位（真未初始化）"都报成
    # 同一句 invalid —— 两者动作完全不同（**重新编译** vs **查固件/内存不可信**），
    # 必须分开，否则批量回归读旧镜像时会集体翻成"看起来像内核挂死"的 FAIL。
    mem_state, _note = sentinel_state(valid is not None, valid_ok)
    if mem_state != MEM_OK:
        log(args, "[dump] " + _note)
        if args.json:
            print(json.dumps({"valid": False, "mem_state": mem_state,
                              "TGT": {}, "reasons": {}, "note": _note}))
            return
        print("\n===== RESULTS FROM MEMORY =====")
        print("!!! " + _note + " !!!")
        return

    # 5. 输出：人读表格 / --json 结构化
    if args.json:
        print(json.dumps({"valid": True, "mem_state": MEM_OK,
                          "TGT": results, "reasons": reasons}))
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
        print(f"  {name:<24} {st}")
    print(f"  -> {passed}/{ran} PASS")

    if fail_names:
        print("\n===== FAIL REASONS FROM MEMORY =====")
        for name, r in zip(fail_names, matches):
            print(f"  {name}: {r}")


if __name__ == "__main__":
    main()
