#!/usr/bin/env python3
"""Ethernet test single-core regression runner.

用法:
    python run_test.py                          # 默认 case_mdio
    python run_test.py --case case_link
    python run_test.py --case case_mdio --skip-build

判定（SPEC_REFERENCE §6.2.1 单核）：该核全项 `PASS - <name>` 且
`PROJECT EXECUTION SUCCESSFUL`。串口缺项按 §6.4.3 用 dump_results.py
读内存兜底合并（来源标 mem）。
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import threading
import time
from datetime import datetime
from pathlib import Path

import serial
import serial.tools.list_ports as list_ports

SAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
sys.path.insert(0, str(Path(__file__).resolve().parent / "scripts"))
from ztest_report import parse_ztest
from report_dir import allocate_session_dir, case_dir_for, update_summary
from serial_archive import archive_serial  # noqa: E402
import process  # noqa: E402  —— L2 处理层（归档 → 判定 → 报告）
import acquire  # noqa: E402  —— L1 采集层（构建/烧录/采集/落归档）

# ---- F1/F2/F3 判定框架收敛点（common/judge.py）----
from judge import (MEM_ONLY_NOTE, MEM_OK, MEM_UNAVAILABLE, MEM_UNAVAILABLE_NOTE,  # noqa: E402
                   mem_only_evidence,
                   exec_label, has_evidence, mem_state_line, mem_state_of_payload,
                   missing_evidence, missing_evidence_note, truncation_evidence)
# ---- R8（2026-09-21）: 批量路径下 mem 取到的项一律不算证据 ----
from judge import (MEM_NOEVID, MEM_WEAK_ITEM_NOTE, evidence_gaps,  # noqa: E402
                   mem_noevid_source, mem_noevid_value, mem_weak_note)
from judge import (REPORT_TABLE_HEADER, REPORT_TABLE_SEP,  # noqa: E402
                   clip_cell, coverage_expected, format_report_header,
                   md_table_row, result_text, source_label)

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
# 本工程为单核 1os 构建（cpu1 入口 = 0x10000000）：构建/烧录一律走
# test.sh → test_common.sh 的 build_single_test/load_and_run_single
# （SINGLE_BOARD / SINGLE_ROM_START），本脚本不消费 TARGET_ROM_START/BOARD。
DRIVER_NAME = "eth_test"
REPORT_BASE = f"{DRIVER_NAME}_report"

# TEST_NAMES 按 ztest_unit_test 段的**链接序**（SORT_BY_NAME）排列，与
# common/ztest_glue.c 记账 idx 对齐；两个 case 变体源集不同，各一份。
# 与 tests/scripts/dump_results.py 的同名表示必须同步（SPEC_REFERENCE §6.4.3）。
# 复核命令： nm --numeric-sort <elf> | grep _ztest_unit_test__<suite>__
TEST_NAMES_BY_CASE = {
    "case_mdio": [                       # suite = eth_mdio（A 组，5 项）
        "eth_dev_ready",
        "eth_phy_link_state",
        "mdio_invalid_args",
        "mdio_phy_id",
        "mdio_reg_rw",
    ],
    "case_link": [                       # suite = eth_link
        "eth_link_up",
    ],
    "case_phy_reset": [                  # suite = eth_mdio（同 case_mdio 源集，仅 overlay 不同）
        "eth_dev_ready",
        "eth_phy_link_state",
        "mdio_invalid_args",
        "mdio_phy_id",
        "mdio_reg_rw",
    ],
    "case_arp_icmp": [                   # suite = eth_arp_icmp
        "eth_arp_icmp",
    ],
    "case_udp_echo": [                   # suite = eth_udp_echo
        "eth_udp_echo",
    ],
}


def log(msg):
    print(msg, flush=True)


def merge_mem(items, payload):
    """§6.4.3：把内存兜底 payload 合并进逐项结果（**纯函数**，不碰硬件）。

    只填补串口缺失的测试项（来源=mem）；payload 为 None（兜底没跑成）时标
    mem-unavailable，不阻断回归。
    """
    if payload is None:
        return items, MEM_UNAVAILABLE, MEM_UNAVAILABLE_NOTE

    # F2/F3：mem_state 是**三态真话**（ok / invalid / no_sentinel_symbol），不再
    # 笼统报 "ok" —— 否则读者分不清"内存结果无效"和"内存兜底根本没跑"。
    # 非 ok 时 payload 的 TGT 必为空表（dumper 侧 fail-closed），故不合并任何项。
    state, note = mem_state_of_payload(payload)
    known = {it["name"] for it in items}
    reasons = payload.get("reasons", {})
    added = 0
    if state == MEM_OK:
        for name, result in payload.get("TGT", {}).items():
            if name in known or result == "NOT_RUN":
                continue
            # R8（2026-09-21，用户裁定 2026-09-20）：**mem 取到的项一律不算证据**。
            # 取值写进「来源」列（`mem(非证据:PASS)`，人可查），逐项结果落
            # MEM_NOEVID ⇒ 不进 passed/ran、计入 missing_evidence。
            # 上方 E6 独立性门（无 ZT_BOOT ⇒ 直接剔除）与 R2 哨兵门原样保留，
            # R8 是在它们**之上**再加的一层。
            items.append({"name": name, "result": MEM_NOEVID,
                          "reason": reasons.get(name, ""),
                          "source": mem_noevid_source(result)})
            added += 1
    log(f"[mem] mem_state={state}"
        + (f" — {note}" if state != MEM_OK else "")
        + f"; merged {added} item(s) from test_results[]")
    return items, state, note


def evaluate_case(case_name, tgt_lines, mem=None):
    view = parse_ztest(
        [l for _, l in tgt_lines],
        ctrl_banners=[], tgt_banners=["TGT"]
    )
    tests = view.get("tests", {})
    items = [{"name": n, "result": v.get("result"), "reason": v.get("reason", ""),
              "source": "token"}
             for n, v in tests.items()]
    execution = view.get("execution")
    boot_seen = bool(view.get("boot_seen"))

    # mem 兜底 payload 由 **L1 侧**（main() 的 dump_mem）取得并传入 —— 它读目标内存，
    # 属采集层；离线重放传 None ⇒ 走 mem-unavailable 分支（与既有重放语义一致）。
    items, mem_state, mem_note = merge_mem(items, mem)

    declared = TEST_NAMES_BY_CASE.get(case_name, [])
    order = {n: i for i, n in enumerate(declared)}
    items.sort(key=lambda it: order.get(it["name"], 1 << 30))

    # ---- 兜底证据可信度门（★ 2026-09-18 R5-1）----
    # mem 证据**本身是有效的**（§12.3 正式兜底机制）—— 本门**不是**"只认 token"，
    # 而是要求证据有**独立性**：内存里有 PASS ≠ **这一轮**确实跑过本固件。
    #   * `mem_state == ok`（哨兵置位）只证明"曾有一次运行跑到过 tr_init"；
    #   * RAM 在两次运行之间**不清零** ⇒ 没有本轮启动佐证时，读到的可能是
    #     **上一次运行**残留的结果数组 + 上一次置位的 0x5A（与"镜像陈旧/两遍烧录"
    #     同源的坑）。
    #   * `ZT_BOOT`（本固件 test_main 第一行）证明**本次采集窗口内确实在跑本固件**。
    # 二者同时成立，mem 项才计入证据；否则 mem 项**降级剔除**（落回 MISSING），
    # 交给下面的完整度检查判 FAIL。佐证齐全时兜底照常生效（不误伤正常路径）。
    mem_trusted = (mem_state == MEM_OK) and boot_seen
    if not mem_trusted:
        # R8：来源串已改为 `mem(非证据:X)` ⇒ 用前缀判归属（E6 门语义不变）
        _dropped = [it["name"] for it in items
                    if str(it.get("source", "")).startswith("mem")]
        if _dropped:
            items = [it for it in items
                     if not str(it.get("source", "")).startswith("mem")]
            mem_note = ("；".join(x for x in [
                mem_note,
                f"内存兜底结果被拒绝（无 ZT_BOOT 佐证，无法排除 RAM 里是上一轮残留"
                f"）：{len(_dropped)} 项降级为无证据"] if x))

    # ---- 完整度守卫（★ R5-1，与 F4 族的 R4 规则同源）----
    # 收尾标记齐全但**有已声明项没有确定结果**（token 无 / mem 被拒或跳过）⇒ 不得判 PASS。
    status = {it["name"]: it["result"] for it in items}
    # R8：无证据项按成因拆开 —— "没测到" 与 "mem 有值但不算证据" 理由串不同
    missing, _mem_noevid_names = evidence_gaps(declared, status)
    trunc_ok, _m2 = truncation_evidence(execution, declared, status)
    truncated = False
    all_pass = (execution == "SUCCESSFUL" and has_evidence(items) and not missing
                and all(i["result"] == "PASS" for i in items))
    if (not all_pass and trunc_ok and has_evidence(items) and not missing
            and all(i["result"] == "PASS" for i in items)):
        all_pass = True
        truncated = True
    # R7（2026-09-20）批量必须靠 token：**全项**证据都来自 mem 兜底 ⇒ 不得判 PASS。
    # 判据与理由串的唯一来源 = common/judge.py（mem_only_evidence / MEM_ONLY_NOTE）；
    # 理由串挂在 mem_note 上 ⇒ 控制台 / .md / .json 三处同源可见。
    if mem_only_evidence([it.get("source") for it in items]):
        all_pass = False
        mem_note = ((mem_note + "；") if mem_note else "") + MEM_ONLY_NOTE
    # R8（2026-09-21，用户裁定 2026-09-20）：mem 取到的项**一律不算证据**。
    # 逐项结果已由 merge_mem 落成 MEM_NOEVID ⇒ 判定已堵；这里只把**可区分理由串**
    # 挂到 mem_note（控制台 / .md / .json 三处同源可见）。
    _mem_noevid = [it for it in items if it.get("result") == MEM_NOEVID]
    if _mem_noevid:
        _weak = mem_weak_note(
            [f"{it['name']}(mem={mem_noevid_value(it.get('source'))})"
             for it in _mem_noevid])
        mem_note = ((mem_note + "；") if mem_note else "") + _weak
    return {"case": case_name, "result": "PASS" if all_pass else "FAIL",
            "execution": exec_label(execution, truncated),
            "mem_state": mem_state, "mem_note": mem_note, "items": items,
            "missing_evidence": missing,
            "missing_note": missing_evidence_note(missing)}


def print_report(results):
    print("\n--- Ethernet test results ---")
    for r in results:
        status = r.get("result", "UNKNOWN")
        items = r.get("items", [])
        passed = sum(1 for i in items if i.get("result") == "PASS")
        # R8：分母不计 mem 非证据项
        total = sum(1 for i in items if i.get("result") != MEM_NOEVID)
        print(f"## {r['case']}: {status} ({passed}/{total})"
              f"  exec={r.get('execution')}")
        # 控制台与 .md（见下方 write_markdown_report）/其余工程同源：走
        # mem_state_line() 才能把 mem_note（如 E6 门禁的"内存兜底结果被拒绝"）
        # 显示出来 —— 旧写法只打裸 mem_state，note 在控制台被吞掉。
        print("  " + mem_state_line(r.get("mem_state", ""),
                                    r.get("mem_note", "")))
        for item in items:
            name = item.get("name", "?")
            res = item.get("result", "?")
            reason = item.get("reason", "")
            print(f"  {name}: {res} [{item.get('source', '?')}] {reason}")


def write_markdown_report(cdir, result):
    """§6.4.2 报告：与 json 同源（items[] 渲染），覆盖写。

    列序 / 表头 / 单元格转义统一走 common/judge.py（REPORT_TABLE_HEADER +
    md_table_row），与全家族逐字一致；内存兜底状态走 mem_state_line()，
    与控制台 / .json 三处同源。
    """
    base = cdir / f"{REPORT_BASE}.md"
    items = result.get("items", [])
    passed = sum(1 for it in items if it.get("result") == "PASS")
    # R8：分母不计 mem 非证据项
    total = sum(1 for it in items if it.get("result") != MEM_NOEVID)
    status = result.get("result") or "FAIL"
    lines = [
        f"# {DRIVER_NAME} 测试报告",
        "",
        *format_report_header(
            DRIVER_NAME, result.get("case", ""), "lsqsh_evb@1os/lsqsh/cpu1",
            date=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            env="见 810_driver_test/README.md 外设接线记录"
                "（涉及外部路径的测试项在 COVERAGE.md 标注）",
            execution=result.get("execution"),
            mem_line=mem_state_line(result.get("mem_state", ""),
                                    result.get("mem_note", "")),
            result=f"{status} ({passed}/{total})"),
        *([""] + ["> " + result.get("missing_note", "")]
          if result.get("missing_evidence") else []),
        "",
        "## 测试项结果",
        "",
        REPORT_TABLE_HEADER,
        REPORT_TABLE_SEP,
    ]
    for it in items:
        lines.append(md_table_row([
            it.get("name", "?"), result_text(it.get("result", "?")),
            source_label(it.get("source")), "-", "-", it.get("reason", "")]))
    lines.append("")
    lines.append(f"## 汇总\n\nOVERALL: {status}\n")
    base.write_text("\n".join(lines), encoding="utf-8")
    return base


# ---- L2 处理层规格（common/process.py）：只声明**本工程特有**的接入点 ----
# 判定族特有的谓词 = evaluate_case（ztest token + mem 兜底独立性门 + 完整度守卫）；
# 渲染 = write_markdown_report / print_report；其余（读归档、落盘、summary、rc）
# 由 process.py 统一。归档行是纯字符串 ⇒ 判定入参按 (0.0, 行) 重组（判定只读行文本）。
# 契约：每工程暴露 `l2_spec(facts=None)`；`facts` = 未归档的采集事实（本族不使用）。
# ★ mem 兜底 payload 走 `L2Input.mem`（L1 侧 `dump_mem()` 的产物），判定侧只做**纯合并**。
def l2_spec(facts=None):
    return process.ProcessSpec(
        project=DRIVER_NAME,
        driver=DRIVER_NAME,
        report_base=REPORT_BASE,
        judge=lambda ci: evaluate_case(ci.case,
                                       [(0.0, ln) for ln in ci.lines["TGT"]],
                                       ci.mem),
        render_md=lambda judged, cdir: write_markdown_report(cdir, judged[0]),
        console=lambda judged: print_report(judged),
        summary=lambda judged: (
            judged[0].get("result") or "FAIL",
            sum(1 for it in judged[0].get("items", []) if it.get("result") == "PASS"),
            # R8：分母不计 mem 非证据项
            sum(1 for it in judged[0].get("items", [])
                if it.get("result") != MEM_NOEVID)),
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", default="case_mdio")
    parser.add_argument("--tgt-uart", default=None)
    parser.add_argument("--session-dir", default=None)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--no-archive", action="store_true")
    parser.add_argument("--case-timeout", type=int, default=60)
    args = parser.parse_args()
    date_str = datetime.now().strftime("%Y-%m-%d")
    session = Path(args.session_dir) if args.session_dir else allocate_session_dir(REPORTS_ROOT, date_str)
    logs = session / "logs"
    logs.mkdir(parents=True, exist_ok=True)

    # L1 采集（common/acquire.py）：串口发现 → 构建 → 烧录采集 → L0 归档 → mem 兜底
    acq = acquire.single_cap(acquire.SingleCapSpec(
        driver=DRIVER_NAME, root=SAMPLE_ROOT, tests_dir=SAMPLE_ROOT / "tests",
        case=args.case, board="lsqsh_evb@1os/lsqsh/cpu1", tgt_uart=args.tgt_uart,
        case_timeout=args.case_timeout, skip_build=args.skip_build,
        archive=not args.no_archive, session=session, script=__file__,
        bash_style="fixed", com99_probe=False,
        mem_hook=lambda c: acquire.dump_mem(
            c, Path(__file__).resolve().parent / "scripts" / "dump_results.py")))
    if not acq.ok:
        return 1

    # L1→L2 边界（L0 归档形状，common/serial_archive.py）：L1 已把采集结果归一成
    # `{核名: [原始行]}` 并落档；这里把**同一个对象**交给 L2（common/process.py）。
    return process.run(l2_spec(), case=args.case, lines=acq.lines, mem=acq.mem,
                       session=session, write=not args.no_archive).rc


if __name__ == "__main__":
    sys.exit(main())
