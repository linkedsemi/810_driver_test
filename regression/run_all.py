#!/usr/bin/env python3
"""810_driver_test 一键全量回归总控。

批量全程脚本自驱（构建/烧录/采集/归档/自愈都在各工程 run 脚本内），
agent 只负责启动；单 case FAIL 不阻断后续 case；预检 FAIL 才中止（exit 2）。
一次批量 = 一个会话目录 reports/<YYYY-MM-DD>-<idx>（spec §9.1）。

用法：
  python run_all.py                # 环境预检 + cases.yaml 全部启用 case（两级清单，见 read_cases）
  python run_all.py --no-precheck  # 跳过预检
  python run_all.py --dry-run      # 只列启用 case 与映射脚本，不执行
"""
import argparse
import datetime
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent   # 810_driver_test
REPORTS_ROOT = ROOT / "reports"
CASES_FILE = Path(__file__).resolve().parent / "cases.yaml"
PRECHECK = ROOT / "regression" / "precheck" / "run_precheck.py"

# 双核工程 UART：None = 由 precheck 自动发现后从 port_mapping.json 读取；
# 环境变量可强制覆盖（同 precheck PRE_CTRL_UART / PRE_TGT_UART）。
CTRL_UART = os.environ.get("PRE_CTRL_UART") or None
TGT_UART  = os.environ.get("PRE_TGT_UART")  or None

# DebugServer（§0/§10.17）：1025 由 PC 进程 XuanTie DebugServer 提供；
# 重启 = 四步自愈（杀残留 gdb → taskkill /IM → netstat 兜底 → Popen + 日志轮询）。
# ★A3: 日志路径以 run_precheck.py:177 实测定稿为准（%TEMP%/debug_server.log），
# 计划笔误 DEBUGSERVER_EXE.parent / "DebugServer.log" 不用。
DEBUGSERVER_EXE = Path(r"C:\uaer_app\debug_server\bin\DebugServerConsole.exe")
DEBUGSERVER_LOG = Path(tempfile.gettempdir()) / "debug_server.log"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"


def _line_buffered_stdout():
    """stdout 逐行刷新（无人值守心跳的可见性，2026-09-20）。

    重定向到文件/管道时 Python 默认**块缓冲**：进度行要等缓冲满（或进程退出）
    才落盘 ⇒ 外部只能看到"卡住"，看不到进度（B2-fix 实测）。这里改成逐行刷新；
    取不到 reconfigure（老 Python / 已包装流）时退回给关键进度 print 加
    flush=True —— 两条路都保证进度可见。
    """
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except (AttributeError, ValueError, OSError):
        pass


GDB = "riscv64-unknown-elf-gdb"

# 工程 → run 脚本（**覆盖用**）。默认按目录约定发现 `projects/<proj>/tests/run_test.py`，
# 无需登记；这里只在某工程要用**非默认脚本名**时才加条目。
PROJECT_SCRIPT = {}


def _is_dual_core(proj: str) -> bool:
    """工程是否双核 —— **由目录结构派生，不维护工程名单**。

    判据：`projects/<proj>/` 下有两个 `cpu*` 固件目录
    （老工程 `cpu0_`+`cpu1_`，新工程 `cpu1_`+`cpu2_`）。

    为什么不建外部表：漏登记会让该工程**所有 case 恒 `FAIL 0/0`**
    （双核 run 脚本缺 `--ctrl-uart` 直接退出）—— 一个静默失效。派生则不可能漂移。
    """
    d = ROOT / "projects" / proj
    if not d.is_dir():
        return False
    return sum(1 for x in d.iterdir()
               if x.is_dir() and re.match(r"^cpu[0-9]+", x.name)) == 2


def _resolve_script(proj):
    """工程名 → tests/ 下的 run 脚本名。

    按**目录约定**发现 `<ROOT>/projects/<proj>/tests/run_test.py` —— 新工程**无需登记**。
    `PROJECT_SCRIPT` 仅作**覆盖**用（某工程要用非默认脚本名时才加），现为空。
    不命中返回 None（保持原 SKIP 记账行为）。
    """
    if proj in PROJECT_SCRIPT:
        return PROJECT_SCRIPT[proj]
    if (ROOT / "projects" / proj / "tests" / "run_test.py").exists():
        return "run_test.py"
    return None


