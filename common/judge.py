#!/usr/bin/env python3
"""810_driver_test 判定框架收敛点（F1/F2/F3，2026-09-18）。

**本文件是唯一真源**，被两类调用方共用（共 45 份调用方）：

  * `projects/*/tests/scripts/dump_results.py`（22 份）——内存兜底读侧的
    **有效性哨兵门**（F2/F3）。只 import `sentinel_state` / `MEM_*`；
  * `projects/*/tests/run_test.py`（23 份）——判定侧的**截断接受谓词**（F1）
    与 `mem_state` 归一化（F2）。只 import `truncation_evidence` /
    `exec_label` / `mem_state_of_payload` / `declared_names_from_dumper` / `MEM_*`。

设计约束（与 SPEC_REFERENCE §6.4 / §12.3 一致）：

  1. **纯函数、零副作用**：不 subprocess、不写文件、不读环境变量。因此可以被
     离线单测直接喂合成数据（硬件不可用时唯一可做的验证手段，见 §验证）。
  2. **只收敛真正同构的部分**。23 份 `run_test.py` 分属 5 个判定家族，家族之间
     在**异常路径**（SKIP / NOT_RUN / 双核结构不对称 / mbox 标记式）上语义本就
     不同——这里只收敛"证据完整性"与"哨兵状态"这两个所有家族**逐字同构**的概念。
     各家族自己的"全项 PASS"谓词仍留原地，不在本批次内强行归一（理由见交付报告）。
  3. **fail-closed**：任何不确定都不允许产生 PASS。信息不足时谓词返回 False
     （= 维持原判 FAIL），绝不返回"看起来是好消息"的结果。

术语：
  token  = 串口采到的 ztest 原生逐项输出
  mem    = `dump_results.py --json` 读目标内存拿到的逐项结果
  marker = 套件收尾标记（ztest 的 `PROJECT EXECUTION SUCCESSFUL|FAILED` /
           rtc_ls 的 `RTC_TEST:DONE:(PASS|FAIL)`）
"""

import ast
import re
from pathlib import Path

__all__ = [
    "MEM_OK", "MEM_INVALID", "MEM_NO_SENTINEL", "MEM_UNAVAILABLE", "MEM_UNBUILT",
    "MEM_STATE_LABEL", "MEM_INVALID_NOTE", "MEM_NO_SENTINEL_NOTE",
    "MEM_UNAVAILABLE_NOTE", "MEM_UNBUILT_NOTE",
    "EXEC_SUCCESSFUL", "EXEC_FAILED", "EXEC_UNKNOWN", "EXEC_TRUNCATED",
    "EXEC_CONFLICT", "exec_conflict_note",
    "ZERO_EVIDENCE_NOTE", "has_evidence", "missing_evidence_note",
    "MEM_ONLY_NOTE", "mem_only_evidence",
    # R8（2026-09-21）: 批量路径下 mem 取到的项**一律不算证据**
    "MEM_NOEVID", "MEM_WEAK_NOTE", "MEM_WEAK_ITEM_NOTE",
    "mem_noevid_source", "mem_noevid_value", "mem_weak_note", "evidence_gaps",
    "mem_noevid_cell",
    "sentinel_state", "mem_state_of_payload", "mem_state_line",
    "missing_evidence", "truncation_evidence", "exec_label",
    "NO_EVIDENCE_STATES",
    "declared_names_from_dumper",
    # 报告渲染（.md）收敛区
    "REPORT_TABLE_HEADER", "REPORT_TABLE_SEP", "RESULT_TEXT",
    "result_text", "escape_md_table_cell", "md_table_row",
    "parse_md_table_row", "coverage_expected", "clip_cell",
    "source_label", "format_report_header",
]

# =============================================================== F2/F3: 内存兜底
#
# mem_state 词表。**报告里必须是真话**：把"内存结果无效（哨兵未置位）"和
# "内存兜底根本没跑"混为一谈，会让读者把 fail-closed 的拒绝误读成"这个 case
# 没测任何项"（F2 的症状）。
MEM_OK = "ok"                                  # 哨兵已置位，内存结果可信
MEM_INVALID = "invalid"                        # 哨兵符号在，但值不对 = 未初始化
MEM_NO_SENTINEL = "no_sentinel_symbol"         # ELF 里没有哨兵符号 = 旧构建
MEM_UNAVAILABLE = "unavailable"                # dump 没跑成（连接/超时/解析失败）
MEM_UNBUILT = "unbuilt"                        # ELF 不存在（未构建）

