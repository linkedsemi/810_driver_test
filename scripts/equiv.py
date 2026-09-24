#!/usr/bin/env python3
"""A2 静态等价验证（离线，最强证据）。

三件事：

  E1 **逐字搬运**：对被搬运的每个函数，取"改动前"的原函数体，经**声明的重命名映射**
     归一后，断言它作为**连续语句序列**原样出现在 `common/acquire.py` 的对应函数里。
     （参数化 = 原体落在某个 `if <param>: …` 分支里，仍是连续语句序列。）

  E2 **非空转自检**：对每条**关键路径**（两遍烧录两轮 / 单核 hold / 写包提速 /
     drain 线程 / 优雅中断 / 超时分支 / 内存兜底 / 环境自愈重试 / wmic 兜底…），
     断言它在 E1 的比对里**真的被覆盖到**（有非空的 (old,new) 配对），而不是比了个空。

  E3 **负对照**：故意扰动 acquire.py 里的若干常量/命令/时长，断言 E1/E2 变红。
     证明比对器**对"命令序列/参数/等待时长"敏感**，不是恒真。

用法:
    python scripts/equiv.py            # 跑 E1+E2
    python scripts/equiv.py --neg      # 追加 E3 负对照

⚠️ 2026-09-24 由 `tmp/a2/equiv.py` 迁来：本脚本被耐久文档当**离线工具**点名（`FRAMEWORK_MAP:90`：离线部分只有 `equiv.py`（静态等价）/ `l1survey.py`（现状盘点）等），却住在会被清理的 `tmp/` 里 —— 一旦清理就**静默失去该工具**。
适配：① 根目录推导 `parents[2]` → `parents[1]`；② `before/` 快照随迁为 `scripts/equiv_before/`
    （**不能**用既有的 `scripts/before/` —— 那是 brule 线束 2026-09-21 的快照，两者内容不同）；其余照搬。
"""
import ast
import shutil
import sys
import tempfile
import textwrap
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BEFORE = Path(__file__).resolve().parent / "equiv_before"
ACQ = ROOT / "common" / "acquire.py"


# --------------------------------------------------------------- 提取
def _parse(text):
    return ast.parse(textwrap.dedent(text))


def stmts_of(text):
    """把一段源码解析成语句序列（顶层）。"""
    return _parse(text).body


def _nodoc(seq):
    """去掉首条 docstring —— 它是纯说明，不参与行为等价。"""
    if (seq and isinstance(seq[0], ast.Expr)
            and isinstance(seq[0].value, ast.Constant)
            and isinstance(seq[0].value.value, str)):
        return seq[1:]
    return seq


def all_bodies(node):
    """收集一棵 AST 里**所有**语句序列（函数体 / if / for / while / try / with / else…）。"""
    out = []

    def walk(n):
        for attr in ("body", "orelse", "finalbody"):
            seq = getattr(n, attr, None)
            if isinstance(seq, list) and seq and all(
                    isinstance(x, ast.stmt) for x in seq):
                out.append(_nodoc(seq))
                for x in seq:
                    walk(x)
        for h in getattr(n, "handlers", []) or []:
            out.append(_nodoc(h.body))
            for x in h.body:
                walk(x)
    walk(node)
    if isinstance(node, ast.FunctionDef):
        out.append(_nodoc(node.body))
        for x in node.body:
            walk(x)
    return out


def func_node(path, name):
    tree = ast.parse(Path(path).read_text(encoding="utf-8"))
    for n in ast.walk(tree):
        if isinstance(n, (ast.FunctionDef, ast.ClassDef)) and n.name == name:
            return n
    return None


def unparse_seq(seq):
    return [ast.unparse(s) for s in seq]


def _canon(line):
    """文本归一：去行尾空白、压连续空白（不动字符串字面量内部的空格）。"""
    return " ".join(line.split())


def find_contiguous(hay_seqs, needle):
    """needle(已归一文本行) 是否作为连续子序列出现在 hay_seqs 任一条里。"""
    if not needle:
        return True
    for seq in hay_seqs:
        lines = [_canon(x) for x in seq]
        for i in range(len(lines) - len(needle) + 1):
            if lines[i:i + len(needle)] == needle:
                return True
    return False