def _elf_has_sentinel(elf: Path) -> bool:
    """ELF 是否含有效性哨兵符号 `test_results_valid`（F3，2026-09-18）。

    缺它 = 该镜像在哨兵加入（2026-09-18）**之前**编译 ⇒ 内存兜底会 fail-closed
    （dump_results.py 报 `no_sentinel_symbol`）。这种镜像**不得被 --skip-build 复用**，
    否则整个 case 会翻成"看起来像内核挂死"的无效 FAIL（batch 级误报）。
    """
    if not elf.exists():
        return False
    nm = shutil.which("riscv64-unknown-elf-nm") or "riscv64-unknown-elf-nm"
    try:
        out = subprocess.run([nm, str(elf)], capture_output=True, text=True,
                             timeout=60).stdout
    except Exception:                                  # noqa: BLE001
        return False
    return any(len(ln.split()) >= 3 and ln.split()[-1] == "test_results_valid"
               for ln in out.splitlines())


def _case_elves(case_dir: Path):
    """该 case 可能存在的 ELF 路径（单核 target / 双核 target+controller）。"""
    build = case_dir / "build"
    return [build / "target" / "zephyr" / "zephyr.elf",
            build / "controller" / "zephyr" / "zephyr.elf"]


# F5（2026-09-20）：构建输入后缀。判"产物是否新鲜"只看**会进构建**的输入；
# `.py`（run_test.py / dbg_run.py 等 harness）**不计入** —— 它们不改变 ELF 内容，
# 计入只会让每次 harness 改动都误触发全量重建，把 --skip-build 快路径彻底废掉。
_BUILD_INPUT_EXTS = {
    ".c", ".h", ".cpp", ".hpp", ".cc", ".S", ".s", ".asm",
    ".ld", ".lds", ".overlay", ".conf", ".dtsi", ".dts", ".inc",
    ".cmake", ".sh", ".txt",
}


# 构建定义里出现的**文件名**（与 shell 侧 test_common.sh::_common_build_inputs 同款
# 判据）：用来判断 common/ 的某份文件是否真的链进本工程。
_CM_REF_RE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.-]*\.(?:c|h|S|s|ld|lds|inc)")


def _common_build_inputs(proj_root: Path):
    """本工程**真正链入**的 `common/` 构建输入（F5 修正 2，2026-09-20）。

    common/ 是公共目录，但**并非每份都进每个工程**：`rlog.c` 只在
    i3c_i2c_switch_test 的 CMakeLists 里显式 `target_sources` 编入。原先把整个
    `common/**` 当输入 ⇒ `common/` 每新增一个文件（如 rlog.c/h）就把**所有**工程的
    产物判成"陈旧"，全线误报 WARN —— 误报会让这条告警失去信号价值。
    **宁可漏报也不误报。**

    判据（与 `scripts/test_common.sh::_common_build_inputs` 逐字同构）：
      * 引用集 = 本工程各 `*/CMakeLists.txt` ＋ `common/common_sources.cmake` 中
        出现的 `.c/.h/.S/.s/.ld/.lds/.inc` 文件名（后者是所有工程的公共入口 ⇒
        test_results / ztest_glue / gpio_toggle 恒被计入）；
      * `common/<f>` 入选 ⇔ `f` 在引用集里，或同干 `.c` 在引用集里
        （`test_results.h` ⇐ `test_results.c`）。
    引用集为空 ⇒ 输出空（漏报可接受，误报不可）。
    """
    refs = set()
    sources = sorted(proj_root.glob("*/CMakeLists.txt"))
    cm = ROOT / "common" / "common_sources.cmake"
    if cm.exists():
        sources.append(cm)
    for f in sources:
        try:
            txt = f.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        refs.update(m.group(0) for m in _CM_REF_RE.finditer(txt))
    if not refs:
        return
    cdir = ROOT / "common"
    if not cdir.is_dir():
        return
    for p in sorted(cdir.iterdir()):
        if not p.is_file():
            continue
        if p.suffix not in _BUILD_INPUT_EXTS \
                and p.suffix.lower() not in _BUILD_INPUT_EXTS:
            continue
        if p.name in refs or f"{p.stem}.c" in refs:
            yield p


def _iter_build_inputs(case_dir: Path):
    """枚举该 case 的全部构建输入源码（保守取并集）。

    范围：
      - 工程全树 `projects/<proj>/**`（覆盖固件目录、`tests/include` 协议头、
        以及本 case 的 `test.sh` 构建配方）
      - **本工程真正链入的** `common/**`（见 `_common_build_inputs`；不是整个
        `common/` —— 那会让公共目录的新增文件对全部工程误报陈旧）
    排除 `build*/`、`tmp/`、`__pycache__`（生成物 / 临时目录 / 缓存）。

    ★ 刻意**不含** `scripts/test_common.sh`：它是跨工程共享的构建配方，`west build
    -p auto` 的 ninja 不跟踪它，把它算进输入会让"配方一改 ⇒ 全体产物永久判陈旧"
    （ELF mtime 不会因此更新）—— 属于过度误伤。配方变更的正确处理是全量重建，
    那是显式动作，不靠本门兜。
    """
    proj_root = case_dir.parents[1].resolve()
    for dirpath, dirnames, filenames in os.walk(proj_root):
        dirnames[:] = [d for d in dirnames
                       if d not in ("__pycache__", "tmp")
                       and not d.startswith("build")]
        for fn in filenames:
            p = Path(dirpath) / fn
            if p.suffix in _BUILD_INPUT_EXTS \
                    or p.suffix.lower() in _BUILD_INPUT_EXTS:
                yield p
    yield from _common_build_inputs(proj_root)


