#!/usr/bin/env python3
"""防线：找出所有 `Popen(stdout=PIPE)` 却没有持续排空的调用点（B2-fix §1.5）。

规则（第 4 次同族缺陷之后立的门）：
  任何一个 `Popen(..., stdout=PIPE)`，**所在函数内**必须至少出现一次
  `.stdout`（drain 线程 / readline）或 `.communicate`。两者都没有 ⇒
  管道从 Popen 到收尾之间没人读 ⇒ Windows 匿名管道写满 ⇒ 子进程阻塞在写
  stdout（KNOWN_ISSUES #97 / #101 / O-22 同族）。

用法：python scripts/scan_pipes.py          # 退出码 = **LIVE 缺陷处数**（0 = 干净）
      python scripts/scan_pipes.py --all    # 连 tests/tmp/ 的非 live 遗留一起列

⚠️ 2026-09-24 由 `tmp/b2fix/scan_pipes.py` 迁来：本脚本被 3 份文档当**必做防线**点名
   （`qsh-debug` §7 / `qsh-test-gen` §2 / `SPEC_REFERENCE` §10.17），却住在会被清理的
   `tmp/` 里 —— 一旦清理就**静默失去防线**。迁入 `scripts/` 后随库长期可用。
   迁移时只改了一处：根目录由 `parents[2]`（`tmp/<子目录>/`）改为 `parents[1]`（`scripts/`）。
"""
import ast
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]      # scripts/ 的上一级 = 810_driver_test
GLOBS = ["common/**/*.py", "regression/**/*.py", "projects/*/tests/**/*.py"]
SKIP_PARTS = ("__pycache__", "/backup", "tmp/backup")


def enclosing(tree, lineno):
    best = None
    for n in ast.walk(tree):
        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef)):
            end = n.end_lineno or n.lineno
            if n.lineno <= lineno <= end and (best is None or n.lineno >= best.lineno):
                best = n
    return best


def main():
    show_all = "--all" in sys.argv
    files = set()
    for g in GLOBS:
        files.update(ROOT.glob(g))
    bad = []
    n_sites = 0
    for f in sorted(files):
        rel = f.relative_to(ROOT).as_posix()
        if "__pycache__" in rel or any(s in rel for s in SKIP_PARTS):
            continue
        dead_demo = "/tmp/" in rel or rel.startswith("tmp/")
        if dead_demo and not show_all:
            continue
        try:
            tree = ast.parse(f.read_text(encoding="utf-8", errors="replace"))
        except SyntaxError:
            continue
        for n in ast.walk(tree):
            if not isinstance(n, ast.Call):
                continue
            if getattr(n.func, "attr", None) != "Popen" and getattr(n.func, "id", None) != "Popen":
                continue
            kws = {k.arg for k in n.keywords if k.arg}
            if not any("PIPE" in ast.dump(k.value) for k in n.keywords
                       if k.arg == "stdout"):
                continue
            n_sites += 1
            fn = enclosing(tree, n.lineno)
            used = set()
            if fn is not None:
                for m in ast.walk(fn):
                    if isinstance(m, ast.Attribute):
                        used.add(m.attr)
            if "stdout" not in used and "communicate" not in used:
                bad.append((rel, n.lineno, fn.name if fn else "<module>",
                            "tmp/遗留" if dead_demo else "LIVE"))
    print(f"扫描 {len(files)} 个文件，stdout=PIPE 调用点 {n_sites} 处")
    if not bad:
        print("OK: 每个 PIPE 调用点所在函数内都有排空/communicate")
        return 0
    for rel, ln, fn, kind in bad:
        print(f"  [{kind}] {rel}:{ln}  in {fn}()  -- 无排空")
    n_live = sum(1 for b in bad if b[3] == "LIVE")
    print(f"共 {len(bad)} 处（其中 LIVE {n_live} 处）")
    return n_live


if __name__ == "__main__":
    sys.exit(main())