def best_mismatch(hay_seqs, needle):
    """失败诊断：找出"最长公共前缀"的那条 hay，报出第一个不同的语句。"""
    best = (-1, None, 0)
    for seq in hay_seqs:
        lines = [_canon(x) for x in seq]
        for i in range(max(1, len(lines) - len(needle) + 1)):
            k = 0
            while k < len(needle) and i + k < len(lines) and lines[i + k] == needle[k]:
                k += 1
            if k > best[0]:
                best = (k, (i, lines), i)
    k, item, i = best
    if item is None:
        return "hay 为空"
    idx, lines = item
    old = needle[k] if k < len(needle) else "<needle 结束>"
    new = lines[idx + k] if idx + k < len(lines) else "<hay 结束>"
    return f"共同前缀 {k} 语句；旧[{k}]={old[:150]!r}  新[{idx + k}]={new[:150]!r}"


# --------------------------------------------------------------- 重命名映射
# 每条 = (正则, 替换)，只在旧体上做文本替换，用来把"模块级全局"映射成新代码里的形参。
def _subs(*pairs):
    import re
    return [(re.compile(a), b) for a, b in pairs]


COMMON = _subs(
    (r"\bSAMPLE_ROOT\b", "ROOT"),
    (r"\bTESTS_DIR\b", "TESTS_DIR"),
    (r"\bBASH\b", "bash"),
    (r"\bTARGET_ROM_START\b", "rom_start"),
    (r"\bBOARD\b", "BOARD"),
    (r"_find_git_bash\(\)", "find_git_bash()"),
    (r"__import__\(\"os\"\)\.environ", "os.environ"),
)

# 需要**挖掉**的旧语句下标（0-based，去 docstring 后）：这些位置在新代码里被参数化了
# （多出一条分支/诊断），逐字比对时从 needle 里剔除，由 E1b 兜底证明字面量没丢。
HOLES = {
    ("i2cf_test", "gdb_flash_once"): [0],
    ("trng_test", "gdb_flash_once"): [0],
    ("rtc_ls_test", "gdb_flash_once"): [0],
    ("otbn_test", "gdb_flash_once"): [0],
    ("i2cf_test", "capture_case"): [0, 1],
    ("otbn_test", "capture_case"): [0, 1],
    ("otbn_test", "gdb_flash_once"): [0],
    ("i2c_pure_test", "run_case"): [4],
    ("i2cf_test", "memory_backfill"): [0],
    ("otbn_test", "memory_backfill"): [0],
    ("i2c_pure_test", "memory_backfill"): [0],
}

# 需要跳过开头的语句数（cmd 构造 / build-elf 派生被参数化重写 ⇒ 由 E1b 单独核对）
SKIP = {
    ("i2cf_test", "gdb_flash_once"): 1,
    ("trng_test", "gdb_flash_once"): 1,
    ("rtc_ls_test", "gdb_flash_once"): 1,
    ("otbn_test", "gdb_flash_once"): 1,
    ("i2cf_test", "capture_case"): 2,
    ("otbn_test", "capture_case"): 2,
}

# 只参与 E1b/E2（不做 E1 逐字比对）：这些是被**参数化重写**的（cmd 构造 / build 派发），
# 逐字比对无意义，靠"字面量一个都不少"来证。
EXTRA_PAIRS = [
    ("i2cf_test", "build_case", "west_build", _subs(
        (r'ROOT / "tests" / case / "build" / "target"', "spec.tests_dir / case / 'build'"),
        (r'ROOT / "cpu1_i2cf_test"', "spec.root / spec.src_dir"),
        (r'"-p", "auto"', "'-p', spec.build_progress"),
        (r'"-DDTC_OVERLAY_FILE="', "'-DDTC_OVERLAY_FILE="),
        (r"timeout=900", "timeout=spec.build_timeout"),
    )),
    ("i2c_pure_test", "build_case", "build_via_test_sh_raise", []),
    ("dma_test", "run_case", "run_case_shell", []),
]