MEM_STATE_LABEL = {
    MEM_OK: "有效",
    MEM_INVALID: "无效（哨兵未置位）",
    MEM_NO_SENTINEL: "无效（ELF 无哨兵符号 = 旧构建）",
    MEM_UNAVAILABLE: "不可用（内存兜底未执行）",
    MEM_UNBUILT: "不可用（未构建）",
}

# 三处（控制台 / .md / .json）共用同一份原因文案，不各写各的（F2 要求）。
MEM_INVALID_NOTE = (
    "test_results_valid 符号**存在但未置位**：固件没跑到 tr_init"
    "（挂在内核初始化 / POST_KERNEL / 早期崩溃）→ test_results[] 停在 .bss 零值"
    "（零值语义 = TR_PASS）→ **内存结果不可信，不是全 PASS**。"
)
MEM_NO_SENTINEL_NOTE = (
    "ELF 里**没有** test_results_valid 符号：这是哨兵加入（2026-09-18）**之前**"
    "编译的旧镜像 → **必须重新编译**后再跑；本次内存兜底不可用（fail-closed）。"
)
MEM_UNAVAILABLE_NOTE = "内存兜底未执行（gdb/dump 失败、超时或符号解析失败）。"
MEM_UNBUILT_NOTE = "ELF 不存在（未构建）→ 内存兜底不可用。"


def sentinel_state(symbol_present, magic_ok):
    """F3：把哨兵门拆成**三种语义不同**的状态（而不是笼统的"invalid"）。

    Args:
        symbol_present: ELF 里能否解析到 `test_results_valid` 符号
        magic_ok:      该符号处的值是否等于 `TR_VALID_MAGIC`（0x5A）

    返回 `(state, note)`。三种状态的处理动作刻意不同：

      * 无符号   → `MEM_NO_SENTINEL`：**旧构建**，动作是"重新编译"，不是"查驱动"
      * 有符号未置位 → `MEM_INVALID`：真的未初始化，内存兜底不可信
      * 都成立   → `MEM_OK`
    """
    if not symbol_present:
        return MEM_NO_SENTINEL, MEM_NO_SENTINEL_NOTE
    if not magic_ok:
        return MEM_INVALID, MEM_INVALID_NOTE
    return MEM_OK, ""


def mem_state_of_payload(payload):
    """F2：从 `dump_results.py --json` 的 payload 归一出 `(mem_state, note)`。

    - `payload is None`（dump 未跑成 / 非 0 退出 / JSON 解析失败）→ UNAVAILABLE
    - 新 payload 带 `mem_state` → 直接采信（并带上它自己的 note）
    - 旧 payload 只有 `valid` → `True`→ok / 其它→invalid（向后兼容：改动前后
      行为一致——旧 dumper 不会输出 `mem_state`）
    """
    if payload is None:
        return MEM_UNAVAILABLE, MEM_UNAVAILABLE_NOTE
    state = payload.get("mem_state")
    if state in MEM_STATE_LABEL:
        # ★ 无 `note` 时返回**空串**，不是标签本身 —— 返回标签会让报告渲染成
        #   `内存兜底: 有效 — 有效`（标签重复）。状态词与理由串是两回事：
        #   标签已经在 `mem_state_line()` 里了，这里只负责"额外理由"。
        return state, (payload.get("note") or "")
    if payload.get("valid") is True:
        return MEM_OK, ""
    return MEM_INVALID, (payload.get("note") or MEM_INVALID_NOTE)


def mem_state_line(state, note="", prefix="内存兜底"):
    """F2：报告里那**一行**可读文案（.md / 控制台共用）。

    ★ `MEM_OK` **不豁免 note**：状态为 `ok` 只表示"哨兵已置位"，不代表"本轮 mem
      证据被采纳"。E6 门禁（`ok` 但本轮无 `ZT_BOOT` ⇒ mem 项降级剔除）产生的
      note 恰好落在 `ok` 分支——旧实现 `if state == MEM_OK: return label` 会**丢掉
      它**，使 `.md`/控制台只显示"内存兜底: 有效"、读者看不到"结果被拒绝"，直接
      违反 F2 判据（读者只看报告文件就要能区分"无效"与"没跑"）。
      故：`ok` **且 note 为空**（常规路径）时才走裸标签；只要 note 非空就照常拼接。
    """
    label = MEM_STATE_LABEL.get(state, state)
    if state == MEM_OK and not note:
        return f"{prefix}: {label}"
    return f"{prefix}: {label}" + (f" — {note}" if note else "")


