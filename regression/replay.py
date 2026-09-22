#!/usr/bin/env python3
"""离线重放入口（阶段 2，2026-09-20）：给定归档的原始串口 + case 名，重跑判定 + 渲染。

**不碰板子、不构建、不烧录**。本文件只做两件事：

  1. **串口重放**（`--session`，主路径）：读 `reports/<会话>/<case>/serial_<CORE>.log`
     + `replay_meta.json`，把归档的原始行喂回**该工程自己的 `run_test.py`**，
     让它的 `main()` 原样跑完判定 → 渲染 → 归档。**判定/渲染函数一行都不重写**
     —— 只把"硬件采集层"换成"从磁盘读行"（见 §适配器）。
  2. **报告重渲染**（`--render-from-json`，校验用）：把已归档的 `*_report.json`
     重新喂给该工程自己的 `write_markdown_report()`，与原始 `.md` 逐字节比对。
     用来回答"**哪些改动可重放、哪些不可**"——老会话没有串口归档，只有这条路。

用法::

    # 列出某会话里可重放的 case
    python regression/replay.py --session reports/2026-09-20-1 --list

    # 重放单个 case 到 tmp/replay/<会话>/
    python regression/replay.py --session reports/2026-09-20-1 --case trng_test_case_default

    # 按工程抽样批量重放（--limit 每个工程最多取几个）
    python regression/replay.py --session reports/2026-09-20-1 \
        --project trng_test --project i3c_dualcore_test --limit 2

    # 校验用：用已有 .json 反推 .md 并与原报告逐字节比对
    python regression/replay.py --session reports/2026-09-20-1 --render-from-json

    # 纯 L2 路径（2026-09-20 A1）：直接调 common/process.process_archive()，
    # 不经 main()、不猴补丁硬件层 —— L1/L2 边界变成接口后的直接形态
    python regression/replay.py --session reports/2026-09-20-1 \
        --project trng_test --l2-direct --freeze-time

**默认输出目录 `tmp/replay/<会话名>/`，绝不覆盖 `reports/`**（写进 reports/ 需
显式 `--out reports/xxx --force`）。

## 适配器（为什么需要）

24 份 `run_test.py` 分 5 个判定家族，`evaluate_case` / `capture_case` / `run_case`
的**签名与返回结构各不相同**（单核 `(ts, line)` 元组 vs 单核 `ports` 字典 vs 双核
`ctrl_lines/tgt_lines`…）。所以重放侧必须有一层**接线**：把归档的行按该家族
期望的形状组装好，塞进 L1 采集层的同名函数位置。**接线层不含任何判定逻辑**——
它只构造入参、替换硬件调用，其余全部走工程自身的代码。

★ **补丁目标 = `common/acquire.py` 的模块全局名**（`acquire.run_case_shell` /
`acquire.capture_case` / `acquire.run_case_dual` / `acquire.run_case_mbox` /
`acquire.run_pass_iwdg` / `acquire.SerialCapture` / `acquire.memory_backfill`）。
**不是** `run_test.py` 里的名字：2026-09-20 A1 收敛 L2 + A2 抽出 L1 之后，24 份
薄壳只剩 `acquire.<策略>(spec)`，`mod.run_case` 之类的名字**已不存在** —— 补丁会
打空，重放就会**落到真硬件**。`mod` 参数保留只为兼容调用方签名，不用于打补丁。

> **2026-09-20 A1 起**：判定 → 渲染 → 落盘已收敛到 `common/process.py`
> （`L2 处理层`，SPEC_REFERENCE §6.4.8）。适配器现在**只用于替换 L1 采集层**
> （`run_case_shell` / `capture_case` / `run_case_dual` / `run_case_mbox` /
> `run_pass_iwdg` / `SerialCapture` / mem 兜底）。
> `--l2-direct` 则完全绕开本文件的适配器与 `main()`，直接调
> `process.process_archive()` —— 两条路径产出**逐字节相同**（SPEC_REFERENCE §6.4.9）。

**隔离网**：每个适配器先调 `_guard_acquire()` 把 `acquire` 上所有能碰板子/进程的
入口换成替身（置空 or 硬中止 `HardwareTouchAttempt`），再覆盖本策略要用的采集入口。
任何未被预期触达的硬件入口 ⇒ **抛异常中止整批**，而不是静默回落（见 `replay_one`）。

## 边界（重要：用户要靠它决定"要不要重跑板子"）

| 改动类型 | 可否重放 | 说明 |
|---|---|---|
| 判定逻辑（`evaluate_case` / `judge_case` / 各家族谓词） | ✅ | 输入就是串口行 |
| 报告渲染（`write_markdown_report` / 表头 / 列序 / 转义） | ✅ | 输入是判定结果 |
| 报告 JSON 落盘、summary 索引 | ✅ | 同上 |
| 串口解析（`parse_ztest` / 各家族行解析） | ✅ | 输入就是原始行 |
| **内存兜底（`dump_results.py` 读目标内存）** | ❌ | **内存内容未归档**：重放一律走 `mem-unavailable` 分支 |
| 构建/烧录/串口时序、环境预检 | ❌ | 与脚本无关，必须上板 |
| 固件本身（`.c/.h`） | ❌ | 输出会变，必须重跑 |

⇒ 判据：**改动只碰"串口行 → 报告"这条链**（解析/判定/渲染），重放可替代上板；
**改动碰采集层、内存兜底、固件**，必须重跑。
"""
import argparse
import importlib.util
import json
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # 810_driver_test
sys.path.insert(0, str(ROOT / "common"))
from serial_archive import read_serial_log, read_meta  # noqa: E402
import acquire                                          # noqa: E402  —— L1 采集层