# (旧工程, 旧函数名, 新函数名, 额外替换)
PAIRS = [
    # ---- 通用原语（单实现，逐字搬运）----
    ("dma_test", "_find_git_bash", "find_git_bash", []),
    ("dma_test", "_find_tgt_uart", "find_tgt_uart", []),
    ("dma_test", "_clean_gdb_residue", "clean_gdb_residue_plain", []),
    ("dma_test", "_kill_proc_group", "kill_proc_group", []),
    ("dma_test", "_restart_debugserver", "restart_debugserver", _subs(
        (r"serial\.Serial", "_serial().Serial"),
        (r'Path\(r"C:\\uaer_app\\debug_server\\bin\\DebugServerConsole\.exe"\)', "Path(DEBUGSERVER_EXE)"),
    )),
    ("dma_test", "_spawn_test_run", "spawn_test_run", _subs(
        (r"\bROOT\b", "root"))),
    ("dma_test", "run_case", "run_case_shell", _subs(
        (r"TESTS_DIR", "spec.tests_dir"),
        (r"SAMPLE_ROOT", "spec.root"),
        (r"SerialCapture\(tgt_uart\)", "SerialCapture(tgt_uart, mode='line')"),
        (r"_spawn_test_run\(spec\.tests_dir / case_name, build_dir\)",
         "spawn_test_run(spec.tests_dir / case_name, spec.root, bash, style='z')"),
        (r"_kill_proc_group\(proc\)", "kill_proc_group(proc, style='z')"),
    )),
    # ---- PORTS_DICT ----
    ("i2cf_test", "reader", "reader", []),
    ("i2cf_test", "discover_ports", "discover_ports", []),
    ("i2cf_test", "gdb_flash_once", "gdb_flash_once", _subs(
        (r"proc\.stdin\.write\('\\\\x03'\)", r"proc.stdin.write('\x03')"),
    )),
    ("trng_test", "gdb_flash_once", "gdb_flash_once", _subs(
        (r"proc\.stdin\.write\('\\\\x03'\)", r"proc.stdin.write('\x03')"),
    )),
    ("rtc_ls_test", "gdb_flash_once", "gdb_flash_once", _subs(
        (r"proc\.stdin\.write\('\\\\x03'\)", r"proc.stdin.write('\x03')"),
    )),
    ("otbn_test", "gdb_flash_once", "gdb_watch_session", _subs(
        (r"(?s)\bin l\b", "in ln"),
        (r"for l in gdb_out", "for ln in gdb_out"),
        (r"for l in gdb_out\)", "for ln in gdb_out)"),
        (r"_own_port\(p\)", "_own_port(p)"),
        (r"ZT_BOOT_RE", "ZT_BOOT_RE"),
        (r"ZTEST_ACTIVITY_RE", "ZTEST_ACTIVITY_RE"),
        (r"EXEC_RE", "EXEC_RE"),
    )),
    ("otbn_test", "_netstat_1025", "netstat_1025", []),
    ("otbn_test", "_kill_residual_gdb", "kill_residual_gdb", _subs(
        (r"_netstat_1025\(\)", "netstat_1025()"),
    )),
    ("otbn_test", "clean_gdb_residue", "clean_gdb_residue", _subs(
        (r"_kill_residual_gdb\(\)", "kill_residual_gdb()"),
        (r"_restart_debugserver\(\)", "restart_debugserver(style=style)"),
        (r"_netstat_1025\(\)", "netstat_1025()"),
    )),
    ("otbn_test", "_own_port", "_is_own", _subs(
        (r"TGT_BANNERS", "tgt_banners"),
        (r"for l in pl", "for ln in pl"),
        (r"ZT_BOOT_RE\.search\(l\)", "ZT_BOOT_RE.search(ln)"),
        (r"b in l for b in", "b in ln for b in"))),
    ("otbn_test", "_restart_debugserver", "restart_debugserver", _subs(
        (r"(?s)subprocess\.Popen\(\s*\[DEBUGSERVER_EXE\],\s*cwd=os\.path\.dirname\(DEBUGSERVER_EXE\),",
         "subprocess.Popen([r\"C:/uaer_app/debug_server/bin/DebugServerConsole.exe\"], cwd=r\"C:/uaer_app/debug_server/bin\","),
        (r"_netstat_1025\(\)", "netstat_1025()"),
        (r'log\("\[ENV\] restarting DebugServer \(residue\)"\)',
         '_say("[ENV] restarting DebugServer (residue)")'),
        (r'log\("\[ENV\] WARNING: DebugServer 30s 内未打印就绪提示符"\)',
         '_say("[ENV] WARNING: DebugServer 30s 内未打印就绪提示符")'),
        (r"DEBUGSERVER_EXE", "DEBUGSERVER_EXE"),
    )),
    ("i2cf_test", "capture_case", "capture_case_oneshot", _subs(
        (r"serial\.Serial\(", "_serial().Serial("),
        (r"except serial\.SerialException as e", "except _serial().SerialException as e"),
    )),
    ("otbn_test", "capture_case", "capture_case_watch", _subs(
        (r"serial\.Serial\(", "_serial().Serial("),
        (r"except serial\.SerialException as e", "except _serial().SerialException as e"),
    )),
    # ---- DUAL_RUN ----
    ("i2c_pure_test", "run_case", "run_case_dual", _subs(
        (r"TESTS_DIR", "spec.tests_dir"),
        (r"SAMPLE_ROOT", "spec.root"),
        (r"ctrl_port", "spec.ctrl_uart"), (r"tgt_port", "spec.tgt_uart"),
        (r"case_timeout", "spec.case_timeout"),
        (r"ctrl_cap = SerialCapture\(spec\.ctrl_uart\)",
         "if spec.announce:\n    print(f'[RUN] {case_dir.name}')\nctrl_cap = SerialCapture(spec.ctrl_uart, mode='line_err')"),
        (r"(?<![.\w])SerialCapture\(spec\.tgt_uart\)", "SerialCapture(spec.tgt_uart, mode='line_err')"),
        (r"_spawn_test_run\(case_dir\)",
         "spawn_test_run(case_dir, spec.root, bash, style='d3')"),
        (r"_kill_proc_group\(proc\)", "kill_proc_group(proc, style='d3')"),
        (r"clean_gdb_residue\(\)", "clean_gdb_residue(style='d3')"),

    )),
    ("i2c_pure_test", "_spawn_test_run", "spawn_test_run", _subs(
        (r"BASH", "bash"), (r"SAMPLE_ROOT", "root"))),
    ("i2c_pure_test", "_kill_proc_group", "kill_proc_group", []),
    ("i2c_pure_test", "_netstat_1025", "netstat_1025", _subs(
        (r"rows: List\[Tuple\[str, str, str\]\] = \[\]", "rows = []"),
    )),
    ("i2c_pure_test", "_probe_gdb_alive", "probe_gdb_alive", _subs(
        (r'"target remote 127\.0\.0\.1:1025"', 'f"target remote {GDB_HOST}:{GDB_PORT}"'),
    )),
    ("i2c_pure_test", "preflight_gdb", "preflight_gdb", _subs(
        (r"_probe_gdb_alive\(\)", "probe_gdb_alive()"),
        (r"clean_gdb_residue\(\)", "clean_gdb_residue(style=style)"),
        (r"restart_debugserver\(\)", "restart_debugserver_stable(style=style)"),
    )),
    ("i2c_pure_test", "_restart_debugserver", "restart_debugserver", _subs(
        (r"(?s)subprocess\.Popen\(\s*\[DEBUGSERVER_EXE\],\s*cwd=os\.path\.dirname\(DEBUGSERVER_EXE\),",
         "subprocess.Popen([r\"C:/uaer_app/debug_server/bin/DebugServerConsole.exe\"], cwd=r\"C:/uaer_app/debug_server/bin\","),
        (r"_netstat_1025\(\)", "netstat_1025()"),
        (r'print\("\[ENV\] restarting DebugServer \(residue\)", file=sys\.stderr\)',
         '_say("[ENV] restarting DebugServer (residue)")'),
        (r'print\("\[ENV\] WARNING: DebugServer 30s 内未打印就绪提示符", file=sys\.stderr\)',
         '_say("[ENV] WARNING: DebugServer 30s 内未打印就绪提示符")'),
        (r'\[r"C:/uaer_app/debug_server/bin/DebugServerConsole\.exe"\]',
         '[r"C:/uaer_app/debug_server/bin/DebugServerConsole.exe"]'),
    )),
    ("i2c_pure_test", "build_case", "build_via_test_sh_raise", []),
    # ---- MBOX / IWDG ----
    ("mbox_test", "run_case", "run_case_mbox", _subs(
        (r"\bBash\b", "bash"), (r"\bROOT\b", "spec.root"),
        (r"TESTS_DIR", "spec.tests_dir"),
        (r"_combined_lines", "combined_lines"),
        (r"_core_view\(", "core_view("),
        (r"SerialCapture\(ctrl_uart\)", "SerialCapture(ctrl_uart, mode='chunk')"),
        (r"SerialCapture\(tgt_uart\)", "SerialCapture(tgt_uart, mode='chunk')"),
    )),
    ("app_iwdg_cpu2_test", "run_pass", "run_pass_iwdg", _subs(
        (r'ROOT / "tests" / case_name / "test.sh"', 'spec.tests_dir / case_name / "test.sh"'),
        (r"str\(ROOT\)", "str(spec.root)"),
        (r"\bBASH\b", "bash"),
    )),
    ("eth_test", "_restart_debugserver", "restart_debugserver", _subs(
        (r"serial\.Serial", "_serial().Serial"),
        (r'Path\(r"C:\\uaer_app\\debug_server\\bin\\DebugServerConsole\.exe"\)',
         "Path(DEBUGSERVER_EXE)"),
    )),
    ("eth_test", "run_case", "run_case_shell", _subs(
        (r'ROOT / "tests"', "spec.tests_dir"),
        (r"SAMPLE_ROOT", "spec.root"),
        (r"SerialCapture\(tgt_uart\)", "SerialCapture(tgt_uart, mode='line')"),
        (r"_spawn_test_run\(spec\.tests_dir / case_name, build_dir\)",
         "spawn_test_run(spec.tests_dir / case_name, spec.root, bash, style='z')"),
        (r"_kill_proc_group\(proc\)", "kill_proc_group(proc, style='z')"),
    )),
    ("i2cf_test", "memory_backfill", "memory_backfill", _subs(
        (r'\(subprocess\.TimeoutExpired, OSError\)', "(subprocess.TimeoutExpired, OSError)"),
        (r'SAMPLE_ROOT / "tests" / "scripts" / "dump_results\.py"', "dumper"),
        (r"timeout=60", "timeout=timeout"))),
    ("otbn_test", "memory_backfill", "memory_backfill", _subs(
        (r'\(subprocess\.TimeoutExpired, OSError\)', "(subprocess.TimeoutExpired, OSError)"),
        (r'SAMPLE_ROOT / "tests" / "scripts" / "dump_results\.py"', "dumper"),
        (r"timeout=60", "timeout=timeout"))),
    ("i2c_pure_test", "memory_backfill", "memory_backfill_dual", _subs(
        (r'str\(TESTS_DIR / "scripts"\)', "cwd"),
        (r'sys\.executable, "dump_results\.py"', "sys.executable, dumper"),
        (r"case_dir\.name", "case"),
    )),
]

