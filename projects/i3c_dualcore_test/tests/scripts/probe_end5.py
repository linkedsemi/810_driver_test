#!/usr/bin/env python3
"""临时调试：抓 'E' <id> <result> 的 TGT_PARSE_END_RESULT 分支是否执行。

断点 i3c_target_test.c:399（k_msgq_put 处，evt 已构造未入队），
连续 continue 两次：第 1 次命中 = daa_discovery 的 'E' 0，第 2 次命中 =
write_read_16 的 'E' 5。命中后打印 evt/current_test_id/ctrl_result。

第 2 次超时 = 'E' 5 的 result 字节没到 parse（字节丢失/状态机错乱）。
用法: python scripts/probe_end5.py
"""
import subprocess
import time
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parents[1]
GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
CTRL_LOAD = 0x10000000
TGT_ROM = 0x100C0000


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


def main():
    build = TESTS_DIR / "case_pure_i3c_12m5" / "build"
    tgt_elf = (build / "target" / "zephyr" / "zephyr.elf").as_posix()
    ctrl_bin = (build / "controller" / "zephyr" / "zephyr.bin").as_posix()

    print("[probe] prime pass 12s ...", flush=True)
    gdb_session(base_cmds(tgt_elf, ctrl_bin) + ["-ex", "continue"], timeout=14)
    time.sleep(1)

    print("[probe] capture pass, dual-break ztest suite entry + run_current_test ...", flush=True)
    cmds = [
        "-ex", "break *0x100cd902",   # z_ztest_run_test_suite 入口
        "-ex", "break *0x100c1556",   # run_current_test 入口
        "-ex", "continue",   # 1: 第一次 ztest 调用（daa）
        "-ex", "continue",   # 2: 第一次 run_current_test（daa）
        "-ex", "continue",   # 3: 第二次 ztest 调用（wr16）← 不命中 = dispatcher 卡死
        "-ex", "continue",   # 4: 第二次 run_current_test
        "-ex", "p current_test_id",
        "-ex", "detach", "-ex", "quit",
    ]
    out = gdb_session(base_cmds(tgt_elf, ctrl_bin) + cmds, timeout=75)

    print("\n===== GDB OUTPUT =====")
    print(out)


if __name__ == "__main__":
    main()
