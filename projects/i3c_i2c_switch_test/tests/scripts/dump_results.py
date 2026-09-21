#!/usr/bin/env python3
"""从 GDB 读内存取测试结果——串口不可用/串口 token 被刷掉时的兜底通道。

固件把每项测试的结果记账到 810_driver_test/common/test_results.h 公共模块的
**全局数组**（int8_t test_results[]：-1 未跑 / 0 PASS / 1 FAIL；char
test_reasons[][]：失败原因）。本脚本连 DebugServer 直接读内存，不依赖串口。
两个核的 elf 里符号同名（各自链接），按 elf 区分核。

实现要点（gdb 无 Python，全部用 -ex 命令链）：
    - 用 riscv64-unknown-elf-nm 解析符号地址（CPU2 elf 链接地址=加载地址 0x10000000，
      直接可读），gdb 只做裸地址内存读，不 add-symbol-file（避免节地址推算失败）。
    - 中断目标用 "interrupt"（等价 \x03）；命令出错不中断后续 -ex。
    - 读数组用 x/15db（有符号十进制字节），每行 "地址: 数值..."，split(':') 后解析。
    - 对 FAIL 项再 x/s 读 reason（reason 行宽 96，地址 = 基址 + i*96）。

本工程（i3c_i2c_switch_test）不使用 ztest 注册（ztest_unit_test_list 为空），
test_results[] 槽位顺序 = protocol.h 枚举序（tr_record(enum_id,...) 按枚举下标写入），
不是 ztest 链接序。本脚本 TEST_NAMES 硬编码为枚举序列表。

用法:
    python dump_results.py --case case_switch_core                  # 只读当前内存
    python dump_results.py --case case_switch_core --json           # JSON 输出（回归兜底合并用，CLAUDE.md §12.3）
    python dump_results.py --case case_switch_core --run 35         # 先烧录跑 35s 再读
    python dump_results.py --case ... --count 15 --reason-stride 96 # 覆盖默认值
    python dump_results.py --case ... --raw                         # 打印 gdb 原始输出

--json 输出结构（stdout 仅此对象，信息行走 stderr）:
    {"CTRL": {"<name>": "PASS|FAIL|NOT_RUN", ...}, "TGT": {...},
     "reasons": {"<name>": "失败原因", ...}}
"""
import argparse
import json
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
CONTROLLER_LOAD_ADDR = 0x10000000
TARGET_ROM_START = 0x100C0000


def win_path(p) -> str:
    """传给 Windows 版 gdb/nm 的路径规范化为 D:/... 形式。

    从 MSYS 环境（Git Bash 的 /usr/bin/python）启动时 __file__ 解析为
    /d/... 形式，Windows gdb 打不开（报 No such file or directory）。
    """
    s = Path(p).as_posix()
    if len(s) > 2 and s[0] == "/" and s[1].isalpha() and s[2] == "/":
        s = f"{s[1].upper()}:/{s[3:]}"
    return s