# ===================================================================== F1: 判定
EXEC_SUCCESSFUL = "SUCCESSFUL"
EXEC_FAILED = "FAILED"
EXEC_UNKNOWN = "UNKNOWN"
# 截断接受的**显式可解释**标签（F1(a) 要求报告里一眼能看出来）
EXEC_TRUNCATED = "TRUNCATED_AFTER_ALL_ITEMS"
# R6（2026-09-19）：相反收尾标记并存时的**显式**标签（parse_ztest 已 fail-closed 归成
# FAILED；报告用本标签把"矛盾"本身写出来，读者一眼能看出不是普通 FAIL）。
EXEC_CONFLICT = "CONFLICTING_MARKERS"

# 零证据（2026-09-18 追加）：items 为空 = 一个测试项 token 都没捕到。
# `all([]) == True` 是 Python 陷阱 —— `execution == "SUCCESSFUL" and all(...)`
# 会在**零测试项**时判 PASS。这与上一轮"5/5 全是 mem 兜底证据"的假 PASS 是
# **同一类缺陷的两副面孔**：都是"最需要证据的时候报告最像好消息"。
# 判据只有一条：**零证据不得判 PASS**。
ZERO_EVIDENCE_NOTE = (
    "零证据：串口未捕获到任何测试项 token（PASS/FAIL/SKIP 全无）"
    "→ 没有任何通过证据，按『零证据不得判 PASS』处理（既不是全过，也不是跳过）。"
    "常见成因：输出被 log ring / 串口缓冲整段刷掉，或固件没跑到测试套件。"
)


def missing_evidence_note(missing, limit=12):
    """"证据缺失"的**可区分理由串**（★ 2026-09-18 R4）。

    与"驱动/测试失败"分开表述：这是"**没测到**"，不是"测坏了"。必须列出
    **哪几项**没有结果（不能只给总数），超出 `limit` 用"…（另 N 项）"收尾。
    """
    if not missing:
        return ""
    shown = "、".join(missing[:limit])
    if len(missing) > limit:
        shown += f" …（另 {len(missing) - limit} 项）"
    return (f"证据缺失（非驱动失败）：{len(missing)} 项已声明测试项无确定结果"
            f"（token 与内存兜底都没有值）—— {shown}")


def has_evidence(items):
    """零证据守卫：items 非空才算"有测试项证据"。

    直接用作判定式的一环（`execution == "SUCCESSFUL" and has_evidence(items) and ...`），
    以消除 `all([]) == True` 造成的 0/0 假 PASS。
    """
    return bool(items)


# ===================================================== R7（2026-09-20）: 批量必须靠 token
MEM_ONLY_NOTE = (
    "证据弱（仅内存兜底，非串口逐项）：本 case **全部测试项**的证据都来自 GDB "
    "内存兜底，串口没有捕到任何逐项 token ⇒ **批量路径不得判 PASS**"
    "（用户裁定 2026-09-20：调试可用 mem 兜底，批量必须靠 token；"
    "调试路径 `dbg_run.py` 不受此约束）。"
    "★ 这是『串口没拿到逐项证据』，**不是**驱动/测试失败 —— 与 `missing_evidence_note`"
    "「没测到」同侧，与 G2「测坏了」分开表述。"
)