REPORTS_ROOT = ROOT / "reports"
DEFAULT_OUT_ROOT = ROOT / "tmp" / "replay"

# ------------------------------------------------------------------ 家族表
# 按"硬件采集层"的形状分家族（决定接线方式），与判定家族一一对应。
SINGLE_CAP = {                    # 单核 SerialCapture + run_case -> tgt_lines[(ts,line)]
    "adc_temper_test", "dma_test", "emmc_test", "flash_test", "iopmp_test",
    "pwm_test", "qspi_flash_test", "wdt_test", "eth_test",
}
PORTS_DICT = {                    # 单核 capture_case -> (ports, ok, err)
    "trng_test", "i2cf_test", "io_probe_test", "spi_test", "spid_test",
    "spif_test", "otbn_test", "otbn_selftest", "mbedtls_test", "rtc_ls_test",
}
DUAL_RUN = {                      # 双核 run_case -> ctrl_lines/tgt_lines
    "i3c_dualcore_test", "i2c_pure_test", "i3c_i2c_switch_test",
}
MBOX = {"mbox_test"}              # 双核，lines 是 (ts, 字节块)
IWDG = {"app_iwdg_cpu2_test"}     # 双核，main() 内直接持有 SerialCapture
ALL_PROJECTS = SINGLE_CAP | PORTS_DICT | DUAL_RUN | MBOX | IWDG
DUAL_PROJECTS = DUAL_RUN | MBOX | IWDG


# =================================================================== 工具
def project_script(project):
    return ROOT / "projects" / project / "tests" / "run_test.py"


def split_case_dir(name):
    """`<driver>_<case>` → (project, case)；按最长工程名前缀匹配。"""
    for p in sorted(ALL_PROJECTS, key=len, reverse=True):
        if name.startswith(p + "_"):
            return p, name[len(p) + 1:]
    return None, None


def _drop_project_local_modules():
    """清掉上一工程遗留的**工程本地**模块，避免裸名 import 串味。

    ★ 为什么需要（2026-09-22 实测）：重放在**同一进程**里依次加载多个工程，而工程
      脚本用**裸名** import 自己的辅助模块（如 `from dump_results import TEST_NAMES`）。
      首个 import 过的工程会把它缓存进 `sys.modules` ⇒ 之后别的工程再 import 同名
      模块就**拿到上一个人的**。实测：`mbedtls_test` 先加载并缓存了它自己的
      `dump_results`（只有 `unit_test_names`），后到的 `otbn_selftest` /
      `otbn_test` 执行 `from dump_results import TEST_NAMES` 直接抛
      `ImportError` ⇒ **`--l2-direct` 全量重放在半路整批断掉**，后半程 case
      完全没被验证（却看起来像"跑完了"）。

    判据：模块 `__file__` 落在 `<repo>/projects/` 之下即为"工程本地"。
    `common/` 下的共享模块**不动**（本就全局共享、无同名冲突）。
    工程脚本自身的 `_replay_<project>` 也会被清掉 —— 无妨，下次加载会重建。
    """
    root = (Path(__file__).resolve().parents[1] / "projects").resolve()
    for name, mod in list(sys.modules.items()):
        f = getattr(mod, "__file__", None)
        if not f:
            continue
        try:
            if root in Path(f).resolve().parents:
                del sys.modules[name]
        except (OSError, ValueError):
            pass                          # 路径异常不该拖垮重放


