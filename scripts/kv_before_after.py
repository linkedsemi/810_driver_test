#!/usr/bin/env python3
"""B1 缺陷 A 验证：新旧 kv 解析在**真串口归档**上的逐行差异。

旧规则 = 按 `,` 切段后 partition('=')；新规则 = `ztest_report.parse_kv`。
对每条 INFO 行算 `{键:值}`，输出**只看差异**：

  * `ADD 键=值`   —— 原本被吞掉、现在新出现的键
  * `CHG 键: 旧 → 新` —— 同一键的取值变化（期望只发生在"被吞并的残渣"上）
  * 其余行必须零差异。

覆盖 `reports/*/*/serial_*.log` 全部真归档 + io_probe 的真硬件采集行
（`scripts/io_probe_run1_uart.txt`；该工程暂无会话归档，见交付说明）。

⚠️ 2026-09-24 由 `tmp/b1/kv_before_after.py` 迁来：本脚本被耐久文档当**证据脚本**
   点名，却住在会被清理的 `tmp/` 里 —— 一旦清理就**静默失去该证据脚本**。
   迁移时只改了两处：根目录由 `parents[2]`（`tmp/<子目录>/`）改为 `parents[1]`（`scripts/`）；
   输入数据 `tmp/io_probe_run1_uart.txt` 一并迁到 `scripts/io_probe_run1_uart.txt`。
   输出**不含路径**（只打印 `src.name`）⇒ 迁移前后逐字节一致。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]        # scripts/ 的上一级 = 810_driver_test
sys.path.insert(0, str(ROOT / "common"))
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

from ztest_report import _INFO_RE, _NATIVE_INFO_RE, parse_kv, strip_line  # noqa: E402


def old_kv(text):
    out = {}
    for part in text.split(","):
        if "=" in part:
            k, _, v = part.partition("=")
            out[k.strip()] = v.strip()
    return out


def iter_lines():
    for f in sorted(ROOT.glob("reports/*/*/serial_*.log")):
        for ln in f.read_text(encoding="utf-8", errors="replace").split("\n"):
            yield f, ln
    p = ROOT / "scripts" / "io_probe_run1_uart.txt"
    if p.exists():
        for ln in p.read_text(encoding="utf-8", errors="replace").split("\n"):
            yield p, ln


n_lines = n_info = n_add = n_chg = 0
for src, raw in iter_lines():
    ln = strip_line(raw)
    m = _NATIVE_INFO_RE.search(ln) or _INFO_RE.search(ln)
    if not m:
        continue
    n_info += 1
    name, body = m.group(1), m.group(2)
    o, n = old_kv(body), parse_kv(body)
    if o == n:
        continue
    adds = {k: v for k, v in n.items() if k not in o}
    chgs = {k: (o[k], n[k]) for k in n if k in o and o[k] != n[k]}
    lost = [k for k in o if k not in n]
    if adds or chgs or lost:
        n_add += len(adds)
        n_chg += len(chgs)
        print(f"[{src.name}] {name}")
        print(f"    body = {body!r}")
        for k, v in adds.items():
            print(f"    ADD  {k}={v}")
        for k, (a, b) in chgs.items():
            print(f"    CHG  {k}: {a!r} -> {b!r}")
        if lost:
            print(f"    LOST {lost}   <<< 非预期！")
print(f"\n=== INFO 行 {n_info} 条；新增键 {n_add}；取值变化 {n_chg} ===")
