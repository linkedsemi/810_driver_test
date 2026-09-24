#!/usr/bin/env python3
"""L2 处理层（processing layer）：**归档 → 判定 → 报告**（A1，2026-09-20）。

## 这个文件是什么

`run_test.py` 的 `main()` 此前把七件事揉在一段里（构建 / 烧录 / 采集 / 判定 /
渲染 / 落盘 / 汇总）。其中**采集完到落盘**这一段的边界其实已经存在 ——
每份 `run_test.py` 里那行 `archive_serial()` 就是 L1/L2 的分界线：它之前碰硬件，
之后是纯数据处理。本模块把这条线**提成真接口**。

    L1 采集（唯一碰板子） ──┐
                            │  L0 归档 = serial_<CORE>.log + replay_meta.json
    L2 处理（纯离线）  ◄────┘

## 为什么叫 `process.py`

* 与既有分层命名一致（`serial_archive.py` = L0 契约、`replay.py` = L2 重放驱动器）；
* 层名取 L2 的通用语义（processing），**不绑定任何单个工程**；
* `common/` 已在各 `run_test.py` 的 `sys.path` 上，`import process` 零配置。

> 备选名 `l2.py` / `pipeline.py` 被否：前者绑层号（层号会随架构演进变），
> 后者暗示"多步管道"（本层其实是"一次判定 + 一次落盘"）。

## 输入输出契约（L0）

**输入**（二选一，形状相同）：

* 在线：L1 采集到的原始行 `{核名: [str 行, ...]}`（**就是归档进去的那一份**）；
* 离线：`replay_meta.json` + `serial_<CORE>.log` 组成的归档目录。

**输出**：

* 判定结果（各家族自己的结构，原样保留 —— 本层不归一化家族语义）；
* `<driver>_report.json` / `<driver>_report.md`（由 spec 的渲染钩子写，字节不变）；
* `summary.md` 一行；进程返回码 0/1。

**契约要点**：L2 的输入里**没有**端口名 / 超时 / gdb —— 那些只作 `replay_meta.json`
的元信息。这正是"改处理层不必重跑板子"成立的形式化条件。

## 为什么用 spec（钩子）而不是在 process.py 里分家族

24 份 `run_test.py` 分 7 个判定家族，家族差异是**真实语义差异**（双核结构不对称 /
mbox 标记式 / rtc_ls 自定义 token / iwdg 双 pass），不是噪音。把 `if family == ...`
写进本文件 = 把 24 份的分叉搬进一个文件，可读性和可验证性都不会更好。

所以：**本文件只做"所有家族逐字同构"的那部分**（读归档 / 调判定 / 调渲染 /
落盘 / 汇总 / 算返回码），家族特有的谓词与渲染由各工程用 `ProcessSpec` 传进来。
`ProcessSpec` 的每个字段都是**可空钩子**，缺省行为即"该家族此前的写法"。

设计约束（与 `judge.py` 同款）：

  1. **纯离线、不碰硬件**：本模块不 import serial / subprocess / gdb；
  2. **不改家族语义**：判定结果原样透传，不归一化、不补字段、不排序；
  3. **字节不变**：落盘一律复用各工程既有的渲染函数与既有的写入调用
     （`Path.write_text(..., encoding="utf-8")`，**不改成 `newline=""`** ——
     本仓报告文件是 CRLF，换写法会翻转行尾）。
"""
from dataclasses import dataclass, field
from pathlib import Path

__all__ = [
    "ARCHIVE_CORES",
    "read_archive",
    "L2Input",
    "ProcessSpec",
    "Outcome",
    "judge",
    "emit",
    "rc",
    "run",
    "process_archive",
]

#: 归档的核名。双核 `CTRL`/`TGT`，单核固定 `TGT`（`serial_archive.serial_log_name`）。
ARCHIVE_CORES = ("CTRL", "TGT")


# =============================================================== L0 读侧
def read_archive(case_dir, cores=ARCHIVE_CORES):
    """读 L0 归档 → `{核名: [原始行]}`。

    只含**实际存在**的 `serial_<CORE>.log`；缺失的核不出现在返回值里（调用方
    据此走"该核无归档"分支，而不是拿到一个空表被误读成"该核采集到 0 行"）。
    行内容由 `serial_archive.read_serial_log` 保证与写入侧逐行等价。
    """
    import serial_archive                      # 延迟 import：本模块不建立循环依赖
    case_dir = Path(case_dir)
    out = {}
    for core in cores:
        f = case_dir / serial_archive.serial_log_name(core)
        if f.exists():
            out[core] = serial_archive.read_serial_log(f)
    return out