def load_project_module(project):
    """按路径加载工程 `run_test.py`（模块名唯一，避免 24 份同名互撞）。"""
    _drop_project_local_modules()         # ★ 先清工程本地模块，防裸名 import 串味
    path = project_script(project)
    name = f"_replay_{project}"
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


def freeze_datetime(mod, fixed):
    """把模块里的 `datetime` 换成固定时刻的替身，使渲染输出可逐字节比对。

    渲染函数普遍用 `datetime.now().strftime(...)` 填报告「日期」——不冻结则
    每次重放的时间戳都不同，"渲染是否等价"就无法用 diff 判。
    """
    class FrozenDT:
        _fixed = fixed

        @classmethod
        def now(cls):
            return cls()

        def strftime(self, fmt):
            return self._fixed.strftime(fmt)

        def isoformat(self, *a, **k):
            return self._fixed.isoformat(*a, **k)

        def __format__(self, spec):
            # 渲染层有 `f"{now:%Y-%m-%d %H:%M:%S}"` 写法（双核族），须透传
            return format(self._fixed, spec)

        def __str__(self):
            return str(self._fixed)

    mod.datetime = FrozenDT


def _noop(*_a, **_k):
    return None


def facts_from_argv(argv):
    """从 replay 组装的 argv 里取出「未归档的采集事实」（各工程 `l2_spec` 的入参）。

    直接复用 `build_argv()` 的结果 ⇒ 与 main() 路径看到的 argparse 值逐字一致
    （含 `type=float` 的转换：`--timeout 30` → `30.0`，报告「环境」行因此不漂移）。
    """
    out = {}
    for k, conv in (("--case", str), ("--tgt-uart", str), ("--ctrl-uart", str),
                    ("--timeout", float), ("--case-timeout", int)):
        if k in argv:
            out[k[2:].replace("-", "_")] = conv(argv[argv.index(k) + 1])
    return out


def replay_l2_direct(project, case, case_dir, out_root, freeze_time=False):
    """**纯 L2 重放**：读归档 → `common/process.process_archive()`。

    不经 `main()`、不猴补丁硬件层 —— 这是 L1/L2 边界变成接口之后的直接形态
    （见 `common/process.py` 文件头）。产出与 `main()` 路径必须逐字节相同。
    """
    import process                                  # common/process.py
    meta = read_meta(case_dir)
    out_session = out_root / case_dir.parent.name
    out_case = out_session / case_dir.name
    out_case.mkdir(parents=True, exist_ok=True)

    mod = load_project_module(project)
    if freeze_time and meta.get("timestamp"):
        try:
            freeze_datetime(mod, datetime.fromisoformat(meta["timestamp"]))
        except ValueError:
            pass
    argv = build_argv(project, case, out_session, meta)
    facts = facts_from_argv(argv)
    try:
        spec = mod.l2_spec(facts)
    except TypeError:                               # 需要逐 case 测试项表的工程
        spec = mod.l2_spec(facts, None)
    outcome = process.process_archive(spec, case, case_dir, session=out_session,
                                      write=True, console=True)
    return True, out_case, f"L2 直调 rc={outcome.rc}"