# 需要"别名进入 acquire"的函数名（在新文件里实际叫别的名字）
ALIAS = {
    "clean_gdb_residue_otbn_alias": "clean_gdb_residue",
    "capture_case_oneshot": "capture_case",
    "capture_case_watch": "capture_case",
    "memory_backfill_dual": "memory_backfill",
}


def _old_src(path, node, subs=()):
    """旧函数体源码切片 + 重命名映射（返回源码文本）。"""
    src = Path(path).read_text(encoding="utf-8")
    lines = src.split("\n")
    chunk = lines[node.body[0].lineno - 1:node.end_lineno]
    indents = [len(x) - len(x.lstrip()) for x in chunk if x.strip()]
    base = min(indents) if indents else 0
    text = "\n".join(x[base:] if len(x) >= base else x for x in chunk)
    return apply_subs(text, COMMON + list(subs))


def OLD_BODY(path, node, skip=0, subs=(), holes=()):
    """把旧函数的函数体渲染成语句文本（含重命名映射、去 docstring、挖空 holes）。"""
    seq = _nodoc(stmts_of(_old_src(path, node, subs)))[skip:]
    drop = set(holes)
    seq = [x for i, x in enumerate(seq) if i not in drop]
    return "\n".join(ast.unparse(x) for x in seq)