def mem_only_evidence(sources):
    """R7：批量「必须靠 token」守卫 —— **全项证据都来自内存兜底 ⇒ 不得判 PASS**。

    Args:
        sources: 该 case **逐项（双核工程还要逐核）**的证据来源串（各家族自己的
                 标签，例：F4 族的 `"src=mem"` / `"src=token"`，D3 族的 `"mem"` /
                 `"token"` / `"missing"`，eth 的 `source` 字段）。

    返回 True 当且仅当**两条同时成立**：

      1. 有证据条目（空表返回 False —— 零证据走 `has_evidence` 守卫，不并入本守卫）；
      2. **没有任何一条**来自串口 token。

    ★ 判据是"**整个 case 一条串口逐项 token 都没有**"，而不是"报告里 `src` 列全是
      mem"：双核工程里 `src` 是**跨核聚合**的（任一核用了 mem ⇒ 该项标 mem），
      按聚合列判会把"某一核整体丢行、另一核 token 完好，丢行由 §12.3 兜底补回"
      误伤 —— 那个形态 R3 明确保留为合法（`run_test.py`：「兜底给出确定结果 ⇒
      不算零证据，仍可 PASS」），R7 不得翻转。故 D3 族传的是**逐核**来源（`ev_srcs`）。

    为什么只堵"全 case 零 token"而不是"任一 mem"：内存兜底（§6.4.3）本身是**正式
    机制**，部分项丢 token 时用它补是设计内的（实测 hw2 批次的 otbn / i3c_dualcore
    都有少量 mem 项且判定正确）。只有当**串口整份逐项证据都没拿到**时，这个 PASS
    才只剩"上一轮 RAM 里的数字"作依据 —— 那正是 O-13（`i3c_i2c_switch_test`
    11/11 全 `src=mem`，两个核都没有任何 token）的形态。
    """
    srcs = [str(s).strip().lower() for s in sources]
    if not srcs:
        return False
    return not any("token" in s for s in srcs)


# ============================== R8（2026-09-21，触发来源 = 用户裁定 2026-09-20）
#
# 用户原话：「**内存兜底只在调试时候使用，测试结果必须以 token 为判断标准**」；
# 追问"批量测试里，内存兜底（mem）取到的项该怎么算？"→ 裁定 **"B 严：mem 一律不算证据"**。
#
# 与 R7 的关系（**R8 在 R7 之上再加一层，R7 不撤**）：
#   * R7（`mem_only_evidence`）只堵「**整个 case** 一条串口逐项 token 都没有」；
#   * R8 把口径下沉到**逐项**：任何一项只要它的 PASS/FAIL/NOT_RUN 来自 mem，
#     **该项即非证据** —— 不再需要"全 case 零 token"这个前提。
#   * 既有哨兵门 / 独立性门**全部保留**（R2：`mem_state != ok` ⇒ dumper 侧已清空；
#     R5-E6：`ok` 但无 `ZT_BOOT` ⇒ mem 项降级剔除）。R8 是它们**之后的**一道闸。
#
# R8 对逐项的处置（三处必须一致：判定 / 计数 / 报告）：
#   1. 逐项结果落 `MEM_NOEVID`（见下），**不进 `passed`**、**不进 `ran`**；
#   2. 落进 `missing_evidence`（`NO_EVIDENCE_STATES` 新增本状态）⇒ 走既有的
#      "零证据 / 证据缺失"通道：`truncation_evidence()` 不放行、case 必须 FAIL；
#   3. **报告里仍要能看到 mem 的取值**（人得能查）：取值写进「来源」列
#      （`mem(非证据:PASS)`），结果列写 `⬜ 无证据(mem)`，理由串走 `MEM_WEAK_NOTE`
#      「证据弱（仅内存兜底，非串口逐项）」—— 与「驱动/测试失败」**严格分开**。
#
# **调试路径（dbg_run.py）不受影响**：它既不 import 本模块、也不调用这两个守卫
# （判据 = `dbg_run.py` 里引用 `mem_only_evidence` / `MEM_ONLY_NOTE` / `mem_noevid*`
# 的次数为 0），mem 兜底在调试时照常可用。
MEM_NOEVID = "MEM_NOEVID"          # 逐项结果词：该项唯一证据来自内存兜底 ⇒ 非证据

#: 逐项理由（报告「失败原因」列；★ 与"驱动/测试失败"分开表述）
MEM_WEAK_ITEM_NOTE = (
    "证据弱（仅内存兜底，非串口逐项）：本项没有串口逐项 token，只有 GDB 内存兜底"
    "读到的值 ⇒ 批量路径**不计为证据**"
)

#: 整 case 理由（`str.format(n=..., detail=...)`；控制台 / .md / .json 三处同源）
MEM_WEAK_NOTE = (
    "证据弱（仅内存兜底，非串口逐项）：{n} 项无串口逐项 token，只有 GDB 内存兜底"
    "读到的值（{detail}）⇒ 批量路径**一律不计为证据**，按『无证据』处理"
    "（用户裁定 2026-09-20：内存兜底只在调试时使用，测试结果必须以 token 为判断标准；"
    "调试路径 `dbg_run.py` 不受此约束）。"
    "★ 这是『串口没拿到逐项证据』，**不是**驱动/测试失败 —— 与 `missing_evidence_note`"
    "「没测到」同侧，与 G2「测坏了」分开表述。"
)

