#!/usr/bin/env python3
"""把 24 份 run_test.py 的某个函数按**语义规范化**（ast.unparse + 已知等价惯用法归一）
后分组，并打印组间差异 —— 用于确定 L1 抽取时的"真正差异面"。

用法:
    python scripts/normgroup.py <funcname> [<funcname> ...]

⚠️ 2026-09-24 由 `tmp/a2/normgroup.py` 迁来：本脚本被耐久文档当**离线工具**点名（`FRAMEWORK_MAP:90`：离线部分只有 `equiv.py`（静态等价）/ `l1survey.py`（现状盘点）等），却住在会被清理的 `tmp/` 里 —— 一旦清理就**静默失去该工具**。
适配：仅根目录推导 `parents[2]` → `parents[1]`（脚本少了一层 `tmp/a2/`）；其余照搬。
"""
import ast
import difflib
import hashlib
import re
import sys
from pathlib import Path

# ⚠️ 本仓默认 stdout 编码是 gbk/cp936：下面 `»»` 等分隔符（且归一后仍可能带出源码里的
# 非 GBK 字符）在**重定向/管道/被捕获时必抛 UnicodeEncodeError**（交互式控制台走
# WriteConsoleW 不受影响 ⇒ 只在自动化里炸，最难发现，且"有差异时"才触发）。
# 同族见 KNOWN_ISSUES #91。
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
PROJ = ROOT / "projects"

# 已知的"同一语义的不同写法" —— 归一后应分组到一起
EQUIV = [
    (r'SAMPLE_ROOT / "tests"', "TESTS_DIR"),
    (r'\(SAMPLE_ROOT / "tests"\)', "TESTS_DIR"),
    (r'SAMPLE_ROOT\.parents\[1\] / "reports"', "REPORTS_ROOT"),
    (r'Build = _find_git_bash\(\)', "BASH = _find_git_bash()"),
    (r'Bash = _find_git_bash\(\)', "BASH = _find_git_bash()"),
]


def norm(node, src):
    """去 docstring + ast.unparse + 归一化已知等价写法。"""
    body = list(node.body)
    if body and isinstance(body[0], ast.Expr) and isinstance(body[0].value, ast.Constant) \
            and isinstance(body[0].value.value, str):
        body = body[1:]
    clone = ast.Module(body=body, type_ignores=[])
    txt = ast.unparse(ast.fix_missing_locations(clone))
    for pat, rep in EQUIV:
        txt = re.sub(pat, rep, txt)
    return txt


def extract(path):
    tree = ast.parse(path.read_text(encoding="utf-8"))
    out = {}
    for n in tree.body:
        if isinstance(n, (ast.FunctionDef, ast.ClassDef)):
            if isinstance(n, ast.ClassDef):
                for m in n.body:
                    if isinstance(m, ast.FunctionDef):
                        out[f"{n.name}.{m.name}"] = norm(m, None)
            else:
                out[n.name] = norm(n, None)
    return out


def main():
    names = sys.argv[1:]
    per = {p.parent.parent.name: extract(p) for p in sorted(PROJ.glob("*/tests/run_test.py"))}
    for name in names:
        groups = {}
        for proj, fns in per.items():
            if name in fns:
                key = hashlib.md5(fns[name].encode()).hexdigest()[:8]
                groups.setdefault(key, []).append((proj, fns[name]))
        print(f"\n{'#'*72}\n### {name} — {len(groups)} 组 / "
              f"{sum(len(v) for v in groups.values())} 份\n{'#'*72}")
        groups = dict(sorted(groups.items(), key=lambda kv: -len(kv[1])))
        reps = []
        for k, v in groups.items():
            print(f"\n-- {k} ({len(v)}): {', '.join(p for p, _ in v)}")
            reps.append((k, v[0][0], v[0][1]))
        base = reps[0][1] if reps else None
        for k, proj, txt in reps[1:]:
            print(f"\n  »» diff {base} -> {proj} ({k})")
            d = difflib.unified_diff(txt.splitlines(), txt.splitlines(),
                                     lineterm="", n=1)
            # 逐组与基组比较
            d = difflib.unified_diff(reps[0][2].splitlines(), txt.splitlines(),
                                     fromfile=base, tofile=proj, lineterm="")
            for line in d:
                print("     " + line)


if __name__ == "__main__":
    main()