def _stale_build_inputs(case_dir: Path, elves):
    """返回晚于该 case **最旧** ELF 的构建输入 `[(path, mtime), ...]`（新→旧）。

    空列表 = 产物新鲜，可安全复用；非空 = 陈旧，**必须重建**。
    `None` = 该 case 没有任何 ELF（调用方按"缺产物"处理）。
    取"最旧 ELF"是刻意的保守：双核 target/controller 只要有一个落后于源码，
    整个 case 就已不一致。
    容差 1s：吸收文件系统时间戳粒度与同批写出的抖动，避免无意义的重建。
    """
    mt = [e.stat().st_mtime for e in elves if e.exists()]
    if not mt:
        return None
    elf_m = min(mt)
    out = []
    for p in _iter_build_inputs(case_dir):
        try:
            m = p.stat().st_mtime
        except OSError:
            continue
        if m > elf_m + 1.0:
            out.append((p, m))
    out.sort(key=lambda x: -x[1])
    return out


def can_skip_build(case_dir: Path):
    """该 case 是否允许 `--skip-build`（复用现成 ELF）。

    返回 `(ok, reason, detail)`：
      - `(False, "no-sentinel", None)` ⇒ 缺哨兵符号（F3）——内存兜底会 fail-closed
      - `(False, "stale", [(path, mtime), ...])` ⇒ 产物陈旧（F5）
      - `(True,  "", None)`            ⇒ 哨兵齐全**且**产物不陈旧
    两条判据都必须通过；任一不过即强制重建（保守：宁可多重建）。
    """
    elves = _case_elves(case_dir)
    if not all(_elf_has_sentinel(e) for e in elves):
        return False, "no-sentinel", None
    stale = _stale_build_inputs(case_dir, elves)   # 此处 ELF 必存在 ⇒ 非 None
    if stale:
        return False, "stale", stale
    return True, "", None


def audit_stale_elves(cases):
    """F3 预检：列出"已存在但缺哨兵符号"的旧镜像（它们会被重建，不是失败）。

    目的 = **不让旧 ELF 静默变成 FAIL**：把这些 case 显式打出来，读者才知道
    接下来那次构建是"必要的重建"而不是"环境抖动"。
    """
    stale = []
    for c in cases:
        for elf in _case_elves(c):
            if elf.exists() and not _elf_has_sentinel(elf):
                stale.append((c.parents[1].name, c.name, elf))
    if stale:
        print(f"[run_all] WARN 检出 {len(stale)} 个缺哨兵符号的旧镜像（将强制重建）：")
        for proj, case, elf in stale:
            print(f"[run_all]   {proj} {case}: {elf}")
        print("[run_all]   原因：哨兵符号 test_results_valid 不存在 = 哨兵加入前"
              "编译的旧构建；若被 --skip-build 复用，内存兜底会 fail-closed。")
    return stale


# ---- DebugServer 环境管理（run_precheck.py 既有实现照搬，§10.17 四步）----

def _netstat_1025():
    """netstat 1025 端口状态：(状态, 本端PID, 对端PID)。"""
    rows = []
    try:
        out = subprocess.run(["netstat", "-ano"], capture_output=True,
                             text=True, timeout=15).stdout
        for line in out.splitlines():
            if ":1025" not in line:
                continue
            parts = line.split()
            if len(parts) >= 5 and parts[0] == "TCP":
                rows.append((parts[3], parts[4], parts[1]))
    except Exception:
        pass
    return rows


def _kill_residual_gdb():
    """精准杀 1025 ESTABLISHED 连接对端（是 gdb 就 taskkill）。

    ★ Python subprocess 无 MSYS 路径转换层：tasklist/taskkill 参数必须用
    单斜杠（/FI /FO /F /PID），双斜杠 //F 会被 Windows 程序拒绝（§10.16）。
    """
    try:
        pids = set()
        for state, local_pid, _ in _netstat_1025():
            if state == "ESTABLISHED" and local_pid.isdigit():
                pids.add(local_pid)
        for pid in pids:
            ti = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/FO", "CSV"],
                                capture_output=True, text=True, timeout=15).stdout
            if "gdb" in ti.lower():
                subprocess.run(["taskkill", "/F", "/PID", pid],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=15)
    except Exception:
        pass