#: 取值 → 「来源」列的写法（`mem(非证据:PASS)`）。取值**保留原样**，人才查得到。
MEM_NOEVID_SOURCE_RE = re.compile(r"mem\(非证据:([^)]*)\)")


def mem_noevid_source(mem_value):
    """R8：mem 非证据项在「来源」列/notes 里的写法 —— 取值与"非证据"必须同时可见。"""
    return f"mem(非证据:{mem_value})"


def mem_noevid_value(source_or_note):
    """R8：从 `mem(非证据:X)` 串里取回 mem 的原始取值（取不回返回 `""`）。"""
    m = MEM_NOEVID_SOURCE_RE.search("" if source_or_note is None else str(source_or_note))
    return m.group(1) if m else ""


def mem_weak_note(entries, limit=12):
    """R8：mem 非证据项的**可区分理由串**（"证据弱" ≠ "驱动/测试失败"）。

    Args:
        entries: 可迭代的条目描述串（各家族自定，建议含取值，如
                 `"autocorr_8k(mem=PASS)"` / `"TGT:i2c_rw_1(mem=NOT_RUN)"`）。
        空表 → `""`（不产生噪音）。
    """
    entries = [str(e) for e in entries]
    if not entries:
        return ""
    shown = "、".join(entries[:limit])
    if len(entries) > limit:
        shown += f" …（另 {len(entries) - limit} 项）"
    return MEM_WEAK_NOTE.format(n=len(entries), detail=shown)


def evidence_gaps(declared, status_by_name):
    """R8：把"无证据"的已声明项按**成因**拆成两类（报告用）。

    返回 `(missing, mem_noevid)`：

      * `missing`    —— `MISSING` / `NOT_RUN`：token 与 mem **都没给值** ⇒ 真"没测到"；
      * `mem_noevid` —— `MEM_NOEVID`：**mem 有值、但按 R8 不算证据**（取值见来源列）。

    两类的**判定效果完全相同**（都已被 `missing_evidence()` 计入 ⇒ 不得判 PASS），
    差别只在报告里的理由串 —— `missing_evidence_note()` 说"token 与内存兜底都没有值"，
    对 `MEM_NOEVID` 项是**假话**（mem 明明有值），故必须拆开各说各的。
    """
    if not declared:
        return [], []
    missing, memn = [], []
    for n in declared:
        st = status_by_name.get(n, "MISSING")
        if st == MEM_NOEVID:
            memn.append(n)
        elif st in NO_EVIDENCE_STATES:
            missing.append(n)
    return missing, memn


# 截断接受判据里的"无确定结果"状态集（★ 2026-09-18 A 组审计追补）。
# ★ 作用域**仅限截断接受**：常规路径里 `NOT_RUN` 是合法状态（用例确实没跑到，
#   各工程按既有语义把它排除出 `ran` 分母）—— 那条语义**不改**。
#   本集合只保证：`NOT_RUN` **不能成为"截断接受"放行的依据**。
# ★ `SKIP` **不并入**：它是 `ztest_test_skip()` 的**主动跳过 = 确定结果**。
# ★ `MEM_NOEVID`（2026-09-21 R8）**并入**：mem 取到的项不算证据 ⇒ 同样不得
#   成为截断接受 / 常规判 PASS 的依据；它同时被 `missing_evidence()` 收走，
#   因此"某个 case 靠 mem 补出来的空档"再也不会被静默写成 PASS。
NO_EVIDENCE_STATES = ("MISSING", "NOT_RUN", MEM_NOEVID)