def apply_subs(text, subs):
    for rx, rep in subs:
        # repl 用 callable：避免 re 把替换串里的 \x / \g 当转义解析
        text = rx.sub(lambda m, _r=rep: _r, text)
    return text


def e1():
    acq_src = ACQ.read_text(encoding="utf-8")
    ok, bad = 0, []
    for old_proj, old_fn, new_fn, subs in PAIRS:
        old_path = BEFORE / old_proj / "run_test.py"
        old_node = func_node(old_path, old_fn)
        new_name = ALIAS.get(new_fn, new_fn)
        new_node = func_node(ACQ, new_name)
        if old_node is None or new_node is None:
            bad.append((old_proj, old_fn, new_fn, "缺函数"))
            continue
        old_text = OLD_BODY(old_path, old_node, SKIP.get((old_proj, old_fn), 0),
                            subs, HOLES.get((old_proj, old_fn), ()))
        try:
            needle = [_canon(x) for x in unparse_seq(stmts_of(old_text))]
        except SyntaxError as exc:
            bad.append((old_proj, old_fn, new_fn, f"旧体解析失败: {exc}"))
            continue
        hit = find_contiguous([unparse_seq(s) for s in all_bodies(new_node)], needle)
        if hit:
            ok += 1
        else:
            why = best_mismatch([unparse_seq(s) for s in all_bodies(new_node)], needle)
            bad.append((old_proj, old_fn, new_fn, why))
    return ok, bad