def _restart_debugserver():
    """重启 DebugServer 进程并等就绪（日志轮询 "DebuggerServer$" 提示符）。

    实测：任何"连接→detach→退出"的 gdb（包括探测连接）都会让 DebugServer
    留下连接残留——detach 不能真正释放连接，唯一可靠的方式是重启进程
    （§10.17）。CKLink 初始化耗时随机，不能只固定 sleep → 日志轮询。
    （run_precheck.py _restart_debugserver 原样照搬）
    """
    print("[run_all] restarting DebugServer (residue)", file=sys.stderr)
    try:
        subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=15)
    except Exception:
        pass   # taskkill 超时/失败不阻断自愈流程（netstat 精确杀兜底，§10.17）
    time.sleep(1)
    # taskkill /IM 可能杀不净（无效实例占 1025，新实例绑定失败卡在初始化）
    # → 用 netstat 精确杀 1025 的 LISTENING 进程兜底。
    for state, local_pid, _ in _netstat_1025():
        if state == "LISTENING" and local_pid.isdigit():
            try:
                subprocess.run(["taskkill", "/F", "/PID", local_pid],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=15)
            except Exception:
                pass
    try:
        DEBUGSERVER_LOG.unlink()
    except OSError:
        pass
    try:
        subprocess.Popen(
            [str(DEBUGSERVER_EXE)],
            cwd=str(DEBUGSERVER_EXE.parent),
            stdout=open(DEBUGSERVER_LOG, "w", encoding="utf-8", errors="replace"),
            stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NEW_CONSOLE,
        )
    except Exception:
        pass
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            with open(DEBUGSERVER_LOG, encoding="utf-8", errors="replace") as f:
                if "DebuggerServer$" in f.read() or "DebuggerServer>" in f.read():
                    break
        except Exception:
            pass
        time.sleep(0.5)
    else:
        print("[run_all] WARNING: DebugServer 30s 内未打印就绪提示符", file=sys.stderr)
    time.sleep(1)


def clean_gdb_residue(force_restart: bool = False) -> None:
    """清 DebugServer 连接残留（§10.1 高频坑）。

    ★ 按需重启：先杀残留 gdb，杀完复查 1025——连接残留仍在（ESTABLISHED
    未释放）或 server 已死（无 LISTENING）才重启；健康则不重启（§10.17）。
    （run_precheck.py clean_gdb_residue 原样照搬；A4：precheck 之后、
    erase 重启 DebugServer 之后、进入 case 循环前各调用一次）
    """
    _kill_residual_gdb()
    if force_restart:
        _restart_debugserver()
        return
    need = False
    for state, _, _ in _netstat_1025():
        if state == "ESTABLISHED":
            need = True
            break
    if not need:
        # server 没起来（无 LISTENING）也要重启
        need = not any(st == "LISTENING" for st, _, _ in _netstat_1025())
    if need:
        _restart_debugserver()


def restart_debugserver() -> None:
    """重启 XuanTie DebugServer 并等稳定期（§10.20：新实例立即连跑会
    间歇性 load_fail/断连，5s 稳定期后再连）。"""
    _restart_debugserver()
    time.sleep(5)   # 稳定期：新实例 CKLink 初始化/总线状态稳定后再连跑


# ---- cases.yaml 读取与擦除编排 ----

def _parse_yaml_list(path: Path):
    """读一份 yaml 清单的 `cases:` 段 —— 简单 list 解析，兼容无 PyYAML 环境。

    `#` 开头或空行忽略（`#` 屏蔽 = 跳过该条）。返回已统一分隔符的字符串列表。"""
    out = []
    in_cases = False
    for raw in Path(path).read_text(encoding="utf-8").splitlines():
        ln = raw.rstrip()
        if not ln or ln.lstrip().startswith("#"):
            continue
        s = ln.strip()
        if s == "cases:":
            in_cases = True
            continue
        if not in_cases or not s.startswith("-"):
            continue
        item = s[1:].strip().strip('"').strip("'").replace("\\", "/")
        if item.lower().startswith("810_driver_test/"):
            item = item[len("810_driver_test/"):]
        out.append(item)
    return out