# =============================================================== L2 输入
@dataclass
class L2Input:
    """一次 L2 判定的输入（= L0 归档的形状）。

    Attributes:
        case:  case 名。
        lines: `{核名: [原始行]}`。**在线路径与离线路径传的是同一份数据** ——
               在线时先按这个形状归档，再把同一个对象交给 L2，因此"离线重放"
               与"在线判定"的输入逐字相同。
        mem:   内存兜底 payload（`dump_results.py --json` 的产物）。**这是 L1 侧
               产物、不进归档** ⇒ 离线路径恒为 `None`（家族按自己的
               `mem-unavailable` 分支走）。这是当前 L0 契约的**已知缺口**
               （D7）：改内存兜底逻辑永远无法离线证。
        facts: **未归档的采集事实**（如 `timed_out` / `elapsed_s`）。在线路径由
               L1 填，离线路径取缺省值 ⇒ 只影响报告里"采集时长"这类
               **明确声明不参与判定**的字段。
    """
    case: str
    lines: dict = field(default_factory=dict)
    mem: object = None
    facts: dict = field(default_factory=dict)


# =============================================================== L2 规格
@dataclass
class ProcessSpec:
    """一个工程的 L2 处理规格：声明"本工程特有"的部分。

    Args:
        project:     工程名（= `run_test.py` 所在目录名，用于归档路径）。
        driver:      `DRIVER_NAME`（报告标题 / 文件名 / summary key 前缀）。
        report_base: `${driver}_report`。
        judge:       ★家族特有：`L2Input -> judged`。**判定的全部家族语义都在这**
                     （谓词、mem 合并门、零证据守卫、矛盾标记）。返回值结构由
                     家族自定，本层原样透传。
        render_md:   `(judged_list, case_dir) -> Path|None`；None = 不写 .md。
        render_json: `(judged_list, case_dir) -> Path|None`；None = 按缺省写法写
                     `<report_base>.json`（`json.dumps(judged, indent=2,
                     ensure_ascii=False)`，与 Z/eth/iwdg/mbox 族既有写法逐字一致）。
        console:     `(judged_list) -> None`；缺省不打印。
        summary:     `(judged_list) -> (status, passed, total)`；`passed`/`total`
                     可为 None（O-12：拿不到计数写"未知"）。
        status:      `judged -> "PASS"|"FAIL"`；缺省 `judged.get("result")`。
        log_archived: `(json_path|None, md_path|None) -> None`；缺省打印
                     `[run] archived -> <json>, <md>`（Z/eth/iwdg/mbox 族原文）。
    """
    project: str
    driver: str
    report_base: str
    judge: object = None
    render_md: object = None
    render_json: object = None
    console: object = None
    summary: object = None
    status: object = None
    log_archived: object = None
    #: 传给 `case_dir_for()` 的 case 标签（缺省 = case 名）。
    case_label: object = None


@dataclass
class Outcome:
    """一次 L2 处理的产出（供调用方取 rc / 报告路径 / 判定结果）。

    `rc` 由 `run()` 用 spec 的判据一次算好存进 `_rc` —— 聚合状态与单 case
    状态在家族间口径不同（`spec.status`），故不在本类里重新推断。
    """
    judged: list                      # 判定结果列表（单 case 家族恒为长度 1）
    _rc: int = 1
    session: Path = None
    case_dir: Path = None
    report_json: Path = None
    report_md: Path = None

    @property
    def rc(self):
        return self._rc

    @property
    def status(self):
        return "PASS" if self._rc == 0 else "FAIL"


def _default_status(judged):
    if isinstance(judged, dict):
        return judged.get("result") or "FAIL"
    return "FAIL"


# =============================================================== L2 驱动
def judge(spec, ci):
    """判定一跳：把 `L2Input` 交给家族的判据，原样返回结果。"""
    return spec.judge(ci)


def _status(spec, judged):
    fn = spec.status or _default_status
    return fn(judged) or "FAIL"


def _scored_items(items):
    """参与计分的测试项 —— 排除框架注入的**运行元信息**。

    `run::timeout` 等（`result="NOTE"`）是运行元信息、不是测试项：把它算进分母会让
    summary 显示成 `PASS (18/19)`，读的人误以为挂了一项。口径与
    `common/expect_check.py`（「`run::timeout` 这类是运行元信息，不是测试项」）一致。

    ⚠️ 该条目仍然**渲染在各 case 的报告表格里** —— 它存在就是为了让「窗口到点」这件事
    可见；这里只把它从**分母**里拿掉，既不删条目、也不改任何判定逻辑。
    """
    return [x for x in items if not str((x or {}).get("name", "")).startswith("run::")]