# ★ 列表顺序 = protocol.h 枚举序（tr_record(enum_id,...) 按枚举下标写入 test_results[]）。
# 本工程不使用 ztest 注册（ztest_unit_test_list 为空），无 ztest 链接序概念。
# 枚举序（2026-09-09 从 preprocessed .c.i 确认，11 项）：
#   0:pinctrl_states, 1:i3c_baseline, 2:switch_i3c_to_i2c,
#   3:i2c_rw_1, 4:i2c_rw_16, 5:i2c_rw_17, 6:i2c_rw_100,
#   7:switch_i2c_to_i3c, 8:i2c_nack_bad_addr, 9:switch_stress,
#   10:broadcast_nack_recovery
# 增删测试项后必须同步枚举序、本列表、run_test.py TEST_NAMES 三处。
TEST_NAMES = [
    "pinctrl_states",
    "i3c_baseline",
    "switch_i3c_to_i2c",
    "i2c_rw_1",
    "i2c_rw_16",
    "i2c_rw_17",
    "i2c_rw_100",
    "switch_i2c_to_i3c",
    "i2c_nack_bad_addr",
    "switch_stress",
    "broadcast_nack_recovery",
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


def status_of(v):
    if v < 0:
        return "NOT_RUN"
    if v == 0:
        return "PASS"
    return "FAIL"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", default="case_switch_core")
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
    tgt_elf = win_path(build / "target" / "zephyr" / "zephyr.elf")
    ctrl_elf = win_path(build / "controller" / "zephyr" / "zephyr.elf")
    ctrl_bin = (build / "controller" / "zephyr" / "zephyr.bin").as_posix()
    for f in (tgt_elf, ctrl_elf, ctrl_bin):
        if not Path(f).exists():
            sys.exit(f"ERROR: not built: {f}")

    # 1. 解析符号地址（两核 elf 各查一次；符号同名，elf 区分核）
    addrs = {}
    for role, elf in (("CTRL", ctrl_elf), ("TGT", tgt_elf)):
        res = nm_addr(elf, RESULT_SYM)
        reason = nm_addr(elf, REASON_SYM)
        valid = nm_addr(elf, VALID_SYM)   # None = 旧 ELF 无哨兵符号
        if res is None or reason is None:
            sys.exit(f"ERROR: symbol not found in {elf}: {RESULT_SYM}={res} {REASON_SYM}={reason}")
        addrs[role] = (res, reason, valid)
        log(args, f"[dump] {role}: {RESULT_SYM}@{res}, {REASON_SYM}@{reason}")

    # 2. 可选：先烧录跑一轮（复用两遍烧录法 + 干净退出，串口全程不开）
    if args.run:
        gdb_cmd = [
            GDB, "-q",
            "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
            "-ex", "set confirm off", "-ex", "set pagination off",
            "-ex", f"file {tgt_elf}",
            "-ex", "reset", "-ex", "load",
            "-ex", f"restore {ctrl_bin} binary 0x{CONTROLLER_LOAD_ADDR:X}",
            "-ex", f"set $pc=0x{TARGET_ROM_START:X}",
            "-ex", "continue",
        ]
        proc = subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log(args, f"[dump] flashing + running {args.run}s (prime pass) ...")
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
        time.sleep(1)
        proc = subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log(args, f"[dump] capture pass {args.run}s ...")
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
        time.sleep(1)

    # 3. dump 会话：interrupt + 读双核结果数组
    log(args, f"[dump] connecting {GDB_HOST}:{GDB_PORT} to read memory ...")
    _ex = []
    for _role in ("CTRL", "TGT"):
        if addrs[_role][2] is not None:
            # 哨兵与结果数组同一 gdb 会话读（少一次连接 = 少一次 DebugServer 残留）
            _ex += ["-ex", f"x/1db 0x{addrs[_role][2]}"]
        _ex += ["-ex", f"x/{args.count}db 0x{addrs[_role][0]}"]
    out = gdb_session(_ex)
    if args.raw:
        print("\n===== GDB RAW =====")
        print(out)

    # 4. 解析数组 → 结构化结果
    # 按**读取顺序**位置切分（每核 = [哨兵] + args.count 个槽位）。
    # 注意：不能对同一 out 调两次 parse_x_bytes(out, N)——它总是从头扫，
    # 会把 CTRL 的数组同时当成 TGT 的（2026-09-18 修复的既有错归属 bug）。
    _n_read = args.count * 2 + sum(1 for _r in ("CTRL", "TGT")
                                   if addrs[_r][2] is not None)
    _all = parse_x_bytes(out, _n_read)
    results, valid_ok = {}, {}
    _pos = 0
    for role in ("CTRL", "TGT"):
        if addrs[role][2] is not None:
            valid_ok[role] = bool(_all[_pos:_pos + 1]) and _all[_pos] == TR_VALID_MAGIC
            _pos += 1
        else:
            valid_ok[role] = False
        results[role] = _all[_pos:_pos + args.count]
        _pos += args.count

    # 5. 失败的项读 reason（内存里的失败原因，串口丢了一样能拿到）
    fail_items = []  # [(role, name)]
    for role in ("CTRL", "TGT"):
        for i, v in enumerate(results.get(role, [])):
            if v == 1:
                fail_items.append((role, TEST_NAMES[i] if i < len(TEST_NAMES) else f"test_{i}"))
    fail_ex = []
    for role in ("CTRL", "TGT"):
        for i, v in enumerate(results.get(role, [])):
            if v == 1:
                fail_ex.append("-ex")
                fail_ex.append(
                    f"x/s 0x{int(addrs[role][1], 16) + i * args.reason_stride:X}")
    reasons = {}
    if fail_ex:
        reason_text = gdb_session(fail_ex)
        if args.raw:
            print("\n===== FAIL REASONS RAW =====")
            print(reason_text)
        # x/s 输出形如 "0x1011c220: \"byte[3]:0x12!=0x34; \""，按顺序与 fail_items 对齐
        matches = re.findall(r'0x[0-9a-f]+:\s+"([^"]*)"', reason_text)
        for (role, name), r in zip(fail_items, matches):
            reasons[name] = r

    # 6b. 有效性门（2026-09-18；F2/F3 2026-09-18 拆两态）：哨兵未置位 / 旧 ELF 无哨兵符号
    # => 该核固件没跑到 tr_init，槽位停在 .bss 零值（= TR_PASS）。无效核清空，
    # 消费者按 MISSING 处理（fail-closed），禁止输出假 PASS。
    # 两态区分见 common/judge.py："ELF 无符号 = 旧构建需重编" vs "符号在但未置位 = 不可信"。
    if not all(valid_ok.values()):
        _bad, _notes, _states = [], [], {}
        for _r in ("CTRL", "TGT"):
            if valid_ok[_r]:
                continue
            _st, _nt = sentinel_state(addrs[_r][2] is not None, False)
            _states[_r] = _st
            _bad.append(_r)
            _notes.append(f"{_r}: {_nt}")
        # 汇总状态取"更需要动作"的那个：缺符号（要重编）> 未置位（内存不可信）
        _mem_state = (MEM_NO_SENTINEL if MEM_NO_SENTINEL in _states.values()
                      else MEM_INVALID)
        _note = " ".join(_notes)
        log(args, "[dump] " + _note)
        for _r in _bad:
            results[_r] = []
        if args.json:
            print(json.dumps({"valid": False, "mem_state": _mem_state,
                              "CTRL": {}, "TGT": {}, "reasons": {},
                              "note": _note}))
            return
        print("\n===== RESULTS FROM MEMORY =====")
        print("!!! " + _note + " !!!")
        return

    # 6. 输出：人读表格 / --json 结构化
    if args.json:
        out_json = {"valid": True, "mem_state": MEM_OK,
                    "CTRL": {}, "TGT": {}, "reasons": reasons}
        for role, vals in results.items():
            for i, v in enumerate(vals):
                name = TEST_NAMES[i] if i < len(TEST_NAMES) else f"test_{i}"
                out_json[role][name] = status_of(v)
        print(json.dumps(out_json))
        return

    print("\n===== RESULTS FROM MEMORY =====")
    for role in ("CTRL", "TGT"):
        vals = results[role]
        print(f"--- {role} ({len(vals)} items) ---")
        if len(vals) < args.count:
            print("  (WARNING: 解析不到完整数组，gdb 输出见上)")
            continue
        passed = ran = 0
        for i, v in enumerate(vals):
            name = TEST_NAMES[i] if i < len(TEST_NAMES) else f"test_{i}"
            st = status_of(v)
            if st == "PASS":
                passed += 1
            if st != "NOT_RUN":
                ran += 1
            print(f"  {name:<20} {st}")
        print(f"  -> {passed}/{ran} PASS")

    if fail_items:
        print("\n===== FAIL REASONS FROM MEMORY =====")
        for (role, name), r in zip(fail_items, matches if fail_ex else []):
            print(f"  {role} {name}: {r}")


if __name__ == "__main__":
    main()
