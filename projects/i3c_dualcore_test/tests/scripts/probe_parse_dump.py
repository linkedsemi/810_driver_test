#!/usr/bin/env python3
"""临时调试：零重建探针——GDB attach 读 target 侧解析机状态 + I3C 外设寄存器。

背景：clean run 中 write_read_16 的 'E' 5 <result> 三个字节未走完解析状态机
（TEST_END 事件未入队、ztest 二次调用未发生），但 CTRL 侧 actual==3 PASS。
本探针在读 target 当前内存快照，判定字节去向：
  - write_len==4 且 write_data[1]==0x45 → 'E' 被 default 分支吞掉（状态机错位）
  - write_len==2 或 tx_len==0x45   → 到达时状态是 READ_LEN（tx_len=69）
  - write_len==1 且 RXCOUNT==3     → 字节卡在 RX FIFO，中断从未投递（死窗/掩码）
  - SINTMASKED 位域              → 结束时刻哪些 target 中断还使能着

用法: python probe_parse_dump.py            # 读当前内存（目标须已跑完一轮）
"""
import re
import subprocess
import sys
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parents[1]
GDB = "riscv64-unknown-elf-gdb"
NM = "riscv64-unknown-elf-nm"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
I3C_BASE = 0x40094000

SYMS = [
    "write_len", "write_len_expected", "tx_len", "last_read_len",
    "parse_state", "current_test_id", "ctrl_result",
    "rx_idx", "read_req_cnt", "read_proc_cnt",
    "write_data", "test_results",
]


def nm_addr(elf, sym):
    out = subprocess.run([NM, elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == sym:
            return parts[0]
    return None


def main():
    elf = (TESTS_DIR / "case_pure_i3c_12m5" / "build" / "target" /
           "zephyr" / "zephyr.elf").as_posix()

    addrs = {}
    for s in SYMS:
        a = nm_addr(elf, s)
        if a is None:
            sys.exit(f"symbol not found: {s}")
        addrs[s] = a
    print("[probe] symbols:", addrs, flush=True)

    ex = []
    ex += ["-ex", f"x/2db 0x{addrs['write_len']}"]
    ex += ["-ex", f"x/2db 0x{addrs['write_len_expected']}"]
    ex += ["-ex", f"x/2db 0x{addrs['tx_len']}"]
    ex += ["-ex", f"x/2db 0x{addrs['last_read_len']}"]
    ex += ["-ex", f"x/2db 0x{addrs['parse_state']}"]
    ex += ["-ex", f"x/2db 0x{addrs['current_test_id']}"]
    ex += ["-ex", f"x/2db 0x{addrs['ctrl_result']}"]
    ex += ["-ex", f"x/2db 0x{addrs['rx_idx']}"]
    ex += ["-ex", f"x/2db 0x{addrs['read_req_cnt']}"]
    ex += ["-ex", f"x/2db 0x{addrs['read_proc_cnt']}"]
    ex += ["-ex", f"x/10db 0x{addrs['write_data']}"]
    ex += ["-ex", f"x/15db 0x{addrs['test_results']}"]
    # 外设寄存器（base + offset）
    for name, off in (("SSTATUS", 0x8), ("SINTSET", 0x10), ("SINTCLR", 0x14),
                      ("SINTMASKED", 0x18), ("SERRWARN", 0x1C),
                      ("SDATACTRL", 0x2C), ("SWDATAB", 0x30),
                      ("SRDATAB", 0x40), ("SDYNADDR", 0x64)):
        ex += ["-ex", f"x/1wx 0x{I3C_BASE + off:X}"]
        ex += ["-ex", f"printf \"    [{name} @0x{off:X}]\\n\""]

    cmd = [
        GDB, "-q", "-batch",
        "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
        "-ex", "set confirm off", "-ex", "set pagination off",
        "-ex", "interrupt",
    ] + ex + ["-ex", "detach", "-ex", "quit"]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        sys.exit("ERROR: gdb timed out (DebugServer 残留? 干净连接或重启)")

    print("\n===== GDB RAW =====")
    print(proc.stdout)


if __name__ == "__main__":
    main()