# =============================================================== 硬件层隔离
# ★ 2026-09-20（A2 之后）：L1 采集层已收敛到 `common/acquire.py`，24 份 `run_test.py`
#   薄壳**不再有** `run_case` / `capture_case` / `SerialCapture` / `run_pass` /
#   `memory_backfill` 这些名字。适配器必须打补丁到 **`acquire` 模块的全局名**上
#   —— 否则补丁打空，重放会**落到真硬件**（占板子/起 DebugServer）。
class HardwareTouchAttempt(Exception):
    """重放路径意外触达真实硬件/调试器入口 —— 立即中止整批，绝不继续。

    刻意继承 `Exception`（**不是** `RuntimeError`）：`acquire.dual_run` 用
    `except RuntimeError` 做环境自愈重试，继承 `RuntimeError` 会被它误吞成"重试"。
    """


def _forbidden(name):
    def _raise(*_a, **_k):
        raise HardwareTouchAttempt(
            f"重放不允许调用 acquire.{name}()（会碰板子 / 起 gdb / 起 test.sh / 构建）")
    return _raise


# 重放期间会被**合法**调用、但不得产生硬件副作用的名字（置空为无害值）。
_ACQ_SILENT = ("clean_gdb_residue", "clean_gdb_residue_plain", "_clean_gdb_residue",
               "restart_debugserver", "restart_debugserver_stable",
               "_restart_debugserver", "preflight_gdb", "kill_residual_gdb")
# 重放绝不该走到的名字（真烧录 / 起 gdb / 起 test.sh / 构建） —— 硬中止。
_ACQ_FORBIDDEN = ("gdb_flash_once", "gdb_watch_session", "spawn_test_run",
                  "kill_proc_group", "build_via_test_sh", "build_via_test_sh_raise",
                  "west_build", "_serial")
# 5 个 L1 采集入口：先全部硬中止，再由各策略按需换成"喂归档行"的替身。
_ACQ_CAPTURE_ENTRIES = ("run_case_shell", "run_case_dual", "run_case_mbox",
                        "run_pass_iwdg", "capture_case")


class _ForbiddenCapture:
    """`acquire.SerialCapture` 的硬中止替身（未被某策略替换时的兜底）。"""

    def __init__(self, *_a, **_k):
        raise HardwareTouchAttempt("重放不允许实例化 acquire.SerialCapture（会开串口）")


def _guard_acquire(acq):
    """给 `acquire` 装"硬件层隔离网"：所有能碰板子/进程的入口换替身。

    顺序敏感：**先装隔离网，再由策略覆盖采集入口** —— 颠倒会让策略替身被盖掉。
    只动"连调试器 / 起串口 / 杀进程 / 重启 server / 构建 / 读内存"这一层；
    判定与渲染函数一律不碰。
    """
    for n in _ACQ_SILENT:
        if hasattr(acq, n):
            setattr(acq, n, _noop)
    for n in _ACQ_FORBIDDEN:
        if hasattr(acq, n):
            setattr(acq, n, _forbidden(n))
    for n in _ACQ_CAPTURE_ENTRIES:
        if hasattr(acq, n):
            setattr(acq, n, _forbidden(n))

    acq.find_git_bash = lambda *_a, **_k: "bash"
    if hasattr(acq, "find_tgt_uart"):
        acq.find_tgt_uart = lambda: "REPLAY_TGT"
    if hasattr(acq, "discover_ports"):
        acq.discover_ports = lambda explicit=None: list(explicit or [])
    if hasattr(acq, "list_serial_ports"):
        acq.list_serial_ports = lambda *_a, **_k: []
    if hasattr(acq, "probe_gdb_alive"):
        acq.probe_gdb_alive = lambda *_a, **_k: False
    if hasattr(acq, "netstat_1025"):
        acq.netstat_1025 = lambda *_a, **_k: []

    # 内存兜底置空（内存内容未归档，见文件头「边界」表）：返回 None ⇒ 工程走它
    # 自己的 mem-unavailable 分支，而不是跳过一个分支。eth 的 `mem_hook` 是
    # `lambda c: acquire.dump_mem(...)`，调用时按属性解析 ⇒ 同样命中本替身。
    for n in ("memory_backfill", "dump_mem"):
        if hasattr(acq, n):
            setattr(acq, n, lambda *_a, **_k: None)
    acq.SerialCapture = _ForbiddenCapture


