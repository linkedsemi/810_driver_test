#!/usr/bin/env python3
"""临时调试：断点法抓 write_read_16 失败现场（target 侧记账时刻）。

断点 tr_record(idx==5)（write_read_16 的结果记账处，线程上下文非 ISR），
命中后读 target 关键变量 + I3C6 硬件寄存器（SSTATUS/SDATACTRL/SINTMASKED/
SERRWARN），用于定位 read actual 0 的根因。

用法:
    python scripts/probe_wr16.py
"""
import re
import subprocess
import sys
import time
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parents[1]
GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
CTRL_LOAD = 0x10000000
TGT_ROM = 0x100C0000

I3C6_BASE = 0x40094400
OFF_SSTATUS, OFF_SDATACTRL, OFF_SINTMASKED, OFF_SERRWARN = 0x8, 0x2C, 0x18, 0x1C

# target 固件符号地址（nm 结果，链接地址=加载地址，直接可读）
SYM = {
    "read_req_cnt": 0x10107EEC,   # u32
    "read_proc_cnt": 0x10107EE8,  # u32
    "tx_idx": 0x10109F4C,   # u16
    "tx_len": 0x10109F4A,   # u16
    "last_read_len": 0x10109F48,  # u16
    "tgt_tx_buf": 0x10107C40,     # 32B pattern
}
READ_REQ_CB = 0x100C1B20  # slave_read_requested_cb


def gdb_session(cmds, timeout=60):
    proc = subprocess.Popen([GDB, "-q"] + cmds, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True)
    try:
        out, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            proc.stdin.write("\x03"); proc.stdin.flush(); time.sleep(1.0)
            proc.stdin.write("detach\nquit\n"); proc.stdin.flush()
            out, _ = proc.communicate(timeout=10)
        except Exception:
            proc.kill(); out, _ = proc.communicate()
    return out


def base_cmds(tgt_elf, ctrl_bin):
    return [
        "-q",
        "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
        "-ex", "set confirm off", "-ex", "set pagination off",
        "-ex", f"file {tgt_elf}",
        "-ex", "reset", "-ex", "load",
        "-ex", f"restore {ctrl_bin} binary 0x{CTRL_LOAD:X}",
        "-ex", f"set $pc=0x{TGT_ROM:X}",
    ]


def dump_cmds():
    cmds = []
    for name, addr in SYM.items():
        if name == "tgt_tx_buf":
            cmds += ["-ex", f"x/8bx 0x{addr:X}"]
        elif name in ("read_req_cnt", "read_proc_cnt"):
            cmds += ["-ex", f"x/dw 0x{addr:X}"]
        else:  # u16
            cmds += ["-ex", f"x/dh 0x{addr:X}"]
    cmds += [
        "-ex", f"x/wx 0x{I3C6_BASE + OFF_SSTATUS:X}",     # SSTATUS
        "-ex", f"x/wx 0x{I3C6_BASE + OFF_SDATACTRL:X}",   # SDATACTRL (TXCOUNT/RXCOUNT)
        "-ex", f"x/wx 0x{I3C6_BASE + OFF_SINTMASKED:X}",  # SINTMASKED
        "-ex", f"x/wx 0x{I3C6_BASE + OFF_SERRWARN:X}",    # SERRWARN
    ]
    return cmds


def main():
    build = TESTS_DIR / "case_pure_i3c_12m5" / "build"
    tgt_elf = (build / "target" / "zephyr" / "zephyr.elf").as_posix()
    ctrl_bin = (build / "controller" / "zephyr" / "zephyr.bin").as_posix()

    # prime 轮：先加载跑一轮，规避 I-cache 残留（断点轮即使不命中也不影响）
    print("[probe] prime pass 10s ...", flush=True)
    gdb_session(base_cmds(tgt_elf, ctrl_bin) + ["-ex", "continue"], timeout=12)
    time.sleep(1)

    # capture 轮：断点 slave_read_requested_cb（读请求到达时刻，STOP 之前，
    # read_req_cnt 尚未被 stop_cb 清零，SDATACTRL.TXCOUNT = FIFO 真实字节数）
    print("[probe] capture pass, breakpoint slave_read_requested_cb ...", flush=True)
    out = gdb_session(base_cmds(tgt_elf, ctrl_bin) + [
        "-ex", f"break *0x{READ_REQ_CB:X}",
        "-ex", "continue",
    ] + dump_cmds() + ["-ex", "detach", "-ex", "quit"], timeout=60)

    print("\n===== GDB OUTPUT =====")
    print(out)


if __name__ == "__main__":
    main()
