#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""离线验证：judge.mem_state_line() 在 MEM_OK 且 note 非空时输出 note。

纯函数、零硬件。跑法：
    python 810_driver_test/scripts/verify_mem_state_line.py

产出三处渲染（控制台 / .md / .json）对四种输入的对照，并逐字比对
"ok 无 note" 改动前后是否一致（向后兼容证明）。

⚠️ 2026-09-24 由 `tmp/emmcfix/verify_mem_state_line.py` 迁来：本脚本被耐久文档当
   **必做验证**点名，却住在会被清理的 `tmp/` 里 —— 一旦清理就**静默失去该证据脚本**。
   迁移时只改了一处：根目录由 `parents[2]`（`tmp/<子目录>/`）改为 `parents[1]`（`scripts/`）。
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
ROOT = HERE.parents[1]  # .../810_driver_test（scripts/ 的上一级）
sys.path.insert(0, str(ROOT / "common"))

import judge  # noqa: E402


def old_mem_state_line(state, note="", prefix="内存兜底"):
    """改动前的实现（逐字照抄自 git 历史，仅用于等价性对照）。"""
    label = judge.MEM_STATE_LABEL.get(state, state)
    if state == judge.MEM_OK:
        return f"{prefix}: {label}"
    return f"{prefix}: {label}" + (f" — {note}" if note else "")


E6_NOTE = ("内存兜底结果被拒绝（无 ZT_BOOT 佐证，无法排除 RAM 里是上一轮残留"
           "）：5 项降级为无证据")

CASES = [
    ("A ok + note（E6 门禁拒绝路径，本次修复目标）",
     judge.MEM_OK, E6_NOTE),
    ("B ok + 无 note（常规路径，必须逐字不变）",
     judge.MEM_OK, ""),
    ("C invalid（哨兵存在未置位）",
     judge.MEM_INVALID, judge.MEM_INVALID_NOTE),
    ("D no_sentinel_symbol（旧构建）",
     judge.MEM_NO_SENTINEL, judge.MEM_NO_SENTINEL_NOTE),
]

print("=" * 78)
print("mem_state_line() 四输入 × 三处渲染")
print("=" * 78)
for title, state, note in CASES:
    line = judge.mem_state_line(state, note)
    # 三处：控制台与 .md 同源（都调 mem_state_line）；.json 走 mem_note 字段
    console = "  " + line
    md = "> " + line
    json_mem_note = note  # JSON payload 字段 mem_note
    print(f"\n[{title}]")
    print(f"  state={state!r}  note_empty={not note}")
    print(f"  控制台: {console}")
    print(f"  .md   : {md}")
    print(f"  .json : \"mem_note\": {json_mem_note!r}")
    print(f"  note 可见(grep -c '被拒绝'): {line.count('被拒绝')}")

print("\n" + "=" * 78)
print("向后兼容逐字比对：new == old ?")
print("=" * 78)
all_same = True
for title, state, note in CASES:
    new = judge.mem_state_line(state, note)
    old = old_mem_state_line(state, note)
    same = new == old
    all_same &= same
    tag = "SAME" if same else "DIFF"
    print(f"  [{tag}] {title}")
    if not same:
        print(f"        old={old!r}")
        print(f"        new={new!r}")

print()
print("结论：")
ok_note = judge.mem_state_line(judge.MEM_OK, E6_NOTE)
ok_plain = judge.mem_state_line(judge.MEM_OK, "")
print(f"  1. ok+note 现在输出 note: {'被拒绝' in ok_note}  -> {ok_note!r}")
print(f"  2. ok 无 note == 旧实现: {ok_plain == old_mem_state_line(judge.MEM_OK)}")
rest_same = all(judge.mem_state_line(s, n) == old_mem_state_line(s, n)
                for _, s, n in CASES if not (s == judge.MEM_OK and n))
print(f"  3. 其余状态（B/C/D）全部 new==old: {rest_same}")
print(f"  4. 四种输入整体 new==old 仅 A 期望为 False: all_same={all_same}")
if all_same:
    print("     !! 意外：A 也 SAME —— 修复未生效")
    sys.exit(1)
expected_diff_only_a = all(
    (judge.mem_state_line(s, n) == old_mem_state_line(s, n))
    for t, s, n in CASES if not t.startswith("A "))
if not expected_diff_only_a:
    print("     !! 意外：非 A 的输入也发生了变化")
    sys.exit(1)
print("\nOK：仅 A（ok+note）变化，B/C/D 逐字不变。")
