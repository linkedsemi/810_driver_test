#!/usr/bin/env python3
"""Flash programmer CLI wrapper for QSH XIP firmware (CSKYFlashProgramerConsole).

Encapsulates the C-Sky flash programmer so XIP firmware can be programmed to /
read back from the on-board SPI flash from the command line:

    python flash_program.py --bin zephyr_flash.bin@0x08000000 --verify
    python flash_program.py --bin cpu1.bin@0x08000000 --bin cpu2.bin@0x08100000 --erase 0
    python flash_program.py --dump-out readback.bin --dump-addr 0x08000000 --dump-len 0x1000

The console exe takes ONE JSON config describing the whole flash session; this
script generates it (defaults mirror the verified GUI config in
userconfig/default: UseDDC=false, ICECLk=1200, EraseType=0, ConnectType=0,
Timeout=20, ResetType=1), runs the exe and reports per-object FP_OK/FP_FAIL.

Environment overrides (test_common.sh style):
    FP_EXE   CSKYFlashProgramerConsole.exe path
             (default: bundled copy in tools/flash_program/,
             fallback C:/user_file/EC_FLASH_PROGRAM/FlashProgrammer/Bins/CSKYFlashProgramerConsole.exe)
    FP_ALGO  flash algorithm ELF path
             (default: tools/flash_program/qsh_flash_algo.elf,
             fallback C:/user_file/EC_FLASH_PROGRAM/qsh_flash_algo.elf)
    FP_LOG   session log path (default flash-programer.log next to FP_EXE)

Known traps (see 810_driver_test/CLAUDE.md §10.x / §15):
  * The tool owns the CKLink EXCLUSIVELY while running; the XuanTie
    DebugServer (127.0.0.1:1025) must NOT be connected during flashing, or the
    tool fails with "CKLINK may be used by other program". We warn when
    DebugServer/gdb processes are alive; --kill-debugserver force-kills them.
  * Dual-core images must be programmed in ONE session (multiple --bin):
    EraseType=0 wipes the whole chip before programming, so two separate calls
    would erase the first core's image (use --erase 2 range-erase instead).
  * Session logs append to flash-programer.log; verdict is dual-channel:
    return code + stdout/log-tail keywords (§15).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# ---- defaults (overridable via env, see header) -----------------------------
# 工程内打包副本优先（810_driver_test/tools/flash_program/，自包含可拷贝使用）；
# 外部 C:\user_file\... 作为回退（旧环境/工具已更新到外部时）。
TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools" / "flash_program"
DEFAULT_EXE = TOOLS_DIR / "CSKYFlashProgramerConsole.exe"
if not DEFAULT_EXE.exists():
    DEFAULT_EXE = Path("C:/user_file/EC_FLASH_PROGRAM/FlashProgrammer/Bins/CSKYFlashProgramerConsole.exe")
DEFAULT_ALGO = TOOLS_DIR / "qsh_flash_algo.elf"
if not DEFAULT_ALGO.exists():
    DEFAULT_ALGO = Path("C:/user_file/EC_FLASH_PROGRAM/qsh_flash_algo.elf")
DEFAULT_ADDR = 0x08000000  # QSH XIP flash base, cpu1

FP_EXE = os.environ.get("FP_EXE", str(DEFAULT_EXE))
FP_ALGO = os.environ.get("FP_ALGO", str(DEFAULT_ALGO))
FP_LOG = os.environ.get("FP_LOG", str(Path(FP_EXE).parent / "flash-programer.log"))

# CLI failure keywords (dual channel; refine after on-target test, §15)
FAIL_WORDS = ("fail", "error", "exception", "cannot", "unable", "timeout")


def _warn_active_peers(kill: bool):
    """Warn (or kill) processes that hold the CKLink: DebugServer / gdb.

    The flash programmer needs exclusive CKLink access; an alive DebugServer
    makes it fail with "CKLINK may be used by other program". We do NOT kill
    silently - flashing is rare and a running debug session is precious.
    """
    names = ("DebugServerConsole.exe", "riscv64-unknown-elf-gdb.exe", "gdb.exe")
    try:
        out = subprocess.run(["tasklist", "/FO", "CSV"], capture_output=True,
                             text=True, encoding="utf-8", errors="replace").stdout
    except OSError:
        return  # tasklist unavailable; proceed
    alive = [n for n in names if n in out]
    if not alive:
        return
    if kill:
        print(f"[fp] killing CKLink holders: {', '.join(alive)}", flush=True)
        for n in alive:
            subprocess.run(["taskkill", "/F", "/IM", n],
                           capture_output=True, text=True)
        return
    print(f"[fp] WARNING: {', '.join(alive)} still running - DebugServer holds "
          f"the CKLink and the flash programmer will likely fail "
          f"(\"CKLINK may be used by other program\").", flush=True)
    print(f"[fp]         pass --kill-debugserver to force-kill them first.", flush=True)


def _session_log_tail():
    """Return the flash-programer.log content appended since the last call
    (sessions append to this file; we diff by byte offset)."""
    path = Path(FP_LOG)
    tail = getattr(_session_log_tail, "_offset", None)
    if not path.exists():
        return ""
    size = path.stat().st_size
    if tail is None:
        _session_log_tail._offset = size
        return ""
    if size <= tail:
        _session_log_tail._offset = size
        return ""
    with open(path, "rb") as f:
        f.seek(tail)
        data = f.read(size - tail)
    _session_log_tail._offset = size
    return data.decode("utf-8", "replace")


def _cfg_path():
    """Config JSON goes to the system temp dir (not the workspace)."""
    return Path(tempfile.gettempdir()) / "flash_program_cfg.json"


def _build_config(bins, erase, verify, timeout, reset_and_run, algo,
                  dump_out, dump_addr, dump_len):
    objs = []
    for path, addr in bins:
        objs.append({
            "Type": 2,                      # bin file
            "Program": True,
            "Verify": verify,
            "TargetAddr": addr,
            "FilePath": Path(path).as_posix(),
        })
    cfg = {
        "DebugServerSetting": {
            "UseDDC": False,
            "EnableTRST": False,
            "ICECLk": 1200,
            "DelayMTCR": 20,
        },
        "FlashProgramSession": {
            "CommandType": 1 if dump_out else 0,   # 1 = read back
            "TargetConfig": {
                "AlgorithmFile": algo,
                "EraseType": erase,
                "EraseRangStart": 0,
                "EraseRangLength": 4096,
                "ConnectType": 0,
                "Timeout": timeout,
                "ResetAndRun": reset_and_run,
                "ResetType": 1,
                "ResetValue": 0,
                "RunMode": False,
            },
            "TargetFile": {
                "DumpFileType": 1 if dump_out else 0,
                # 帮助注释有笔误（DumpBin/DumpHex 对应关系反了），两个都填同一
                # 路径，无论 CLI 用哪个字段都能工作（实测后精简）。
                "DumpBinFileInfo": {
                    "FilePath": Path(dump_out).as_posix() if dump_out else "",
                    "StartAddress": dump_addr,
                    "Length": dump_len,
                },
                "DumpHexFileInfo": {
                    "FilePath": Path(dump_out).as_posix() if dump_out else "",
                    "StartAddress": dump_addr,
                    "Length": dump_len,
                },
            },
            "TargetObjectArray": objs,
        },
    }
    return cfg


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", action="append", metavar="FILE[@ADDR]",
                    help="bin to program; @ADDR (hex) optional, default "
                         "0x08000000. Repeat for multiple objects (one session).")
    ap.add_argument("--addr", default=None, metavar="0xADDR",
                    help="default target address for --bin without @ADDR")
    ap.add_argument("--verify", action="store_true", help="verify after program")
    ap.add_argument("--erase", type=int, choices=[0, 1, 2, 3], default=0,
                    help="0=chip erase (default), 1=sectors, 2=range, 3=no erase")
    ap.add_argument("--algo", default=FP_ALGO, help="flash algorithm ELF")
    ap.add_argument("--timeout", type=int, default=20, help="per-session timeout s")
    ap.add_argument("--reset-and-run", action="store_true",
                    help="reset the board and run from flash after programming")
    ap.add_argument("--cklink", default=None, help="CKLink hardware ID (-c)")
    ap.add_argument("--kill-debugserver", action="store_true",
                    help="taskkill DebugServer/gdb before flashing (CKLink exclusive)")
    ap.add_argument("--dump-out", default=None, help="read back to this file")
    ap.add_argument("--dump-addr", default=None, metavar="0xADDR",
                    help="read back start address (default 0x08000000)")
    ap.add_argument("--dump-len", type=lambda s: int(s, 0), default=0x1000,
                    help="read back length in bytes (default 4096)")
    ap.add_argument("--compare", default=None,
                    help="after --dump-out, compare dumped file to this file")
    ap.add_argument("--raw", action="store_true", help="print full CLI output")
    args = ap.parse_args()

    # ---- validate -----------------------------------------------------------
    if args.dump_out:
        if args.bin:
            ap.error("--dump-out is exclusive with --bin (read back only)")
        addr = int(args.dump_addr, 0) if args.dump_addr else DEFAULT_ADDR
        return _do_dump(args, addr)

    if not args.bin:
        ap.error("nothing to do: give --bin FILE[@ADDR] or --dump-out FILE")

    default_addr = int(args.addr, 0) if args.addr else DEFAULT_ADDR
    bins = []
    for b in args.bin:
        if "@" in b:
            path, _, a = b.rpartition("@")
            bins.append((path, int(a, 0)))
        else:
            bins.append((b, default_addr))
    for path, _ in bins:
        if not Path(path).exists():
            sys.exit(f"ERROR: bin not found: {path}")
    if not Path(args.algo).exists():
        sys.exit(f"ERROR: algorithm ELF not found: {args.algo}")
    if not Path(FP_EXE).exists():
        sys.exit(f"ERROR: flash programmer exe not found: {FP_EXE}")

    _warn_active_peers(args.kill_debugserver)
    return _do_run(args, bins)


def _do_run(args, bins):
    cfg = _build_config(bins, args.erase, args.verify, args.timeout,
                        args.reset_and_run, args.algo, None, 0, 0)
    cfg_path = _cfg_path()
    with open(cfg_path, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
    print(f"[fp] config: {cfg_path}", flush=True)

    cmd = [FP_EXE, "-f", str(cfg_path)]
    if args.cklink:
        cmd += ["-c", args.cklink]
    print(f"[fp] running: {' '.join(cmd)}", flush=True)

    _session_log_tail()  # mark current log end before the session
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=args.timeout + 60)
    out = proc.stdout + proc.stderr
    if args.raw:
        print(out, flush=True)

    log_tail = _session_log_tail()

    # ---- verdict (dual channel: return code + stdout/log keywords) ----------
    fail = proc.returncode != 0
    for chunk in (out.lower(), log_tail.lower()):
        for w in FAIL_WORDS:
            if w in chunk:
                fail = True
                break
    if fail:
        print(f"[fp] FP_FAIL: returncode={proc.returncode}", flush=True)
        if out.strip():
            print(out[-2000:], flush=True)
        if log_tail:
            print("[fp] session log tail:", flush=True)
            print(log_tail[-2000:], flush=True)
        sys.exit(1)
    for path, addr in bins:
        print(f"[fp] FP_OK: {Path(path).name}@{hex(addr)} "
              f"(erase={args.erase}, verify={'y' if args.verify else 'n'})", flush=True)
    return 0


def _do_dump(args, addr):
    cfg = _build_config([], 0, False, args.timeout, False, args.algo,
                        args.dump_out, addr, args.dump_len)
    cfg_path = _cfg_path()
    with open(cfg_path, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)

    cmd = [FP_EXE, "-f", str(cfg_path)]
    if args.cklink:
        cmd += ["-c", args.cklink]
    print(f"[fp] reading flash {hex(addr)} +{args.dump_len} -> {args.dump_out}", flush=True)

    _session_log_tail()
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=args.timeout + 60)
    out = proc.stdout + proc.stderr
    if args.raw:
        print(out, flush=True)

    log_tail = _session_log_tail()
    fail = proc.returncode != 0
    for chunk in (out.lower(), log_tail.lower()):
        for w in FAIL_WORDS:
            if w in chunk:
                fail = True
                break
    if fail:
        print(f"[fp] FP_FAIL (dump): returncode={proc.returncode}", flush=True)
        if out.strip():
            print(out[-2000:], flush=True)
        if log_tail:
            print("[fp] session log tail:", flush=True)
            print(log_tail[-2000:], flush=True)
        sys.exit(1)

    dump = Path(args.dump_out)
    if not dump.exists() or dump.stat().st_size == 0:
        print(f"[fp] FP_FAIL (dump): file missing/empty: {args.dump_out}", flush=True)
        sys.exit(1)
    size = dump.stat().st_size
    print(f"[fp] FP_OK: dumped {size} bytes to {args.dump_out}", flush=True)

    if args.compare:
        ref = Path(args.compare)
        if not ref.exists():
            print(f"[fp] WARNING: --compare file missing: {ref}", flush=True)
            return 1
        a = dump.read_bytes()[:size]
        b = ref.read_bytes()[:size]
        if a == b:
            print(f"[fp] FP_OK: dump matches {ref.name}", flush=True)
        else:
            print(f"[fp] FP_FAIL: dump MISMATCH vs {ref.name} "
                  f"({size} bytes)", flush=True)
            sys.exit(1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