# =============================================================== 适配器
def apply_single_cap(mod, cores, meta):
    tgt = list(cores.get("TGT", []))

    def fake_run_case_shell(spec, case_name, tgt_uart, timeout, bash):
        return {"case": case_name, "ctrl_lines": [],
                "tgt_lines": [(0.0, ln) for ln in tgt],
                "proc_lines": [], "timed_out": False}

    _guard_acquire(acquire)
    acquire.run_case_shell = fake_run_case_shell


def apply_ports_dict(mod, cores, meta):
    tgt = list(cores.get("TGT", []))

    def fake_capture_case(spec, case, tgt_uart, timeout):
        return ({tgt_uart or "REPLAY_TGT": {"lines": list(tgt),
                 "stop": threading.Event()}}, True, "")

    _guard_acquire(acquire)
    acquire.capture_case = fake_capture_case


def apply_dual_run(mod, cores, meta):
    ctrl, tgt = list(cores.get("CTRL", [])), list(cores.get("TGT", []))

    def fake_run_case_dual(spec, case_dir, bash):
        return {"case": getattr(case_dir, "name", str(case_dir)),
                "ctrl_lines": list(ctrl), "tgt_lines": list(tgt),
                "load_failed": False, "timed_out": False}

    _guard_acquire(acquire)
    acquire.run_case_dual = fake_run_case_dual


def apply_mbox(mod, cores, meta):
    ctrl, tgt = list(cores.get("CTRL", [])), list(cores.get("TGT", []))

    def fake_run_case_mbox(spec, case_name, ctrl_uart, tgt_uart, timeout, bash,
                           core_view, combined_lines):
        # mbox 的 `_split_lines` = 把 (时刻, 字节块) 拼接后按 '\n' 切 ⇒ 单块喂回
        # `"\n".join(归档行)` 即**逐行还原**（末尾不留空行）。
        return {"case": case_name,
                "ctrl_lines": [(0.0, "\n".join(ctrl))],
                "tgt_lines": [(0.0, "\n".join(tgt))],
                "timed_out": False, "elapsed_s": 0.0}

    _guard_acquire(acquire)
    acquire.run_case_mbox = fake_run_case_mbox


def apply_iwdg(mod, cores, meta):
    """iwdg 的 `acquire.iwdg()` 自己 new SerialCapture 并跑两遍（prime + capture）。

    ⇒ 替换 `acquire.SerialCapture`（假类，`clear()` 时按归档行重新填充 = 第二遍
    采集）与 `acquire.run_pass_iwdg`（不再起 test.sh）。

    核→端口映射取自 `meta`（与 `build_argv()` 同源）并**闭包持有**：不依赖模块全局
    `CTRL_PORT` / `TGT_PORT`（那会让假类在全局未就绪时 `NameError`）。
    """
    ctrl, tgt = list(cores.get("CTRL", [])), list(cores.get("TGT", []))
    meta_cores = meta.get("cores", {}) or {}
    ctrl_port = (meta_cores.get("CTRL") or {}).get("port") or "REPLAY_CTRL"
    tgt_port = (meta_cores.get("TGT") or {}).get("port") or "REPLAY_TGT"

    class FakeCap:
        def __init__(self, port, baud=115200, *_a, **_k):
            self.port = port
            self.baud = baud
            self.error = None
            self.lines = []
            self._fill()

        def _fill(self):
            if self.port == ctrl_port:
                picked = ctrl
            elif self.port == tgt_port:
                picked = tgt
            else:
                picked = []
            self.lines = [(0.0, ln) for ln in picked]

        def start(self):
            self._fill()

        def stop(self):
            pass

        def clear(self):
            self._fill()

        @property
        def text(self):
            return "\n".join(ln for _, ln in self.lines)

        def plain_lines(self):
            return [ln for _, ln in self.lines]

    _guard_acquire(acquire)
    acquire.SerialCapture = FakeCap
    acquire.run_pass_iwdg = (
        lambda spec, case_name, ctrl_cap, tgt_cap, timeout, bash: ([], False, 0))


ADAPTERS = {}
for _p in SINGLE_CAP:
    ADAPTERS[_p] = apply_single_cap
for _p in PORTS_DICT:
    ADAPTERS[_p] = apply_ports_dict
