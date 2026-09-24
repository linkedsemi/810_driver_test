#!/usr/bin/env python3
"""按名字 dump run_test.py 里的函数源码（供逐字搬运时对照）。

用法:
    python scripts/dump.py <project> [func ...]      # 不给 func 则 dump 全部 L1 相关
    python scripts/dump.py --all [func ...]          # 所有工程（用于比对）

⚠️ 2026-09-24 由 `tmp/a2/dump.py` 迁来：本脚本被耐久文档当**离线工具**点名（`FRAMEWORK_MAP:90`：离线部分只有 `equiv.py`（静态等价）/ `l1survey.py`（现状盘点）等），却住在会被清理的 `tmp/` 里 —— 一旦清理就**静默失去该工具**。
适配：仅根目录推导 `parents[2]` → `parents[1]`（脚本少了一层 `tmp/a2/`）；其余照搬。
"""
import ast
import sys
from pathlib import Path

# ⚠️ 本仓默认 stdout 编码是 gbk/cp936：本脚本会 **dump 出源码原文**，而源码里含 `⇒` 等
# 非 GBK 字符 ⇒ **重定向/管道/被捕获时必抛 UnicodeEncodeError**（交互式控制台走
# WriteConsoleW 不受影响 ⇒ 只在自动化里炸，最难发现）。同族见 KNOWN_ISSUES #91。
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
PROJ = ROOT / "projects"

L1 = {
    "log", "SerialCapture", "reader", "discover_ports", "list_serial_ports",
    "_find_tgt_uart", "gdb_flash_once", "capture_case", "run_case", "run_pass",
    "build_case", "memory_backfill", "dump_mem", "_spawn_test_run",
    "_kill_proc_group", "_restart_debugserver", "restart_debugserver",
    "_clean_gdb_residue", "clean_gdb_residue", "_find_git_bash",
    "_netstat_1025", "_kill_residual_gdb", "_probe_gdb_alive", "preflight_gdb",
    "_own_port", "_wait_continue", "run_with_timeout", "run_until_done",
    "find_cases", "list_serial_ports", "_split_lines", "_core_view",
}


def dump(path, want):
    tree = ast.parse(path.read_text(encoding="utf-8"))
    for n in tree.body:
        if isinstance(n, (ast.FunctionDef, ast.ClassDef)) and (not want or n.name in want):
            print(f"\n# ===== {n.name}  ({path.name}:{n.lineno}) =====")
            print(ast.get_source_segment(path.read_text(encoding="utf-8"), n))


def main():
    argv = sys.argv[1:]
    allp = "--all" in argv
    argv = [a for a in argv if a != "--all"]
    want = set(argv[1:]) if not allp else set(argv)
    targets = (sorted(PROJ.glob("*/tests/run_test.py")) if allp
               else [PROJ / argv[0] / "tests" / "run_test.py"])
    if not allp:
        want = set(argv[1:]) or L1
    for t in targets:
        if allp:
            print(f"\n{'='*70}\n### {t.parent.parent.name}\n{'='*70}")
        dump(t, want)


if __name__ == "__main__":
    main()