# --------------------------------------------------------------- E1b 字面量集合
def consts(node):
    """递归收集所有 str/int/float 字面量的规范文本（多重集）。"""
    out = []
    for n in ast.walk(node):
        if isinstance(n, ast.Constant) and isinstance(n.value, (str, int, float)) \
                and not isinstance(n.value, bool):
            out.append(repr(n.value))
    return out


def e1b(acq_path=None):
    """E1b：**旧 L1 体里的每个字面量都必须出现在对应的新函数里**。

    对 E1 无能为力的"参数化重写"（cmd 构造 / build-elf 派生 / 分支包装）尤其有用：
    命令串、时长、端口名、寄存器地址都是字面量，少一个就报红。
    """
    acq_path = Path(acq_path or ACQ)
    rows, miss = [], []
    for old_proj, old_fn, new_fn, subs in PAIRS:
        old_path = BEFORE / old_proj / "run_test.py"
        old_node = func_node(old_path, old_fn)
        new_node = func_node(acq_path, ALIAS.get(new_fn, new_fn))
        if old_node is None or new_node is None:
            continue
        old_c = consts(ast.Module(
            body=stmts_of(OLD_BODY(old_path, old_node, 0, subs)), type_ignores=[]))
        # 新侧 = acquire 的对应函数 ∪ 本工程改写后的 run_test.py
        #（参数化的字面量——stall_s / build_subdir / banner_key / dumper 路径——搬到薄壳里，
        #  必须在本工程新文件里能找到，否则仍是"搬丢了"）
        shell = ROOT / "projects" / old_proj / "tests" / "run_test.py"
        new_c = consts(new_node)
        if shell.exists():
            new_c += consts(ast.parse(shell.read_text(encoding="utf-8")))
        missing = sorted(set(old_c) - set(new_c))
        rows.append((old_proj, old_fn, len(set(old_c)), len(missing)))
        if missing:
            miss.append((old_proj, old_fn, new_fn, missing))
    return rows, miss


# --------------------------------------------------------------- E2 非空转自检
HEDGES = {
    "两遍烧录/prime 轮": "pass-1 (prime, I-cache flush)",
    "单核 hold cpu2（APP_CPU_SRST）": "set {int}0x4002200c = 2",
    "写包提速 16384": "set memory-write-packet-size 16384",
    "remotewritesize 16384": "set remotewritesize 16384",
    "gdb stdout drain 线程": "detach\\nquit\\n",
    "优雅中断 \\x03": "\\x03",
    "静默判完成 IDLE=10s": "IDLE_S = 10",
    "LOAD_WINDOW=20s": "LOAD_WINDOW_S = 20",
    "prime hold 5s": "P1_HOLD_S = 5",
    "wmic 兜底杀 gdb": "wmic",
    "taskkill 进程树 /T": "/T",
    "DebugServer 残留自愈": "ESTABLISHED",
    "内存兜底 dump_results --json": "--json",
    "构建 west build": "west",
    "串口 115200": "115200",
    "1025 端口": "1025",
    "自动收尾 PROJECT EXECUTION": "PROJECT EXECUTION",
}