for _p in DUAL_RUN:
    ADAPTERS[_p] = apply_dual_run
for _p in MBOX:
    ADAPTERS[_p] = apply_mbox
for _p in IWDG:
    ADAPTERS[_p] = apply_iwdg


# =================================================================== 主流程
def build_argv(project, case, out_session, meta):
    cores = meta.get("cores", {}) or {}
    tgt = (cores.get("TGT") or {}).get("port") or "REPLAY_TGT"
    ctrl = (cores.get("CTRL") or {}).get("port") or "REPLAY_CTRL"
    timeout = int(meta.get("timeout") or 0)
    argv = ["run_test.py", "--skip-build", "--session-dir", str(out_session),
            "--case", case]
    if project in DUAL_PROJECTS:
        argv += ["--ctrl-uart", ctrl, "--tgt-uart", tgt]
        argv += ["--case-timeout", str(timeout or 120)]
    elif project in PORTS_DICT:
        argv += ["--tgt-uart", tgt, "--timeout", str(timeout or 30)]
    else:                                   # SINGLE_CAP
        argv += ["--tgt-uart", tgt, "--case-timeout", str(timeout or 60)]
    return argv


def replay_one(project, case, case_dir, out_root, freeze_time=False):
    """重放单个 case；返回 `(ok, out_case_dir, note)`。"""
    meta = read_meta(case_dir)
    cores = {}
    for core in ("CTRL", "TGT"):
        f = case_dir / f"serial_{core}.log"
        if f.exists():
            cores[core] = read_serial_log(f)
    if not cores:
        return False, None, "无串口归档（serial_*.log 缺失）"

    out_session = out_root / case_dir.parent.name
    out_case = out_session / case_dir.name
    out_case.mkdir(parents=True, exist_ok=True)

    mod = load_project_module(project)
    if freeze_time and meta.get("timestamp"):
        try:
            freeze_datetime(mod, datetime.fromisoformat(meta["timestamp"]))
        except ValueError:
            pass
    ADAPTERS[project](mod, cores, meta)

    argv = build_argv(project, case, out_session, meta)
    old_argv = sys.argv
    try:
        sys.argv = argv
        rc = mod.main()
    except SystemExit as exc:                       # main() 正常 return，防御性兜底
        rc = exc.code if isinstance(exc.code, int) else 0
    except HardwareTouchAttempt:
        # 重放试图触达真硬件 ⇒ **立刻中止整批**（绝不静默跳过：静默会掩盖
        # "补丁又打空了"这类回归，而这正是 A2 打破 replay 的方式）。
        raise
    except Exception as exc:                        # noqa: BLE001
        # 单 case 失败不阻断其余 case（批量重放语义）
        return False, None, f"main() 抛异常: {type(exc).__name__}: {exc}"
    finally:
        sys.argv = old_argv
    return True, out_case, f"main() rc={rc}"


# --------------------------------------------------- 报告重渲染（校验用）
def _json_report_path(case_dir):
    cands = sorted(case_dir.glob("*_report.json"))
    return cands[0] if cands else None