def missing_evidence(declared, status_by_name):
    """已声明但**没有确定结果**的测试项（截断接受判据专用）。

    无确定结果 = `MISSING`（token 与 mem 都没给值）**或** `NOT_RUN`（mem 明确读到
    "该项没跑"）。把 `NOT_RUN` 计入是关键：否则下面这条链条成立 ——

        套件跑到第 6 项挂死 ⇒ 前 5 项 PASS、其余经 mem 兜底读成 NOT_RUN、
        无收尾标记 ⇒ `passed == ran == 5` ⇒ **被截断接受判成 PASS**。

    即"任何工程中途挂死都被静默写成 PASS"。`SKIP` 是主动跳过，属确定结果，不并入。

    ★ R8（2026-09-21）追加：`MEM_NOEVID`（mem 取到的项 ⇒ 不算证据）**也**计入。
    这与"mid-hang"同一条道理：`passed == ran` 之所以还能成立，靠的是分母里
    计入了那些**没有串口证据**的项 —— R8 把它们从分子分母两端同时摘掉，
    于是"靠 mem 补出来的全 PASS"不再能满足 `passed == ran`，只能落进本函数。

    ★ 边界：本函数**只**服务 `truncation_evidence()`（截断接受）。常规路径里
    `NOT_RUN` 的既有语义（不计入 `ran` 分母）不得因此改变。
    """
    if not declared:
        return []
    return [n for n in declared
            if status_by_name.get(n, "MISSING") in NO_EVIDENCE_STATES]


def truncation_evidence(marker, declared, status_by_name,
                        good_marker=EXEC_SUCCESSFUL, bad_marker=EXEC_FAILED):
    """F1(a) 的**前置条件**：只缺收尾标记，且所有已声明项都有确定结果。

    注意本函数**不**判 PASS——它只回答"证据够不够判截断接受"。调用方随后
    叠加自己的"全项 PASS"谓词（各家族不同，故不在此收敛）。

    返回 `(evidence_complete, missing)`：

      * `marker` 已经是 good/bad → `(False, [])`，调用方走**原有分支**（等价性保证：
        常规路径 100% 走这里，新代码路径不参与）；
      * 有任一 `MISSING` **或 `NOT_RUN`** → `(False, missing)`，**红线**，调用方必须
        维持 FAIL（`NOT_RUN` 计入见 `missing_evidence`：防止"中途挂死"被静默放宽）；
      * 没有 declared 表 → `(False, [])`（无法判"证据完整"，fail-closed）；
      * 否则 `(True, [])`。
    """
    if marker == good_marker or marker == bad_marker:
        return False, []
    if not declared:
        return False, []
    missing = missing_evidence(declared, status_by_name)
    return (not missing), missing


def exec_conflict_note(seen):
    """矛盾标记的理由串（R6）。与"驱动失败"分开表述：这是**采集里出现自相矛盾的
    收尾标记**，不是某个测试项 FAIL。"""
    got = " 与 ".join(f"`PROJECT EXECUTION {x}`" for x in (seen or []))
    return ("收尾标记矛盾（非驱动失败）：同一次采集里同时出现 " + got
            + " ⇒ 按 fail-closed 判 FAIL（不采用 last-wins）")


def exec_label(marker, truncated, good_marker=EXEC_SUCCESSFUL, conflict=False):
    """报告里的 `execution` 取值。

    截断接受时用 `EXEC_TRUNCATED`（显式可解释），否则保持原值；
    `None` 归一成 `"UNKNOWN"`（与既有报告口径一致）。
    """
    if conflict:
        return EXEC_CONFLICT
    if truncated:
        return EXEC_TRUNCATED
    return marker or EXEC_UNKNOWN


# ------------------------------------------------- F1 用：已声明测试项的唯一真源
def declared_names_from_dumper(dumper_py, case):
    """从同工程 `scripts/dump_results.py` **静态**解析该 case 的已声明测试项。

    动机：`dump_results.py` 的 `TEST_NAMES` / `TEST_NAMES_BY_CASE` 已经是
    nm 链接序核验过的**唯一权威表**（mem 兜底的 idx 对齐就靠它）。run_test.py
    若再复制一份就是新的漂移源——所以这里用 `ast` **静态**读它（不 exec、
    不 import，因此不会触发 dumper 的任何模块级副作用）。

    取不到（文件缺失 / 变量改名 / 该 case 未登记 / 非字面量）返回 `[]`：
    调用方据此关闭截断接受，**行为与改动前一致**（fail-closed）。
    """
    try:
        src = Path(dumper_py).read_text(encoding="utf-8")
        tree = ast.parse(src)
    except (OSError, SyntaxError, ValueError):
        return []
    by_case = None
    plain = None
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if not isinstance(target, ast.Name):
                continue
            if target.id == "TEST_NAMES_BY_CASE":
                try:
                    by_case = ast.literal_eval(node.value)
                except (ValueError, SyntaxError):
                    by_case = None
            elif target.id == "TEST_NAMES":
                try:
                    plain = ast.literal_eval(node.value)
                except (ValueError, SyntaxError):
                    plain = None
    if isinstance(by_case, dict) and case in by_case:
        names = by_case[case]
        return [str(n) for n in names] if isinstance(names, list) else []
    if isinstance(plain, list):
        return [str(n) for n in plain]
    return []


