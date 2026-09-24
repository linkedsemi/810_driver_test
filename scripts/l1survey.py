#!/usr/bin/env python3
"""A2 现状盘点：24 份 run_test.py 的 L1 相关函数，按 AST 函数体 md5 分组。

不做任何判定，只把"同一名字的函数在 24 份里有几种实现、分别是哪些工程"列出来，
作为 L1 抽取的粒度依据。

⚠️ 2026-09-24 由 `tmp/a2/l1survey.py` 迁来：本脚本被耐久文档当**离线工具**点名（`FRAMEWORK_MAP:90`：离线部分只有 `equiv.py`（静态等价）/ `l1survey.py`（现状盘点）等），却住在会被清理的 `tmp/` 里 —— 一旦清理就**静默失去该工具**。
适配：仅根目录推导 `parents[2]` → `parents[1]`（脚本少了一层 `tmp/a2/`）；其余照搬。
"""
import ast
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]          # 810_driver_test/
PROJ = ROOT / "projects"

# L1（采集层）相关的定义名：串口发现/采集、gdb 烧录、进程管理、环境自愈
L1_NAMES = {
    "SerialCapture", "reader", "_reader", "_drain_stdout", "gdb_reader",
    "discover_ports", "list_serial_ports", "_find_tgt_uart",
    "gdb_flash_once", "capture_case", "run_case", "run_pass", "start_capture",
    "build_case", "memory_backfill",
    "_spawn_test_run", "_kill_proc_group",
    "_restart_debugserver", "restart_debugserver",
    "_clean_gdb_residue", "clean_gdb_residue",
    "_find_git_bash", "_netstat_1025", "_kill_residual_gdb",
    "_probe_gdb_alive", "_own_port",
    "flush_stale", "wait_load_done", "_has_completion_marker",
    "_has_boot", "_has_any_output", "quit_clean", "gdb_send",
    "main", "l2_spec",
}


def funcs(path):
    """name -> md5(ast.dump)，含嵌套函数（用 Class.method 限定名）。"""
    tree = ast.parse(path.read_text(encoding="utf-8"))
    out = {}

    def walk(node, prefix=""):
        for n in node.body:
            if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                key = prefix + n.name
                if isinstance(n, ast.ClassDef):
                    walk(n, key + ".")
                else:
                    out[key] = hashlib.md5(
                        ast.dump(n, annotate_fields=True).encode()).hexdigest()[:8]
    walk(tree)
    return out


def main():
    projects = sorted(p for p in PROJ.iterdir() if (p / "tests" / "run_test.py").exists())
    per = {}
    for p in projects:
        per[p.name] = funcs(p / "tests" / "run_test.py")

    # 只关心 L1 相关的名字（含 Class.method 形式）
    names = sorted({k for f in per.values() for k in f
                    if k in L1_NAMES or k.split(".")[0] in L1_NAMES})
    for name in names:
        impls = {}
        for proj, f in per.items():
            if name in f:
                impls.setdefault(f[name], []).append(proj)
        print(f"\n### {name}  —— {len(impls)} 种实现 / {sum(len(v) for v in impls.values())} 份")
        for h, ps in sorted(impls.items(), key=lambda kv: -len(kv[1])):
            print(f"  {h}  ({len(ps)}): {', '.join(ps)}")


if __name__ == "__main__":
    sys.exit(main())