def read_cases(cases_file: Path = None):
    """读启用清单，展开成**一串 case 目录**。

    **两级结构**：
      顶层（缺省 `regression/cases.yaml`，或 `--cases-file` 指定）—— 列**工程**；
      工程侧 `projects/<工程>/tests/cases.yaml` —— 列该工程要跑哪些 **case**。

    为兼容既有子集清单，顶层条目**同时认两种形态**：
      · 工程目录（非 case 目录的目录）→ 展开其 `tests/cases.yaml`
      · case 目录（含 `test.sh`）→ 直接采用

    工程缺 `tests/cases.yaml` ⇒ **报错中止**。**不自动发现**：磁盘上存在 ≠ 启用
    （磁盘上 60 个 case、启用 51 个），自动发现会把不该跑的一起跑。

    `--cases-file` 仍可指向另一份同格式清单（子集验证 / 不同板况用），
    走的仍是 run_all 的完整路径（预检 → 擦除 → 逐 case 起 run_test.py → 汇总）。"""
    cases_file = Path(cases_file) if cases_file else CASES_FILE
    out = []
    for item in _parse_yaml_list(cases_file):
        p = ROOT / item
        if not p.is_dir():
            print(f"[run_all] SKIP {item}: 目录不存在")
            continue
        if (p / "test.sh").exists():          # 兼容形态：条目直接是 case 目录
            out.append(p)
            continue
        sub = p / "tests" / "cases.yaml"       # 工程目录 → 展开其 case 清单
        if not sub.is_file():
            print(f"[run_all] ERROR: {item} 缺 tests/cases.yaml —— 工程必须显式声明"
                  f"启用哪些 case（不自动发现：磁盘上存在 ≠ 启用）", file=sys.stderr)
            raise SystemExit(1)
        for name in _parse_yaml_list(sub):
            cd = p / "tests" / name
            if not (cd / "test.sh").exists():
                print(f"[run_all] ERROR: {item}/tests/cases.yaml 登记的 {name} 下无 test.sh",
                      file=sys.stderr)
                raise SystemExit(1)
            out.append(cd)
    return out


def _probe_cklink():
    """快速探测 CKLink 是否可用（5s 超时）。

    用 DebugServer 尝试监听 1025 → GDB 连一次 → 看是否进 "Remote debugging"。
    返回 True = CKLink 正常，False = 需要用户拔插 USB。
    """
    # 先确保证 DebugServer 在跑（probe 需要它）
    ds_was_running = any(st == "LISTENING" for st, _, _ in _netstat_1025())
    if not ds_was_running:
        try:
            subprocess.Popen(
                [str(DEBUGSERVER_EXE)],
                cwd=str(DEBUGSERVER_EXE.parent),
                stdout=open(DEBUGSERVER_LOG, "w", encoding="utf-8", errors="replace"),
                stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NEW_CONSOLE,
            )
        except Exception:
            return False
        deadline = time.time() + 15
        while time.time() < deadline:
            try:
                with open(DEBUGSERVER_LOG, encoding="utf-8", errors="replace") as f:
                    if "DebuggerServer$" in f.read() or "DebuggerServer>" in f.read():
                        break
            except Exception:
                pass
            time.sleep(0.5)
        else:
            return False
        time.sleep(1)

    # GDB 探测连接
    try:
        proc = subprocess.run(
            [GDB, "-batch", "-ex", "target remote 127.0.0.1:1025", "-ex", "detach", "-ex", "quit"],
            capture_output=True, text=True, timeout=8,
            encoding="utf-8", errors="replace",
        )
        # -batch 模式不打印 "Remote debugging"，改判 "detached" 或 退出码 0
        combined = proc.stdout + proc.stderr
        ok = proc.returncode == 0 and ("detached" in combined.lower()
                                        or "remote target" in combined.lower())
    except Exception:
        ok = False

    # 探测完杀掉临时 DebugServer（erase 需要无 DebugServer 状态）
    if not ds_was_running:
        try:
            subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=10)
        except Exception:
            pass
        time.sleep(3)  # CKLink USB 释放需要时间，太短会导致后续 erase 卡住
    return ok


def erase_flash_once():
    """flash 全片擦除：DebugServer 必须先退出（CKLink 互斥，§15）。

    预清理残留进程 → 直接擦除 → 重启 DebugServer。
    CKLink 健康性由 cleanup_cklink()（回归结束时）保证，不在此探测
    （实测：探测的启停 DebugServer 会干扰 CKLink 状态，反而导致擦除卡死）。
    """
    print("[run_all] === flash 全片擦除 ===")

    # 预清理残留进程
    _kill_residual_gdb()
    try:
        subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=10)
    except Exception:
        pass
    time.sleep(1)
    for state, local_pid, _ in _netstat_1025():
        if state == "LISTENING" and local_pid.isdigit():
            try:
                subprocess.run(["taskkill", "/F", "/PID", local_pid],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=10)
            except Exception:
                pass

    # 执行擦除
    rc = subprocess.call([sys.executable, str(ROOT / "scripts" / "flash_program.py"),
                          "--erase-only", "--timeout", "20"])
    if rc != 0:
        print("[run_all] flash 擦除失败，批量中止")
        return rc
    restart_debugserver()
    return 0