def rerender_from_json(case_dir, out_case):
    """把归档 `.json` 喂回工程自己的 `write_markdown_report()`，产出 `.md`。

    返回 `(ok, note)`。**只支持有归档 json 且渲染函数签名已登记的工程**。
    """
    project, case = split_case_dir(case_dir.name)
    if project is None:
        return False, "无法识别工程"
    jpath = _json_report_path(case_dir)
    if jpath is None:
        return False, "无 *_report.json"
    data = json.loads(jpath.read_text(encoding="utf-8"))
    mod = load_project_module(project)
    ts = data.get("timestamp") or datetime.now().isoformat()
    try:
        freeze_datetime(mod, datetime.fromisoformat(ts))
    except ValueError:
        pass
    try:
        expected = mod.parse_expected_from_coverage()
    except Exception:                               # noqa: BLE001
        expected = {}
    out_case.mkdir(parents=True, exist_ok=True)

    if project in DUAL_RUN:                         # write_markdown_report(results, expected, dir, driver)
        results = data.get("cases", [])
        mod.write_markdown_report(results, expected, out_case, mod.DRIVER_NAME)
        return True, f"dual 渲染（{len(results)} case）"

    if project in PORTS_DICT:
        results = data.get("results", {})
        infos = data.get("infos", {})
        reasons = data.get("reasons", {})
        notes = data.get("notes", {})
        execution = data.get("execution", "UNKNOWN")
        mem_line = mod.mem_state_line(data.get("mem_state", ""), data.get("mem_note", ""))
        missing = data.get("missing_evidence", [])
        passed = sum(1 for v in results.values() if v == "PASS")
        ran = sum(1 for v in results.values() if v not in ("NOT_RUN", "SKIP"))
        declared = list(results)
        verdict = mod.judge_case(results, passed, ran, execution, declared)
        case_ok = verdict["ok"] and not mod.mem_only_evidence(notes.values())
        env_line = data.get("env", "")
        if project == "trng_test":
            mod.write_markdown_report(case, results, infos, reasons, notes, expected,
                                      out_case, env_line, execution, mem_line,
                                      missing, case_ok, passed, ran)
        elif project == "rtc_ls_test":
            mod.write_markdown_report(results, infos, reasons, notes, expected,
                                      out_case, env_line, execution, mem_line,
                                      case, case_ok, passed, ran)
        else:                                       # otbn_test / otbn_selftest / mbedtls_test …
            mod.write_markdown_report(case, results, infos, reasons, notes, expected,
                                      execution, out_case, env_line, mem_line,
                                      missing, case_ok, passed, ran)
        return True, f"单核渲染（{passed}/{ran}）"

    return False, f"该工程（{project}）的渲染接线未登记"