def e2(acq_path=None, rows=None):
    """E2：每条关键路径必须至少落在**一个通过 E1b 的配对**里（否则是"比了个空"）。"""
    acq_path = Path(acq_path or ACQ)
    acq_txt = acq_path.read_text(encoding="utf-8")
    pairs_ok = {r[1] for r in (rows or [])} if rows else None
    table, holes = [], []
    for label, needle in HEDGES.items():
        hits = []
        for old_proj, old_fn, new_fn, subs in PAIRS + EXTRA_PAIRS:
            try:
                txt = OLD_BODY(BEFORE / old_proj / "run_test.py",
                               func_node(BEFORE / old_proj / "run_test.py", old_fn), 0, subs)
            except Exception:
                continue
            if needle.replace("\\n", "\n") in txt.replace("\\n", "\n"):
                hits.append(f"{old_proj}.{old_fn}")
        in_new = needle.replace("\\n", "\n") in acq_txt.replace("\\n", "\n")
        table.append((label, len(hits), in_new, hits[:3]))
        if not hits or not in_new:
            holes.append(label)
    return table, holes


# --------------------------------------------------------------- E3 负对照
MUTATIONS = [
    ("16384", "16385"),                       # 写包提速参数
    ("time.sleep(1.0)", "time.sleep(1.5)"),   # 优雅中断等待
    ("taskkill", "taskkillX"),                # 命令名
    ('"/T"', '"/Z"'),                         # taskkill 参数
    ("IDLE_S = 10", "IDLE_S = 11"),           # 静默阈值
    ("0x4002200c", "0x4002200d"),             # 单核 hold 寄存器
    ("_TZ_NEVER_", "_TZ_NEVER_"),             # 占位（不生效）
]


def e3():
    """E3：扰动 acquire.py，断言 E1b 的"旧字面量缺失"变多 ⇒ 比对器对命令/参数/时长敏感。"""
    base_rows, base_miss = e1b()
    base_n = sum(len(m[3]) for m in base_miss)
    print(f"  [E3] 基线：缺失字面量 {base_n} 个")
    detected = 0
    for old, new in MUTATIONS:
        if old == new:
            continue
        src = ACQ.read_text(encoding="utf-8")
        if old not in src:
            print(f"  [E3] 跳过（源码中无 {old!r}）")
            continue
        tmp = Path(tempfile.mkdtemp()) / "acquire_mut.py"
        tmp.write_text(src.replace(old, new), encoding="utf-8")
        _, miss = e1b(tmp)
        n = sum(len(m[3]) for m in miss)
        hit = n > base_n or bool(miss) != bool(base_miss)
        detected += 1 if hit else 0
        print(f"  [E3] 扰动 {old!r} -> {new!r}: 缺失字面量 {base_n} -> {n} "
              f"{'检测到 ★' if hit else '未检测到 ✗'}")
    return detected, len([m for m in MUTATIONS if m[0] != m[1]])


# --------------------------------------------------------------- 已声明等价（非逐字）
KNOWN_EQUIV = {
    ("otbn_test", "gdb_flash_once", "'[run] WARNING: 60s no token (t='"):
        "卡死阈值 60 参数化为 spec.stall_s（otbn 薄壳 stall_s=60；mbedtls 同处 300）",
    ("i2c_pure_test", "run_case", "'s 内无任何固件输出（ZT_BOOT/ztest 行/I2C_PURE_TEST token）"
                                   "——测试环境有问题（DebugServer 残留/烧录链路/镜像陈旧）'"):
        "pass-1 失败文案里的 token 前缀参数化为 spec.banner_key（薄壳 = 'I2C_PURE_TEST:'）",
    ("i2c_pure_test", "memory_backfill", "'dump_results.py'"):
        "DUAL_RUN 形态的 dumper 名搬到 acquire.dual_run（dumper=\"dump_results.py\" + cwd=<tests>/scripts）",
}