def cleanup_cklink():
    """回归结束后释放 CKLink（§10.17 / §15 互斥）。

    回归全程 DebugServer 常驻（case 循环中按需重启），跑完后必须杀掉，
    否则下次 erase_flash_once 时 CKLink 仍被占 → flash 卡死 180s。
    """
    print("[run_all] === 释放 CKLink（杀 DebugServer + 残留 gdb）===")
    _kill_residual_gdb()
    try:
        subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=10)
    except Exception:
        pass
    # 精确杀 1025 上的进程（/IM 偶发杀不净）
    for state, local_pid, _ in _netstat_1025():
        if local_pid.isdigit():
            try:
                subprocess.run(["taskkill", "/F", "/PID", local_pid],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=10)
            except Exception:
                pass
    # 确认 1025 已释放
    time.sleep(1)
    remaining = [(s, p) for s, p, _ in _netstat_1025() if s == "LISTENING"]
    if remaining:
        print(f"[run_all] WARNING: 1025 仍有 LISTENING: {remaining}", file=sys.stderr)
    else:
        print("[run_all] CKLink 已释放")


def main():
    _line_buffered_stdout()
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-precheck", action="store_true")
    ap.add_argument("--no-erase", action="store_true",
                    help="跳过 flash 全片擦除（默认全量前擦一次，用户 #6）")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--date", default=None,
                    help="会话目录日期（YYYY-MM-DD），缺省 = 今天。"
                         "用于把本批报告归到指定日期（如跨零点长跑 / 避免与当日已有批次混排）")
    ap.add_argument("--cases-file", default=None,
                    help="改用另一份同格式清单（默认 regression/cases.yaml，只列工程）；"
                         "顶层条目也认 case 目录，故旧的完整 case 路径子集清单仍可用；"
                         "走的仍是完整 run_all 路径；本批归档固定 reports/<date>-<idx>，"
                         "单模块测试请直接跑 projects/<p>/tests/run_test.py（进 reports/module/）")
    args = ap.parse_args()

    cases = read_cases(args.cases_file)
    if not cases:
        print("[run_all] ERROR: cases.yaml 无启用 case")
        return 1
    if args.dry_run:
        print("[run_all] DRY-RUN，启用 case 与映射：")
        for c in cases:
            proj = c.parents[1].name
            print(f"  {proj:<22} {c.name:<26} -> {_resolve_script(proj) or '??'}")
        sys.path.insert(0, str(ROOT / "common"))
        from report_dir import plan_session_dir          # noqa: E402
        planned = plan_session_dir(
            REPORTS_ROOT, args.date or datetime.date.today().isoformat(), kind="full")
        print(f"[run_all] DRY-RUN 计划会话目录（全量）: {planned}")
        return 0

    # ★ try/finally：任何退出路径都释放 CKLink（§15 互斥）
    try:
        return _run_all(args, cases)
    finally:
        cleanup_cklink()