def diff_md(new_dir, orig_dir):
    """逐字节比对重放 .md 与原始 .md；返回 (相同?, 差异说明)。"""
    new = sorted(new_dir.glob("*_report.md"))
    old = sorted(orig_dir.glob("*_report.md"))
    if not new or not old:
        return False, f"md 缺失（new={len(new)} old={len(old)}）"
    a = new[0].read_bytes()
    b = old[0].read_bytes()
    if a == b:
        return True, f"逐字节相同（{len(a)} bytes）"
    al, bl = a.decode("utf-8", "replace").splitlines(), b.decode("utf-8", "replace").splitlines()
    diffs = [f"L{i+1}: 新={x!r} 旧={y!r}" for i, (x, y) in enumerate(zip(al, bl)) if x != y]
    return False, (f"不同（new {len(a)}B / old {len(b)}B，{len(diffs)} 行差异）"
                   + ("；" + diffs[0] if diffs else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--session", required=True, help="归档会话目录（reports/<date>-<idx>）")
    ap.add_argument("--case", action="append", default=None, help="指定 case（可重复）")
    ap.add_argument("--project", action="append", default=None,
                    help="按工程抽样（可重复；与 --case 二选一）")
    ap.add_argument("--limit", type=int, default=0, help="每个工程最多重放几个（抽样）")
    ap.add_argument("--out", default=None, help="输出目录（默认 tmp/replay/<会话名>）")
    ap.add_argument("--force", action="store_true", help="允许 --out 落在 reports/ 内")
    ap.add_argument("--list", action="store_true", help="只列出可重放的 case")
    ap.add_argument("--freeze-time", action="store_true",
                    help="把渲染时钟冻结到归档时间戳（便于与原报告逐字节比对）")
    ap.add_argument("--render-from-json", action="store_true",
                    help="校验模式：用归档 .json 反推 .md 并与原报告比对")
    ap.add_argument("--l2-direct", action="store_true",
                    help="★ 真正离线：纯 L2 路径，直接调 common/process.process_archive()"
                         "（不经 main()、不猴补丁硬件层，实测 0.0s/case）。"
                         "**不带本开关会走工程 main()（含 DebugServer 前置检查）⇒ 不是离线、"
                         "DS 未起时每 case 白等 ~52s 且不产出报告**")
    args = ap.parse_args()

    session = Path(args.session)
    if not session.is_dir():
        session = REPORTS_ROOT / args.session
    if not session.is_dir():
        print(f"ERROR: 会话目录不存在: {args.session}", file=sys.stderr)
        return 2

    case_dirs = sorted(d for d in session.iterdir()
                       if d.is_dir() and split_case_dir(d.name)[0])
    if args.case:
        want = set(args.case)
        case_dirs = [d for d in case_dirs if d.name in want
                     or split_case_dir(d.name)[1] in want]
    if args.project:
        case_dirs = [d for d in case_dirs if split_case_dir(d.name)[0] in set(args.project)]
    if args.limit > 0 and not args.case:
        picked, seen = [], {}
        for d in case_dirs:
            p = split_case_dir(d.name)[0]
            seen[p] = seen.get(p, 0) + 1
            if seen[p] <= args.limit:
                picked.append(d)
        case_dirs = picked

    if not case_dirs:
        print("没有匹配的 case。", file=sys.stderr)
        return 2
    if args.list:
        print(f"会话 {session.name}：{len(case_dirs)} 个 case")
        for d in case_dirs:
            p, c = split_case_dir(d.name)
            has = "serial归档" if (d / "serial_TGT.log").exists() or \
                (d / "serial_CTRL.log").exists() else "无串口归档"
            print(f"  {p:<22} {c:<28} [{has}]")
        return 0

    out_root = Path(args.out) if args.out else DEFAULT_OUT_ROOT / session.name
    try:
        out_root.resolve().relative_to(REPORTS_ROOT.resolve())
        if not args.force:
            print(f"ERROR: 输出目录在 reports/ 内（{out_root}）；重放不得覆盖原报告。"
                  f"换 --out，或显式 --force。", file=sys.stderr)
            return 2
    except ValueError:
        pass

    print(f"重放会话 : {session}")
    print(f"输出目录 : {out_root}")
    print(f"模式     : {'报告重渲染（json→md 校验）' if args.render_from_json else '串口重放'}")
    if not args.l2_direct and not args.render_from_json:
        print("[WARN] 未加 --l2-direct ⇒ **这不是纯离线重放**：本模式会走工程 main()，"
              "其中含 DebugServer 前置检查。DS 未起时**每个 case 白等自愈超时"
              "（实测 51.7s）且不产出任何报告** —— 表现为「跑很久、目录是空的」。\n"
              "       只改 L1/L2 脚本、不碰板子时请加 --l2-direct（实测 0.0s/case、真离线）。",
              file=sys.stderr)
    print("-" * 72)

    n_ok = n_same = n_fail = 0
    for case_dir in case_dirs:
        project, case = split_case_dir(case_dir.name)
        out_case = out_root / session.name / case_dir.name
        t0 = time.time()
        if args.render_from_json:
            ok, note = rerender_from_json(case_dir, out_case)
        elif args.l2_direct:
            ok, out_case, note = replay_l2_direct(project, case, case_dir, out_root,
                                                  freeze_time=args.freeze_time)
        else:
            ok, out_case, note = replay_one(project, case, case_dir, out_root,
                                            freeze_time=args.freeze_time)
        if not ok:
            n_fail += 1
            print(f"[SKIP] {case_dir.name:<45} {note}")
            continue
        n_ok += 1
        same, dnote = diff_md(out_case, case_dir)
        n_same += same
        meta = read_meta(case_dir)
        md5_note = ""
        if meta.get("script_md5"):
            import hashlib
            try:
                cur = hashlib.md5(project_script(project).read_bytes()).hexdigest()
            except OSError:
                cur = ""
            md5_note = " | 脚本md5: " + ("未变" if cur == meta["script_md5"]
                                          else f"已变({meta['script_md5'][:8]}→{cur[:8]})")
        print(f"[{'等价' if same else '差异'}] {case_dir.name:<45} "
              f"{time.time()-t0:5.1f}s | {note}{md5_note}")
        print(f"          {dnote}")

    print("-" * 72)
    print(f"完成：重放 {n_ok} / 与归档逐字节相同 {n_same} / 跳过 {n_fail}")
    if not args.render_from_json and n_same < n_ok:
        print("提示：差异常见来源 = 报告「日期」字段用重放时刻（加 --freeze-time 可比对）"
              "，或该 case 在线时用过**内存兜底**（内存未归档，重放走 mem-unavailable）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