# ================================================= 报告渲染（.md）收敛区
#
# 24 份 `run_test.py` 各自渲染 .md，此前列序 / 命名 / 转义各写各的 → 同一个
# 项目里的报告没法横向读（列序不一、`src=` 前缀不一、`\|` 未还原）。这里把
# **纯渲染**的那些概念收敛成一处：
#
#   1. 表头与列序（`REPORT_TABLE_HEADER`）—— 全家族逐字一致；
#   2. 单元格转义（`md_table_row`）—— 裸 `|` 不转义会让整行串列；
#   3. COVERAGE 解析（`coverage_expected`）—— 还原 markdown 转义、裸竖线不截断；
#   4. 头部 bullet 块（`format_report_header`）—— 同一组字段、同一顺序；
#   5. 长文本截断（`clip_cell`）—— 表格里塞整段散文没法读。
#
# ★ 本区**只做渲染**，不含任何判定逻辑：PASS/FAIL 的算法一行都不在这里。

REPORT_TABLE_HEADER = "| 测试项 | 结果 | 来源 | 期望 | 实测(INFO) | 失败原因 |"
REPORT_TABLE_SEP = "|---|---|---|---|---|---|"

# 结果词 → 表格文案。各家族结果词表不同（PASS/FAIL/NOT_RUN/SKIP/MISSING/
# INCOMPLETE/OBS/GUARD…），这里只统一**已登记**的那几个；未登记的保持原样
# 可见（renders as `⬜ <词>`），不吞信息。
RESULT_TEXT = {
    "PASS": "✅ PASS",
    "FAIL": "❌ FAIL",
    "SKIP": "⏭️ SKIP",
    "NOT_RUN": "⬜ NOT_RUN",
    "MISSING": "⬜ MISSING",
    "INCOMPLETE": "⚠️ INCOMPLETE",
    # R8：mem 取到的项 —— 结果列写"无证据"，取值另在「来源」列（`mem(非证据:PASS)`），
    # 两列合读一眼就知道"探到了值，但不算数"。
    "MEM_NOEVID": "⬜ 无证据(mem)",
}


def result_text(res):
    """结果词 → 报告单元格文案（未登记的词保留原词，不静默吞掉）。"""
    return RESULT_TEXT.get(res, f"⬜ {res}")


def mem_noevid_cell(status, mem_value=None):
    """R8：D3 族「核」列的单元格 —— mem 非证据时把**取值**与**非证据**一起写出来。

    双核家族的核列直接渲染逐核状态，没有单独的「来源」列可放取值；本函数保证
    `MEM_NOEVID` 的那一格仍然人可查（`⬜ 非证据(mem=PASS)`），不因"不算证据"
    就把 mem 读到的值一并抹掉。非 `MEM_NOEVID` 状态原样返回。
    """
    if status != MEM_NOEVID:
        return status
    return f"⬜ 非证据(mem={mem_value})" if mem_value else "⬜ 无证据(mem)"


def escape_md_table_cell(value):
    """markdown 表格单元格转义：竖线转义、换行压平。

    不转义的裸 `|` 会让整行多出一列，把后面的列全部顶错位。
    """
    text = "" if value is None else str(value)
    return text.replace("|", "\\|").replace("\r", " ").replace("\n", " ").strip()


def md_table_row(cells):
    """把一组单元格渲染成一行 markdown 表格（自动转义）。"""
    return "| " + " | ".join(escape_md_table_cell(c) for c in cells) + " |"


