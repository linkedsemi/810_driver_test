#!/usr/bin/env python3
"""读从核/主核的 RLOG 背压计数器（B2-impl 量化用，tmp 工具不参与判定）。

用法:
    python read_rlog_stats.py <elf> [--host 127.0.0.1] [--port 1025]

做法与 `dump_results.py::gdb_session` 同款：`target remote` + `interrupt` +
`x/wx &sym` + `detach`。**只读**，不 reset、不 load（读的就是跑完之后留在
RAM 里的累计值）。

⚠️ 2026-09-24 由 `tmp/b2impl/read_rlog_stats.py` 迁来：本脚本被耐久文档当**工具**
   点名，却住在会被清理的 `tmp/` 里 —— 一旦清理就**静默失去该工具**。
   本脚本**零路径假设**（elf 走命令行参数，无相对路径引用）⇒ 照搬，未改一行。
"""
import argparse
import re
import shutil
import subprocess
import sys

SYMS = [
    "rlog_stat_tx_lines",
    "rlog_stat_tx_retries",
    "rlog_stat_tx_max_retry",
    "rlog_stat_tx_max_block_ms",
    "rlog_stat_tx_lost",
    "rlog_stat_rx_hwm",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", default="1025")
    args = ap.parse_args()

    gdb = shutil.which("riscv64-unknown-elf-gdb") or "riscv64-unknown-elf-gdb"
    ex = [gdb, "-q", "-batch", "-ex", f"target remote {args.host}:{args.port}",
          "-ex", "set confirm off", "-ex", "set pagination off",
          "-ex", "interrupt", "-ex", f"file {args.elf.replace(chr(92), '/')}"]
    for s in SYMS:
        ex += ["-ex", f"x/1wx &{s}"]
    ex += ["-ex", "detach", "-ex", "quit"]
    out = subprocess.run(ex, capture_output=True, text=True, timeout=120).stdout
    print(out.strip())
    vals = {}
    for ln in out.splitlines():
        m = re.match(r"0x[0-9a-f]+ <(\w+)>:\s+0x([0-9a-fA-F]+)", ln.strip())
        if m:
            vals[m.group(1)] = int(m.group(2), 16)
    if not vals:
        print("ERROR: no symbols read (ELF/符号名不匹配？)", file=sys.stderr)
        return 1
    print("\n--- 解读 ---")
    for k, v in vals.items():
        print(f"{k:28s} = {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
