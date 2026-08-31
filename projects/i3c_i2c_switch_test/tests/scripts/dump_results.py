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

ztest 原生化说明：test_results 槽位顺序 = ztest_unit_test 段的**链接序**
（ztest_glue 按链接下标记账；本工程 dispatcher 的结账记账也用同一口径），
不再是 protocol.h 枚举序——本脚本 TEST_NAMES 硬编码为链接序列表
（不再解析协议头，见 TEST_NAMES 注释）。

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
import time
from pathlib import Path

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


# ★ 列表顺序 = ztest_unit_test 段的**链接序**（§10.27 + 协调者修正口径）：
# 链接器对 ztest_unit_test 段 SORT_BY_NAME，且 TYPE_SECTION_ITERABLE 给段名
# 追加尾下划线——比较的是完整段名（含尾 '_'），数字 '0'-'9'(0x30-0x39)
# 排序在 '_'(0x5F) 之前，因此带数字后缀的测试名与裸名字符串字典序**相反**
# （如 i2c_rw_100_ < i2c_rw_16_ < i2c_rw_17_ < i2c_rw_1_）。
# ztest_glue 的 tr_record idx 按链接下标记账，本列表必须与之一一对齐，
# 否则内存兜底结果张冠李戴（全 PASS 时潜伏不可见）。
# 复核方法（构建后执行，切勿用普通 nm 名字典序输出）：
#   nm --numeric-sort build/{controller,target}/zephyr/zephyr.elf \
#       | grep z_ztest_unit_test
# 地址升序 = 链接序；或对照 zephyr.map 的 ._ztest_unit_test.static.* 段序。
# 实测（2026-08-29 构建产物）两核一致：
#   z_ztest_unit_test__switch_ctrl__i2c_nack_bad_addr
#   z_ztest_unit_test__switch_ctrl__i2c_rw_100
#   z_ztest_unit_test__switch_ctrl__i2c_rw_16
#   z_ztest_unit_test__switch_ctrl__i2c_rw_17
#   z_ztest_unit_test__switch_ctrl__i2c_rw_1
#   z_ztest_unit_test__switch_ctrl__i3c_baseline
#   z_ztest_unit_test__switch_ctrl__pinctrl_states
#   z_ztest_unit_test__switch_ctrl__switch_i2c_to_i3c
#   z_ztest_unit_test__switch_ctrl__switch_i3c_to_i2c
#   z_ztest_unit_test__switch_ctrl__switch_stress
# （target 为 switch_tgt 前缀，顺序相同。）
# 增删测试项后必须重新核对链接序并同步本列表与 run_i3c_i2c_switch_tests.py
# 的 TEST_NAMES。
TEST_NAMES = [
    "i2c_nack_bad_addr",
    "i2c_rw_100",
    "i2c_rw_16",
    "i2c_rw_17",
    "i2c_rw_1",
    "i3c_baseline",
    "pinctrl_states",
    "switch_i2c_to_i3c",
    "switch_i3c_to_i2c",
    "switch_stress",
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
        if res is None or reason is None:
            sys.exit(f"ERROR: symbol not found in {elf}: {RESULT_SYM}={res} {REASON_SYM}={reason}")
        addrs[role] = (res, reason)
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
        try:
            out, _ = proc.communicate(timeout=args.run)
        except subprocess.TimeoutExpired:
            try:
                proc.stdin.write("\x03"); proc.stdin.flush(); time.sleep(1.0)
                proc.stdin.write("detach\nquit\n"); proc.stdin.flush()
                out, _ = proc.communicate(timeout=10)
            except Exception:
                proc.kill(); out, _ = proc.communicate()
        time.sleep(1)
        proc = subprocess.Popen(gdb_cmd, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log(args, f"[dump] capture pass {args.run}s ...")
        try:
            out, _ = proc.communicate(timeout=args.run)
        except subprocess.TimeoutExpired:
            try:
                proc.stdin.write("\x03"); proc.stdin.flush(); time.sleep(1.0)
                proc.stdin.write("detach\nquit\n"); proc.stdin.flush()
                out, _ = proc.communicate(timeout=10)
            except Exception:
                proc.kill(); out, _ = proc.communicate()
        time.sleep(1)

    # 3. dump 会话：interrupt + 读双核结果数组
    log(args, f"[dump] connecting {GDB_HOST}:{GDB_PORT} to read memory ...")
    out = gdb_session([
        "-ex", f"x/{args.count}db 0x{addrs['CTRL'][0]}",
        "-ex", f"x/{args.count}db 0x{addrs['TGT'][0]}",
    ])
    if args.raw:
        print("\n===== GDB RAW =====")
        print(out)

    # 4. 解析数组 → 结构化结果
    results = {}
    for role in ("CTRL", "TGT"):
        results[role] = parse_x_bytes(out, args.count)

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

    # 6. 输出：人读表格 / --json 结构化
    if args.json:
        out_json = {"CTRL": {}, "TGT": {}, "reasons": reasons}
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