def _run_all(args, cases):
    """run_all 主逻辑（由 main 的 try/finally 保护退出时释放 CKLink）。"""

    # Step 3b：flash 全片擦除（A5：--dry-run 返回后、分配会话目录之前；
    # --no-precheck 不动擦除——擦除恒做，--no-erase 逃生门）
    if not args.no_erase:
        rc = erase_flash_once()
        if rc != 0:
            return 2
        # A4-②: erase_flash_once 重启 DebugServer 之后、进入 case 循环前
        # （erase 先于 precheck，此残留若被 precheck 探测重新制造，
        # 由下方 A4-① 的 clean 再清一次）
        clean_gdb_residue()

    sys.path.insert(0, str(ROOT / "common"))
    from report_dir import allocate_session_dir, read_summary, update_summary
    import acquire as _acquire   # E-2：每 case 前复用同一份 DS 存活检查（不另写一套）
    # 批量回归固定落到 reports/<date>-<idx>（kind="full"）；单模块 run_test.py
    # 自行分配的会话走 reports/module/（见 common/report_dir.py）。
    session = allocate_session_dir(
        REPORTS_ROOT, args.date or datetime.date.today().isoformat(),
        kind="full", projects=sorted({c.parents[1].name for c in cases}))
    print(f"[run_all] session: {session}", flush=True)

    if not args.no_precheck:
        print("[run_all] === 环境预检 ===")
        rc = subprocess.call([sys.executable, str(PRECHECK),
                              "--session-dir", str(session)])
        if rc != 0:
            print("[run_all] 预检 FAIL，批量中止；请交 agent 排查（qsh-debug）后重跑")
            return 2
        # A4-①: precheck 的探测连接/load 会话必留残留（T1，§10.17）→
        # case 循环前必须清一次（若 --no-precheck 则上面的 A4-② 覆盖）
        clean_gdb_residue()

    # ---- 读取端口映射（precheck 自动发现 或 环境变量指定） ----
    import json as _json
    global CTRL_UART, TGT_UART
    _mapping_file = session / "port_mapping.json"
    if not CTRL_UART or not TGT_UART:
        if _mapping_file.exists():
            _m = _json.loads(_mapping_file.read_text(encoding="utf-8"))
            CTRL_UART = CTRL_UART or _m.get("ctrl")
            TGT_UART  = TGT_UART  or _m.get("tgt")
            print(f"[run_all] port mapping from precheck: CTRL={CTRL_UART} TGT={TGT_UART}")
        else:
            # --no-precheck 且无环境变量：做一次最小发现（不 load，只看在线口）
            _ports = [p.device for p in __import__("serial.tools.list_ports", fromlist=["comports"]).comports()
                      if "32bf" not in (p.hwid or "").lower() and "cklink" not in (p.description or "").lower()]
            if len(_ports) >= 2:
                # 无法在无固件情况下识别角色，按顺序猜（用户应通过 precheck 或环境变量指定）
                CTRL_UART, TGT_UART = _ports[0], _ports[1]
                print(f"[run_all] --no-precheck, 无映射文件，按枚举顺序猜: CTRL={CTRL_UART} TGT={TGT_UART}（建议跑一次 precheck）")
            else:
                print("[run_all] ERROR: 无足够串口且无 port_mapping.json，双核工程无法运行", file=sys.stderr)
                return 2

    logs = session / "logs"
    logs.mkdir(exist_ok=True)
    # F3 预检（2026-09-18）：先把"已存在但缺哨兵符号"的旧镜像显式打出来，
    # 免得它们被误读成"环境抖动/内核挂死"（见 audit_stale_elves 注释）。
    audit_stale_elves(cases)
    results = []
    built_projects = set()
    for c in cases:
        proj = c.parents[1].name
        script = _resolve_script(proj)
        script_path = c.parent / script if script else None
        if not script_path or not script_path.exists():
            print(f"[run_all] SKIP {c}: 无 run 脚本映射（{script}）")
            results.append((proj, c.name, "SKIP"))
            continue

        # ★ E-2（2026-09-21）每 case 前置：DebugServer 存活检查。
        #   此前只在**整批开始前**清一次残留（A4-①②），循环内不补刀 ⇒ 一次 CKLink
        #   探针瞬时故障把 PC 侧 DebugServer 打挂（1025 无 LISTENING）之后，**每个**
        #   后续 case 的 gdb 都只能报 `could not connect (error 138)`，被记成假 FAIL
        #   —— 一次掉线放大成 N 个假 FAIL（实测 1 → 4）。
        #   这里逐 case 补刀：不活 ⇒ 复用 acquire 的既有自愈链拉起；**起不来 ⇒
        #   fail-closed 中止该 case**（不烧录、明确报错、结果记 ENV），绝不放它去
        #   退化成 error 138 的假 FAIL。
        ds_ok, ds_msg = _acquire.ensure_debugserver_alive(style="d3")
        if not ds_ok:
            print(f"[run_all] ERROR {proj} {c.name}: DebugServer 不可用 —— {ds_msg}"
                  " ⇒ 中止该 case（fail-closed，不产出 error 138 假 FAIL）",
                  file=sys.stderr, flush=True)
            logf = logs / f"{proj}_{c.name}.log"
            # 用 write_bytes + 显式 \n：与同目录其它日志（`open(logf,"wb")` 重定向
            # 子进程输出）同为 LF；`write_text` 在 Windows 会把 LF 整份翻成 CRLF。
            logf.write_bytes(
                ("[run_all] E-2 fail-closed：DebugServer 每 case 前置检查失败\n"
                 f"原因：{ds_msg}\n"
                 "该 case 未被拉起（未烧录、未采集）⇒ 不计入 PASS/FAIL。\n"
                 ).encode("utf-8"))
            results.append((proj, c.name, "ENV"))
            update_summary(session, f"{proj}_{c.name}", "ENV-ABORT", None, None,
                           f"logs/{proj}_{c.name}.log")
            continue

        if ds_msg != _acquire.DS_HEALTHY_MSG:
            # 只有**发生过自愈**才打日志：健康路径保持安静（否则 30 个 case 刷 30 行噪音）
            print(f"[run_all] DS 前置检查 {proj} {c.name}: {ds_msg}", flush=True)

        cmd = [sys.executable, str(script_path), "--case", c.name,
               "--session-dir", str(session)]
        if _is_dual_core(proj):
            cmd += ["--ctrl-uart", CTRL_UART, "--tgt-uart", TGT_UART]
        else:
            cmd += ["--tgt-uart", TGT_UART]  # 单核工程也传 TGT 串口
        if proj == "i3c_dualcore_test" and proj in built_projects:
            # F3（2026-09-18）修正：原注释"同会话首个 i3c case 已全量构建，无混旧
            # 镜像"**说反了** —— 每个 case 有**自己**的 build/ 目录，首个 case 的
            # 构建并不覆盖后续 case 的目录。所以这里必须按本 case 自己的 ELF 判断。
            #
            # 两道门（都要过才允许 --skip-build），判定见 can_skip_build()：
            #   ① 缺哨兵符号 ⇒ 强制重建（内存兜底会 fail-closed）。
            #   F5（2026-09-20）② **产物陈旧** ⇒ 强制重建。哨兵门只挡得住"哨兵加入
            #   （2026-09-18）之前的老 ELF"，挡不住"**有哨兵但陈旧**"的 ELF：源码
            #   改了、产物没重建时，烧进去的是**旧断言**，于是产生假 FAIL（旧期望
            #   值）或假 PASS（旧弱断言）—— 两者都是静默的结果污染。
            ok, reason, detail = can_skip_build(c)
            if ok:
                cmd.append("--skip-build")
            elif reason == "no-sentinel":
                print(f"[run_all] WARN {c.name}: ELF 缺哨兵符号或不存在 → 强制重建"
                      "（F3：复用旧镜像会让内存兜底 fail-closed，集体误报 FAIL）",
                      file=sys.stderr)
            else:  # stale
                def _ts(t):
                    return datetime.datetime.fromtimestamp(t).strftime("%m-%d %H:%M:%S")
                print(f"[run_all] WARN {c.name}: 构建产物陈旧（{len(detail)} 个输入"
                      f"晚于 ELF）→ 强制重建（F5）")
                for p, m in detail[:5]:
                    print(f"[run_all]     - 输入更新于 {_ts(m)}: {p}")
                if len(detail) > 5:
                    print(f"[run_all]     ... 另有 {len(detail) - 5} 个更新的输入")
        built_projects.add(proj)
        logf = logs / f"{proj}_{c.name}.log"
        print(f"[run_all] === {proj} {c.name} START（log: {logf.name}）===", flush=True)
        with open(logf, "wb") as fh:
            rc = subprocess.call(cmd, stdout=fh, stderr=subprocess.STDOUT)
        status = "PASS" if rc == 0 else "FAIL"
        results.append((proj, c.name, status))
        print(f"[run_all] === {proj} {c.name} DONE rc={rc} ===", flush=True)
        # ★ 失败 case 的兜底写入（O-12 修正）：现代 run_test.py 在**归档之后**
        #   才 `return 1` —— 即已写完整 report + 带真实 passed/total 的 summary 行
        #   （其计数来自报告，非 0/0）。此时**绝不能**再用 0/0 覆盖（曾把
        #   i3c_dualcore_test_case_mixed_400k_1m 的 34/35 覆盖成 0/0，误导判定）。
        #   仅当该 case 未留下 summary 行（如 build 失败提前 return）才兜底写 0/0
        #   并把链接指向日志。
        if status == "FAIL":
            key = f"{proj}_{c.name}"
            if key in read_summary(session):
                print(f"[run_all] {proj} {c.name}: summary 已由 run 脚本写入，保留不覆盖")
            else:
                # O-12 修正（2026-09-20）：拿不到计数时写**"未知"**，不得用 `0/0`
                # 冒充 —— `0/0` 读起来像"测得 0 项、通过 0 项"，真相是"该 case
                # 没留下可信计数"（run 脚本连报告都没写，如 build 失败提前 return）。
                # 链接指向日志供排查；计数由 report_dir.update_summary 渲染成"未知"。
                update_summary(session, key, "FAIL", None, None,
                               f"logs/{proj}_{c.name}.log")

    print("\n[run_all] 汇总：")
    n_fail = n_env = 0
    for proj, case, st in results:
        print(f"  {st:<5} {proj:<22} {case}")
        n_fail += (st == "FAIL")
        n_env += (st == "ENV")     # E-2：环境中止单列，不与"测试失败"混算
    n_pass = len(results) - n_fail - n_env
    print(f"\n[run_all] 共 {len(results)} 个测试，通过 {n_pass} 个，失败 {n_fail} 个"
          + (f"，环境中止 {n_env} 个" if n_env else ""))
    return 1 if (n_fail or n_env) else 0


if __name__ == "__main__":
    sys.exit(main())