def _summary(spec, judged_list):
    if spec.summary is not None:
        return spec.summary(judged_list)
    it = judged_list[0] if len(judged_list) == 1 else None
    items = (it or {}).get("items", []) if isinstance(it, dict) else []
    scored = _scored_items(items)
    passed = sum(1 for x in scored if (x or {}).get("result") == "PASS")
    return _status(spec, it), passed, len(scored)


def _default_render_json(judged_list, case_dir, spec):
    import json
    payload = judged_list[0] if len(judged_list) == 1 else judged_list
    p = Path(case_dir) / f"{spec.report_base}.json"
    # ★ 与既有 24 份 run_test.py 逐字一致的写法（Path.write_text 默认
    #   newline=None ⇒ Windows 下 CRLF；改成 newline="" 会翻转行尾 = 改字节）。
    p.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    return p


def emit(spec, judged_list, session, case_label=None, case_dir=None):
    """渲染 + 落盘 + 汇总（L2 的"写侧"）。

    Returns `(case_dir, json_path, md_path)`。`session` 为 None 时只渲染不落盘
    （调用方负责保证 write=True 时 session 非空 —— 与既有 main() 的语义一致）。
    """
    from report_dir import case_dir_for, update_summary
    label = case_label if case_label is not None else (
        spec.case_label(judged_list) if spec.case_label else None)
    if label is None:
        raise ValueError("emit(): case_label 未提供（聚合家族必须显式传）")
    cdir = Path(case_dir) if case_dir else case_dir_for(session, spec.driver, label)
    cdir.mkdir(parents=True, exist_ok=True)

    jp = mp = None
    if spec.render_md is not None:
        mp = spec.render_md(judged_list, cdir)
    else:
        mp = None
    if spec.render_json is not None:
        jp = spec.render_json(judged_list, cdir)
    else:
        jp = _default_render_json(judged_list, cdir, spec)

    log_a = spec.log_archived
    if log_a is not None:
        log_a(jp, mp)
    else:
        print(f"[run] archived -> {jp}, {mp}", flush=True)

    status, passed, total = _summary(spec, judged_list)
    update_summary(session, f"{spec.driver}_{label}", status, passed, total,
                   f"{cdir.name}/{spec.report_base}.md")
    return cdir, jp, mp


def rc(spec, judged_list):
    """返回码：全部 PASS ⇒ 0，否则 1（与既有各家族 rc 语义一致）。"""
    if not judged_list:
        return 1
    return 0 if all(_status(spec, j) == "PASS" for j in judged_list) else 1


def run(spec, *, case, lines=None, archive_dir=None, session=None,
        write=True, console=True, mem=None, facts=None, case_label=None):
    """单 case 的完整 L2：判定 → 控制台 → 渲染 → 落盘 → 汇总。

    Args:
        lines:       在线路径：L1 采到的 `{核名: [行]}`（**已按 L0 形状归一**）。
        archive_dir: 离线路径：归档目录。与 `lines` 二选一（都给则 `lines` 优先）。
        write:       False = 只判定 + 打印，不落盘（`--no-archive`）。
        session:     `write=True` 时必须非空。
    """
    if lines is None:
        lines = read_archive(archive_dir) if archive_dir is not None else {}
    ci = L2Input(case=case, lines=lines, mem=mem, facts=facts or {})
    judged = judge(spec, ci)
    jl = [judged]
    if console and spec.console is not None:
        spec.console(jl)
    out = Outcome(judged=jl, _rc=rc(spec, jl))
    if write:
        cdir, jp, mp = emit(spec, jl, session, case_label=case_label or case)
        out.session, out.case_dir, out.report_json, out.report_md = session, cdir, jp, mp
    return out


def process_archive(spec, case, case_dir, *, session=None, write=True, console=True):
    """**离线入口**：读归档 → 判定 → 报告。

    这就是 L1/L2 边界变成接口之后的样子 —— 重放不再需要猴补丁硬件层，
    直接调本函数即可（`regression/replay.py` 的主路径）。
    """
    return run(spec, case=case, archive_dir=case_dir, session=session,
               write=write, console=console)