def parse_md_table_row(line):
    """按**未转义**的竖线切分一行 markdown 表格；还原 `\\|`→`|`、`\\\\`→`\\`。

    与 `escape_md_table_cell()` 互逆（COVERAGE 里的 `\\|R(d)\\|` 读进来是
    `|R(d)|`，渲染回报告时再转义回去）。不是表格行返回 None。
    """
    s = line.strip()
    if not s.startswith("|"):
        return None
    s = s[1:]
    if s.endswith("|") and not s.endswith("\\|"):
        s = s[:-1]
    cells, cur, i = [], [], 0
    while i < len(s):
        ch = s[i]
        if ch == "\\" and i + 1 < len(s) and s[i + 1] in "|\\":
            cur.append(s[i + 1])
            i += 2
            continue
        if ch == "|":
            cells.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(ch)
        i += 1
    cells.append("".join(cur).strip())
    return cells


def _is_table_sep(cells):
    return bool(cells) and all(re.fullmatch(r":?-{2,}:?", c) for c in cells)


def coverage_expected(cov_path, name_idx=0, expected_idx=3, tail=2):
    """从 COVERAGE.md 提取 `{测试项: 预期数据}`（报告「期望」列的数据源）。

    - 只认**列头含 `预期数据`** 的表（COVERAGE.md 里还有别的表，整表忽略）；
    - 预期值 = 该行 `expected_idx` 起、去掉末 `tail` 列（预期来源 / 状态）后
      合并 —— 这样单元格里的**裸竖线**（如 trng `runs_8k` 的 `z = |V−2nπ…|`）
      与**转义竖线**（如 `autocorr_8k` 的 `\\|R(d)\\|`）都不会截断 / 串列；
    - 文件缺失或无该表 → `{}`（调用方填 `-`）。

    这一条就是 §1.4「`\\|` 转义残留」的修法：旧实现用 `[^|]*` 解析，遇到
    `\\|` 时把 `|` 当列界，单元格被截成半个。
    """
    try:
        text = Path(cov_path).read_text(encoding="utf-8")
    except OSError:
        return {}
    out, in_table = {}, False
    for line in text.splitlines():
        if not line.lstrip().startswith("|"):
            in_table = False
            continue
        cells = parse_md_table_row(line)
        if cells is None or _is_table_sep(cells):
            continue
        if "预期数据" in cells:
            in_table = True
            continue
        if not in_table or len(cells) < expected_idx + tail:
            continue
        name = cells[name_idx].strip().strip("`").strip()
        if not re.match(r"^[A-Za-z_]\w*$", name):
            continue
        out[name] = "|".join(cells[expected_idx:len(cells) - tail]).strip()
    return out


def clip_cell(value, limit=60):
    """长文本截断（表格里塞整段散文没法读）：截到 `limit` 字符 + `…`。

    只作用于**渲染**，不改任何判定；完整文本在 `tests/COVERAGE.md`。
    """
    text = "" if value is None else str(value)
    return text if len(text) <= limit else text[:limit].rstrip() + "…"


def source_label(raw):
    """「来源」列统一词表：`token` / `mem` / `mem(NOT_RUN)` / `missing`。

    各家族既有写法都归一到这里：去掉 `src=` 前缀、`token(skip)`→`token`
    （"跳过"已由结果列表达）、`mem-unavailable`→`missing`（没有拿到任何证据）。
    `mem(NOT_RUN)` **保留**——它表达"内存明确读到该项没跑"，与 `missing` 不同。
    未知写法原样返回（不吞信息）。
    """
    n = ("" if raw is None else str(raw)).strip()
    if n.startswith("src="):
        n = n[4:].strip()
    if n == "token(skip)":
        return "token"
    if n == "mem-unavailable":
        return "missing"
    return n or "-"


def format_report_header(project, case, board, date=None, env=None,
                         execution=None, mem_line="", result=None):
    """报告头部 bullet 块：**全家族同一组字段、同一顺序**（§1.6）。

    字段顺序固定为 工程 / case / 日期 / 板级 / 环境 / 执行标记 / 内存兜底 / 结果；
    调用方缺哪一项就不传（不输出空字段）。`mem_line` 直接收
    `mem_state_line()` 的输出（形如 `内存兜底: 有效`）。
    """
    lines = [f"- 工程: {project}", f"- case: {case}"]
    if date is not None:
        lines.append(f"- 日期: {date}")
    lines.append(f"- 板级: {board}")
    if env is not None:
        lines.append(f"- 环境: {env}")
    if execution is not None:
        lines.append(f"- 执行标记: {execution}")
    if mem_line:
        lines.append(f"- {mem_line}")
    if result is not None:
        lines.append(f"- 结果: {result}")
    return lines
