#!/usr/bin/env python3
"""串口原始输出归档（重放基础设施，2026-09-20）。

动机（用户裁定）：判定层消费的是**串口原始行**（`parse_ztest(lines, ...)` /
各家族自己的行解析），而既有归档只落**派生结果**（`*_report.md` / `*.json`）。
后果：任何判定 / 渲染脚本的改动都无法离线验证，只能**重跑固件**——而整轮
回归的开销主要就花在"重跑固件"上。

修法：把每 case 的**原始行按核分文件**落盘，使"只改脚本、不重跑固件"成为
可能（重放入口见 `regression/replay.py`）。

落盘布局（跟随既有报告会话目录，§12）::

    reports/<会话>/<driver>_<case>/serial_CTRL.log      # controller 核原始行
    reports/<会话>/<driver>_<case>/serial_TGT.log       # target 核原始行
    reports/<会话>/<driver>_<case>/replay_meta.json     # 重放元信息

单核工程写一份 `serial_TGT.log` 即可；双核工程**分核各写一份**——合并后无法
按核重放（两核的行交错会破坏 `parse_ztest` 的核归属判定）。

设计约束（硬要求）：

  1. **纯增量、失败隔离**：`archive_serial()` 整体 try/except，任何异常只打
     一行 warning 并返回 False，**绝不**改变调用方的判定与控制流；
  2. **不参与判定**：本模块不 import 任何判定函数，返回值不被任何判定消费；
  3. **行尾原样**：写文件用 `open(..., newline="")`，不触发 Windows 的
     LF→CRLF 翻转（本仓行尾混用，`Path.write_text()` 会静默改写行尾）。
"""

import hashlib
import json
from datetime import datetime
from pathlib import Path

__all__ = [
    "ARCHIVE_VERSION",
    "SERIAL_LOG_FMT",
    "META_NAME",
    "serial_log_name",
    "archive_serial",
    "read_serial_log",
    "read_meta",
]

# 归档格式版本：读侧（regression/replay.py）据它判断能否理解元信息。
#   1 = 初版（serial_<CORE>.log + replay_meta.json）
#   2 = 单出口分拣（SPEC_REFERENCE §6.9.4）：serial_TGT.log 里的从核行是 L1 按
#       `<PREFIX>:<core>:` 前缀从主核流分拣回来的（从核本地不出 token）。行内容
#       与"两核各自本地输出"同形，只是来源不同 ⇒ 元信息加 split_from_primary。
ARCHIVE_VERSION = 1
ARCHIVE_VERSION_SPLIT = 2

SERIAL_LOG_FMT = "serial_{core}.log"
META_NAME = "replay_meta.json"


def serial_log_name(core):
    """核名 → 归档文件名（双核 `CTRL` / `TGT`，单核固定 `TGT`）。"""
    return SERIAL_LOG_FMT.format(core=core)


def _script_md5(script):
    """`run_test.py` 自身的内容 md5（重放时校验"判定脚本是否变过"）。

    取不到（路径为空 / 文件不可读）返回空串——元信息缺该字段不等于归档失败。
    """
    if not script:
        return ""
    try:
        return hashlib.md5(Path(script).read_bytes()).hexdigest()
    except OSError:
        return ""


def archive_serial(case_dir, case, cores, board="", ports=None, timeout=None,
                   script=None, log=None, split_from_primary=False,
                   envelope_stripped=False):
    """把一 case 的原始串口行按核落盘 + 写重放元信息。

    Args:
        case_dir: 落盘目录（本 case 的报告子目录；不存在则创建）。
        case:     case 名（如 `case_default`）。
        cores:    `{核名: [原始行, ...]}`。**双核必须分开传**（CTRL / TGT 各一份），
                  单核传 `{"TGT": [...]}`。行必须是**字符串列表**，且是判定层
                  实际消费的那一份（`parse_ztest` 的入参）——tuple 时间戳形式请
                  调用方先行解包，时间戳不参与判定、不归档。
        board:    板级串（元信息用，可为空）。
        ports:    `{核名: 串口名}`（元信息用，可为空）。
        timeout:  该 case 的采集超时（秒）。
        script:   调用方 `run_test.py` 路径（通常传 `__file__`）。
        log:      日志回调（默认 print）；归档失败时输出一行 warning。
        split_from_primary: `True` = 从核行是 L1 按 `<PREFIX>:<core>:` 从主核流
                   分拣回来的（单出口，§6.9.4）⇒ 版本记 2 并标注来源。默认
                   `False` ⇒ 元信息与初版逐字相同（其余 23 个工程零影响）。

    Returns:
        bool：归档成功 True；任何异常 False（**调用方不得据此改变控制流**）。
    """
    try:
        case_dir = Path(case_dir)
        case_dir.mkdir(parents=True, exist_ok=True)
        ports = ports or {}

        for core, lines in cores.items():
            lines = [str(ln) for ln in (lines or [])]
            text = "\n".join(lines)
            if text:
                text += "\n"
            with open(case_dir / serial_log_name(core), "w",
                      encoding="utf-8", newline="") as fh:
                fh.write(text)

        meta = {
            "archive_version": (ARCHIVE_VERSION_SPLIT if split_from_primary
                                else ARCHIVE_VERSION),
            "case": case,
            "board": board,
            "cores": {core: {"port": ports.get(core, ""),
                             "lines": len(cores[core] or [])}
                      for core in cores},
            "timeout": timeout,
            "script": str(script) if script else "",
            "script_md5": _script_md5(script),
            "timestamp": datetime.now().isoformat(timespec="seconds"),
        }
        if split_from_primary:
            meta["split_from_primary"] = True
        if envelope_stripped:
            # 从核行是"中继侧加壳后剥壳"的产物（§6.9.4 单出口，ztest 型从核）：
            # 行内容与从核本核打印逐行等价，只是物理来源不同。
            meta["relay_envelope_stripped"] = True
        with open(case_dir / META_NAME, "w",
                  encoding="utf-8", newline="") as fh:
            fh.write(json.dumps(meta, ensure_ascii=False, indent=2))
        return True
    except Exception as exc:                        # noqa: BLE001 —— 失败隔离
        try:
            (log or print)(f"[run] WARNING: serial archive failed ({case}): {exc}")
        except Exception:                           # noqa: BLE001 —— 连日志失败也吞掉
            pass
        return False


def read_serial_log(path):
    """读回归归档的 `serial_<CORE>.log` → 原始行列表（与 `archive_serial` 互逆）。

    写入约定：`"\\n".join(lines) + "\\n"`（非空时）。读侧先去掉**唯一一个**末尾
    换行再按 `\\n` 切，保证 `read_serial_log(write(lines)) == lines`（逐行等价）。
    """
    text = Path(path).read_text(encoding="utf-8")
    if text.endswith("\n"):
        text = text[:-1]
    if text == "":
        return []
    return text.split("\n")


def read_meta(case_dir):
    """读 `replay_meta.json`；缺失 / 损坏返回 `{}`（调用方自行降级）。"""
    try:
        return json.loads((Path(case_dir) / META_NAME).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