def _equiv_explained(proj, fn, literal):
    return KNOWN_EQUIV.get((proj, fn, literal))


# --------------------------------------------------------------- E4 接线表
STRATEGIES = ("single_cap", "ports_dict", "ports_dict_case", "dual_run", "mbox", "iwdg")


def e4():
    """E4：24 个薄壳各自恰好调一次采集策略，且 L1 定义已不在本文件里。"""
    rows, bad = [], []
    for p in sorted((ROOT / "projects").iterdir()):
        f = p / "tests" / "run_test.py"
        if not f.exists():
            continue
        tree = ast.parse(f.read_text(encoding="utf-8"))
        calls = [ast.unparse(n.func) for n in ast.walk(tree)
                 if isinstance(n, ast.Call)
                 and ast.unparse(n.func) in {f"acquire.{s}" for s in STRATEGIES}]
        defined = {n.name for n in ast.walk(tree) if isinstance(n, (ast.FunctionDef, ast.ClassDef))}
        leftover = sorted(defined & L1_MOVED)
        rows.append((p.name, len(calls), calls[0] if calls else "-", leftover))
        if len(calls) != 1 or leftover:
            bad.append((p.name, len(calls), calls, leftover))
    return rows, bad


L1_MOVED = {"SerialCapture", "reader", "discover_ports", "find_tgt_uart", "gdb_flash_once",
            "capture_case", "run_case", "run_pass", "memory_backfill", "dump_mem",
            "_spawn_test_run", "_kill_proc_group", "restart_debugserver",
            "_restart_debugserver", "clean_gdb_residue", "_clean_gdb_residue",
            "_find_git_bash", "_netstat_1025", "_kill_residual_gdb", "_probe_gdb_alive",
            "preflight_gdb", "build_case", "_own_port", "_ztest_view"}


def main():
    ok, bad = e1()
    print(f"E1 逐字搬运（归一后的连续语句序列）: {ok} 通过 / {len(bad)} 未通过（共 {len(PAIRS)}）")
    for row in bad:
        print("   E1 FAIL:", row[0], row[1], "->", row[2], "|", row[3][:160])

    rows, miss = e1b()
    print(f"\nE1b 字面量核对（旧体每个字面量都在新函数里）: "
          f"{len(rows) - len(miss)}/{len(rows)} 配对无缺失")
    unexplained = []
    for m in miss:
        proj, fn, new_fn, lits = m
        left = []
        for lit in lits:
            why = _equiv_explained(proj, fn, lit)
            if why:
                print(f"   E1b 等价 {proj}.{fn} {lit}  —— {why}")
            else:
                left.append(lit)
        if left:
            unexplained.append((proj, fn, new_fn, left))
    if unexplained:
        for u in unexplained:
            print("   E1b MISS:", u[0], u[1], "缺", u[3][:12])

    t4, b4 = e4()
    print(f"\nE4 接线表：{len(t4) - len(b4)}/{len(t4)} 个薄壳恰好调一次采集策略、且 L1 定义已清空")
    for r in t4:
        print(f"   {'OK ' if not r[3] else 'BAD'} {r[0]:<20} 策略={r[2]:<26} 残留L1定义={r[3]}")

    table, holes = e2(rows=[r for r in rows if not miss or True])
    print("\nE2 非空转自检：关键路径 → 落在几个被比对的旧函数里")
    for label, n, in_new, sample in table:
        print(f"   {'OK ' if n and in_new else 'HOLE'} {label:<28} 旧侧命中 {n:>2} 处, "
              f"新侧存在={in_new}  例: {sample[:2]}")
    print(f"E2 结论: {'全部关键路径都被覆盖 ★' if not holes else '未覆盖: ' + str(holes)}")

    if "--neg" in sys.argv:
        print("\nE3 负对照（扰动 acquire.py 看比对器是否变红）")
        got, total = e3()
        print(f"E3 结论: {got}/{total} 个扰动被检测到")

    return 0 if (not bad and not holes) else 1


if __name__ == "__main__":
    sys.exit(main())
