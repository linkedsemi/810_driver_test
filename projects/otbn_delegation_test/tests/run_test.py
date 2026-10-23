#!/usr/bin/env python3
"""OTBN ECDSA delegation dual-core one-button regression agent (ztest-native).

Runs all cases under 810_driver_test/projects/otbn_delegation_test/tests/case_*/
on the connected controller/target UARTs, parses the ztest-native output
(common/ztest_report.parse_ztest) and produces an archived Markdown + JSON
report under 810_driver_test/reports/<YYYY-MM-DD>-<idx>/ (SPEC_REFERENCE.md §6.4).

双核工程 ztest 原生化版：每核固件跑自己的 ztest 套件，逐项结果 = 同名
测试项双核 PASS；case 判定 = 双核 execution 均 "SUCCESSFUL" + 全部项双核
PASS。完成判定（提前杀 gdb 的静默判据）= 本固件 ZT_BOOT 已见的端口上出现
PROJECT EXECUTION（他方固件无 ZT_BOOT，其标记一律忽略，KNOWN_ISSUES.md #30）。

Archiving behavior (SPEC_REFERENCE.md §6.4):
    - Report written to reports/<date>-<idx>/<driver>_<case>/otbn_delegation_test_report.{md,json}
    - Session summary.md index updated (per-case row replaced, §12.4)
    - ZT_INFO (§13.4) parsed into the "actual" column of the report
    - Missing UART items backfilled from memory via dump_results.py --json
      (§12.3: token wins, memory fills gaps, both missing -> FAIL)

Environment:
    I3C_CTRL_UART          Controller UART port (e.g. COM15)
    I3C_TGT_UART           Target UART port (e.g. COM16)
    GDB_SERVER_HOST        GDB server host (default: localhost)
    GDB_SERVER_PORT        GDB server port (default: 1025)
    I3C_CASE_TIMEOUT       Seconds to wait for one case to finish (default: 60)

Usage:
    python run_test.py
    python run_test.py --case case_default
    python run_test.py --case ... --session-dir reports/2026-08-29-3
"""

import argparse
import json
import os
import re
import shutil
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import subprocess
import tempfile
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple

try:
    import serial
    import serial.tools.list_ports
except ImportError as exc:
    print(f"ERROR: pyserial is required: {exc}", file=sys.stderr)
    print("Install with: pip install pyserial", file=sys.stderr)
    sys.exit(1)


# tests/run_test.py -> parents[1] = otbn_delegation_test 工程根
SAMPLE_ROOT = Path(__file__).resolve().parents[1]
# 归档根：810_driver_test/reports/（会话目录 <YYYY-MM-DD>-<idx> 由 report_dir 分配）
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

# 公共解析/归档层（Task 1/2 产物，路径同 trng 样板；REPORTS_ROOT / "common"
# 是计划笔误——公共模块在 810_driver_test/common/）
sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
from ztest_report import parse_ztest   # noqa: E402
from serial_archive import archive_serial  # noqa: E402
import process  # noqa: E402  —— L2 处理层（归档 → 判定 → 报告）
import acquire  # noqa: E402  —— L1 采集层（构建/烧录/采集/落归档）
from report_dir import (allocate_session_dir, case_dir_for,  # noqa: E402
                        update_summary)

# ---- F1/F2/F3 判定框架收敛点（common/judge.py）----
from judge import (MEM_ONLY_NOTE, exec_conflict_note, exec_label, has_evidence,  # noqa: E402
                   mem_only_evidence, mem_state_line, mem_state_of_payload,
                   truncation_evidence)
# ---- R8（2026-09-21）: 批量路径下 mem 取到的项一律不算证据 ----
from judge import (MEM_NOEVID, MEM_WEAK_ITEM_NOTE, mem_noevid_cell,  # noqa: E402
                   mem_noevid_source, mem_weak_note)
from judge import (REPORT_TABLE_HEADER, REPORT_TABLE_SEP,  # noqa: E402
                   clip_cell, coverage_expected, format_report_header,
                   md_table_row, result_text, source_label)

DRIVER_NAME = "otbn_delegation_test"   # §12.4 summary 行 `<driver>_test` 前缀
REPORT_BASE = "otbn_delegation_test_report"  # 避免 _test_test 重复

CTRL_BANNERS = ("CPU2 OTBN_DELEGATION CTRL",)
TGT_BANNERS = ("CPU1 OTBN_DELEGATION TGT",)

# 测试项顺序必须与 ztest_unit_test 段链接序一致（链接器 SORT_BY_NAME → 测试名
# 字典序，§10.27）。当前 10 项，顺序由 nm <elf> | grep ztest_unit_test 核实
# （两个核一致）：
#   z_ztest_unit_test__otbn_deleg__t01_genkey_p256 ... t10_reject_unsupported_curve
TEST_NAMES = [
    "t01_genkey_p256",
    "t02_genkey_p384",
    "t03_genkey_sm2",
    "t04_sign_p256",
    "t05_sign_p384",
    "t06_sign_sm2",
    "t07_verify_p256",
    "t08_verify_p384",
    "t09_verify_sm2",
    "t10_reject_unsupported_curve",
    "t11_sign_blen_lt_n",
    "t12_sign_blen_gt_n",
    "t13_verify_r_out_of_range",
    "t14_verify_s_zero",
    "t15_concurrent_genkey",
    "t16a_sign_blen_zero",
    "t16b_sign_blen_eq_n",
    "t16c_repeat_channel_init",
    "t17_sign_bad_d_zero",
    "t19_verify_bad_q_off_curve",
    "t21_verify_wrong_keypair",
    "t80_proxy_responds_within_timeout",
    "t88_sign_bad_d_ge_n",
    "t89_verify_bad_q_infinity",
]

# Per-case item sets: a case whose test surface differs from the project default
# (KNOWN_ISSUES #60 -- one project-wide TEST_NAMES is wrong for such a case).
# case_explicit_api = the default matrix (client calls the explicit
# mbedtls_ls_otbn_ecdsa_* names) + t16d (the same-image equivalence item).
TEST_NAMES_BY_CASE = {
    "case_throughput": ["b01_throughput"],
    # Isolated throughput: the SAME bench item, but exactly one core benches and
    # the peer holds (does not touch the OTBN), so the numbers carry no
    # cross-core queueing. case_throughput_iso_proxy = client benches the proxy;
    # case_throughput_iso_local = server benches its local OTBN. Both cores
    # register b01_throughput (the hold core's copy is a trivial PASS + role
    # INFO), so the same list serves both cores.
    "case_throughput_iso_proxy": ["b01_throughput"],
    "case_throughput_iso_local": ["b01_throughput"],
    # Same local bench, but genkey/sign draw from the hardware entropy device --
    # the RNG the delegation SERVER uses when serving -- so a proxy-minus-this
    # diff isolates the bus transport rather than the entropy cost.
    "case_throughput_iso_local_hwrng": ["b01_throughput"],
    # Entropy-API timing: cpu1 times the two fetch paths of trng0 (blocking vs
    # ENTROPY_BUSYWAIT) while cpu2 holds. New item name b02_entropy; the HOLD
    # core registers the same name as a trivial PASS placeholder.
    "case_entropy_iso": ["b02_entropy"],
    "case_seq_soak": ["s01_seq_soak"],
    "case_explicit_api": [
        "t01_genkey_p256",
        "t02_genkey_p384",
        "t03_genkey_sm2",
        "t04_sign_p256",
        "t05_sign_p384",
        "t06_sign_sm2",
        "t07_verify_p256",
        "t08_verify_p384",
        "t09_verify_sm2",
        "t10_reject_unsupported_curve",
        "t11_sign_blen_lt_n",
        "t12_sign_blen_gt_n",
        "t13_verify_r_out_of_range",
        "t14_verify_s_zero",
        "t15_concurrent_genkey",
        "t16a_sign_blen_zero",
        "t16b_sign_blen_eq_n",
        "t16c_repeat_channel_init",
        "t16d_mbedtls_and_explicit_agree",
        "t17_sign_bad_d_zero",
        "t19_verify_bad_q_off_curve",
        "t21_verify_wrong_keypair",
        "t80_proxy_responds_within_timeout",
        "t88_sign_bad_d_ge_n",
        "t89_verify_bad_q_infinity",
    ],
    # case_alt_via_proxy = the default matrix (client calls the PLAIN
    # mbedtls_ecdsa_* names, which ARE the proxy in this config) + t90 (the
    # "plain API is really mailbox-bound" discriminator; client asserts, server
    # SKIPs). t90 sorts last (t90 > t89).
    "case_alt_via_proxy": [
        "t01_genkey_p256",
        "t02_genkey_p384",
        "t03_genkey_sm2",
        "t04_sign_p256",
        "t05_sign_p384",
        "t06_sign_sm2",
        "t07_verify_p256",
        "t08_verify_p384",
        "t09_verify_sm2",
        "t10_reject_unsupported_curve",
        "t11_sign_blen_lt_n",
        "t12_sign_blen_gt_n",
        "t13_verify_r_out_of_range",
        "t14_verify_s_zero",
        "t15_concurrent_genkey",
        "t16a_sign_blen_zero",
        "t16b_sign_blen_eq_n",
        "t16c_repeat_channel_init",
        "t17_sign_bad_d_zero",
        "t19_verify_bad_q_off_curve",
        "t21_verify_wrong_keypair",
        "t80_proxy_responds_within_timeout",
        "t88_sign_bad_d_ge_n",
        "t89_verify_bad_q_infinity",
        "t90_plain_api_mailbox_bound",
    ],
    # case_rsa_delegation = the RSA family of the delegation bus, in its own
    # source file (tests/src/otbn_delegation_rsa.c). r01/r02/r03 run on BOTH
    # cores (client proxy / server local backend); r90 is client-assert /
    # server-SKIP (it masks the channel, so it runs LAST -- 'r90' > 'r03').
    "case_rsa_delegation": [
        "r01_rsa_modexp_roundtrip_2048",
        "r02_rsa_modexp_roundtrip_4096",
        "r03_reject_unsupported_modulus",
        "r90_rsa_mailbox_bound",
    ],
    # case_slot = private-key slot signing (own TU). s01/s02/s03/s05/s06/s07
    # run on BOTH cores (client mbox / server direct handler + local verify).
    # s04/s09/s12 are server-only (registration/argument/race); s08/s10/s11 are
    # client-only (client API args / soak / concurrent callers).
    "case_slot": [
        "s01_sign_verify",
        "s02_unregistered",
        "s03_curve_mismatch",
        "s04_register_validation",
        "s05_curves",
        "s06_multi_slot",
        "s07_hash_lengths",
        "s08_client_args",
        "s09_server_args",
        "s10_soak",
        "s11_concurrent",
        "s12_server_race",
        "s13_proxy_rng",
        "s14_slot_rng",
        "s15_p384_fixed_rng",
        "s16_p384_server_rng",
        "s17_p384_32b_server_rng",
    ],
}


GDB = shutil.which("riscv64-unknown-elf-gdb") or "riscv64-unknown-elf-gdb"
DEBUGSERVER_EXE = r"C:\uaer_app\debug_server\bin\DebugServerConsole.exe"


TESTS_DIR = SAMPLE_ROOT / "tests"


def parse_expected_from_coverage():
    """§6.5.2 预期数据（COVERAGE.md 表「预期数据」列）—— 解析收敛在 common/judge.py。

    只认列头含 `预期数据` 的表；转义竖线与裸竖线都不再串列（旧实现用
    `[^|]*` 解析，COVERAGE 里的转义竖线被截成半个单元格，见 §1.4）。"""
    return coverage_expected(TESTS_DIR / "COVERAGE.md")


def find_cases(filter_name: Optional[str] = None) -> List[Path]:
    cases = sorted(p for p in TESTS_DIR.iterdir() if p.is_dir() and p.name.startswith("case_"))
    if filter_name:
        cases = [p for p in cases if p.name == filter_name]
    return cases


def evaluate_case(case_name: str, ctrl_lines: List[str], tgt_lines: List[str],
                  test_names: List[str], mem_data: Optional[Dict]) -> Dict:
    """双核判定（ztest 原生）：两路 lines 各 parse_ztest 后合并。

    - 测试项判定 = 同名项双核 PASS（CTRL+TGT）；单核缺失走 §12.3 内存兜底
    - case 判定 = 双核 execution 均 "SUCCESSFUL" + 全部项双核 PASS
    - ctrl_done/tgt_done 字段 = 两核 execution 值（ztest 无 DONE token，
      PROJECT EXECUTION 是套件完成标记）

    返回结构保持双核样板形状：{name, result, ctrl_done, tgt_done,
    mem_unavailable, tests:[{name,ctrl,tgt,src,info,reason,result}]}。
    """
    ctrl_p = parse_ztest(ctrl_lines, ctrl_banners=CTRL_BANNERS,
                         tgt_banners=TGT_BANNERS)
    tgt_p = parse_ztest(tgt_lines, ctrl_banners=CTRL_BANNERS,
                        tgt_banners=TGT_BANNERS)
    per_core = {"CTRL": ctrl_p, "TGT": tgt_p}

    tests = []
    case_pass = True
    ev_srcs = []   # 每项×每核的证据来源（R7「批量必须靠 token」判据用）
    # R8：mem 取值留档（**只在该 case 真有 mem 项时才进报告** —— 非 mem case
    # 的产物必须逐字节不变，下游 diff 才不会被无关字段噪音淹没）。
    mem_map: Dict[str, Dict[str, str]] = {}
    for name in test_names:
        statuses: Dict[str, str] = {}
        srcs: Dict[str, str] = {}
        mem_vals: Dict[str, str] = {}   # R8：mem 取值留档（只供渲染，不参与判定）
        for core in ("CTRL", "TGT"):
            t = per_core[core]["tests"].get(name)
            st = t["result"] if t else "MISSING"
            src = "token"
            # §12.3 内存兜底：token 缺失的项用内存值补（来源=mem）
            if st == "MISSING" and mem_data is not None and core in mem_data:
                if name in mem_data[core]:
                    # R8（2026-09-21，用户裁定 2026-09-20）：**mem 一律不算证据**。
                    # 取值留档进 mem_vals（渲染「核」列，人可查），逐核状态落
                    # MEM_NOEVID ⇒ 不再满足"双核 PASS"、不计入 passed/ran、
                    # 且进 NO_EVIDENCE_STATES（截断接受同样不放行）。
                    mem_vals[core] = mem_data[core][name]
                    st, src = MEM_NOEVID, "mem"
            if st == "MISSING":
                src = "missing"
            statuses[core] = st
            srcs[core] = src
            ev_srcs.append(src)
        # G3（SPEC_REFERENCE §6.2.4 判定规则总表）：`SKIP`（ztest_test_skip()）是
        # **主动跳过 = 确定结果** —— 既不是失败、也**不计入分母**。逐项三态：
        #   任一核显式 FAIL ⇒ FAIL；否则任一核 SKIP ⇒ SKIP；否则双核 PASS ⇒ PASS；
        #   否则沿用 mem/FAIL 路径。
        _c, _t = statuses["CTRL"], statuses["TGT"]
        has_explicit_fail = (_c == "FAIL" or _t == "FAIL")
        has_skip = (_c == "SKIP" or _t == "SKIP")
        test_pass = (_c == "PASS" and _t == "PASS")
        if has_explicit_fail:
            item_result = "FAIL"
        elif has_skip:
            item_result = "SKIP"
        elif test_pass:
            item_result = "PASS"
        elif mem_vals:
            item_result = MEM_NOEVID
        else:
            item_result = "FAIL"
        # 只有显式 FAIL 才把 case 判死；SKIP 不判死（G3：含 SKIP 其余 PASS ⇒ PASS）。
        if item_result == "FAIL":
            case_pass = False

        # INFO 数值拼接（实际列）："CTRL:k=v,k=v; TGT:..."，无 INFO 留空
        info_parts = []
        for core in ("CTRL", "TGT"):
            kv = ", ".join(f"{k}={v}"
                           for k, v in sorted(per_core[core]["infos"].get(name, {}).items()))
            if kv:
                info_parts.append(f"{core}:{kv}")
        # reason 合并：双核 token reason + 内存 reason（§13.5 可归因格式）
        reason_parts = []
        for core in ("CTRL", "TGT"):
            t = per_core[core]["tests"].get(name)
            if t and t["reason"]:
                reason_parts.append(f"{core}:{t['reason']}")
        if mem_data is not None:
            mr = mem_data.get("reasons", {}).get(name)
            if mr:
                reason_parts.append(f"mem:{mr}")

        # 来源：任一核用内存兜底 → mem；任一核缺失 → missing；否则 token
        if "missing" in srcs.values():
            src = "missing"
        elif "mem" in srcs.values():
            src = "mem"
        else:
            src = "token"

        tests.append({
            "name": name,
            "ctrl": statuses["CTRL"],
            "tgt": statuses["TGT"],
            "src": src,
            "info": "; ".join(info_parts),
            "reason": "; ".join(reason_parts),
            # 结果四路（G3）：PASS / token 显式 FAIL（= 测坏了）/ SKIP（主动跳过，
            # 确定结果、不计分母）/ 仅 mem（= 没测到）。判定见上面的 item_result。
            "result": item_result,
        })
        if mem_vals:
            mem_map[name] = dict(mem_vals)

    ctrl_done = ctrl_p["execution"]
    tgt_done = tgt_p["execution"]
    # 零证据守卫（2026-09-18；同日按裁定**收紧一档**）：
    #   ① tests 为空 ⇒ 一个测试项都没有 ⇒ 不得判 PASS。
    #   ② **某一核整体没有确定结果**（该核全部项既非 PASS 也非 FAIL —— 串口未捕到
    #      **且** 内存兜底也没给出值）⇒ 不得因"另一核完好"而判 PASS。
    #      理由：那一核根本没跑 / 跑崩了且兜底也读不出，两者都不是可容忍的丢行；
    #      丢行本该由 §12.3 内存兜底补回（兜底给出确定结果 ⇒ 不算零证据，仍可 PASS）。
    # ★ 边界：**只堵整核零证据**；部分项缺失仍走 incomplete 容忍（原设计不变）。
    # ★ 与"测坏了"（显式 FAIL token）**分开表述**：这是"没测到"，理由串不混。
    _gaps, _gap_parts = [], []
    for _core in ("CTRL", "TGT"):
        _sts = [t[_core.lower()] for t in tests]
        # G3：SKIP 是确定结果 ⇒ 不再让"整核全 SKIP"落进零证据缺口（否则 case_dead_channel
        # 的 23 项主动跳过会被误当"没测到"）。
        if _sts and not any(s in ("PASS", "FAIL", "SKIP") for s in _sts):
            _gaps.append(_core)
            # R8：整核都只是 mem 非证据时，"内存兜底无结果"是假话，分开说
            # （与"串口未捕到"的真缺口同一通道判 FAIL，理由串不混）。
            _gap_parts.append(
                f"{_core} 核全部 {len(tests)} 项" + (
                    "证据仅来自内存兜底（R8：mem 一律不算证据）"
                    if all(s == MEM_NOEVID for s in _sts) else
                    "无确定结果（串口未捕到 + 内存兜底无结果）"))
    _gap_note = ""
    if _gaps:
        _gap_note = ("证据缺失（非驱动失败）：" + "、".join(_gap_parts)
                     + " ⇒ 不得因另一核完好判 PASS")
    if (not has_evidence(tests) or _gaps or ctrl_done != "SUCCESSFUL" or tgt_done != "SUCCESSFUL"):
        case_pass = False
    # F1/F2（2026-09-18）：见 common/judge.py。
    # F1：收尾标记未到时，只要**全部已声明测试项在两核都有确定结果**（无 MISSING）
    #     且全 PASS ⇒ 判 PASS 并标注 TRUNCATED_AFTER_ALL_ITEMS；任一已声明项在
    #     任一核无证据 ⇒ 维持 FAIL（红线）。
    # F2：mem_state 三态（ok / invalid / no_sentinel_symbol）进报告，读者不用翻日志。
    _status = {}
    for _t in tests:
        if _t["ctrl"] == "MISSING" or _t["tgt"] == "MISSING":
            _status[_t["name"]] = "MISSING"
        elif _t["ctrl"] == MEM_NOEVID or _t["tgt"] == MEM_NOEVID:
            # R8：任一核的 PASS 来自内存兜底 ⇒ 该项不算证据（截断接受不放行）
            _status[_t["name"]] = MEM_NOEVID
        elif _t["result"] == "SKIP":
            # G3：主动跳过 = 确定结果（截断接受可放行）
            _status[_t["name"]] = "SKIP"
        elif _t["ctrl"] == "PASS" and _t["tgt"] == "PASS":
            _status[_t["name"]] = "PASS"
        else:
            _status[_t["name"]] = _t["result"]
    _trunc_ok, _missing = truncation_evidence(ctrl_done, test_names, _status)
    truncated = False
    if (not case_pass and _trunc_ok and tgt_done != "FAILED" and tests
            and not _gaps and all(_t["result"] == "PASS" for _t in tests)):
        case_pass = True
        truncated = True
    _mem_state, _mem_note = mem_state_of_payload(mem_data)
    # R6（2026-09-19）：相反收尾标记并存 ⇒ parse_ztest 已 fail-closed 归成 FAILED；
    # 这里把"矛盾"本身显式写进报告（读者要能一眼看出是矛盾，而不是笼统 FAIL）。
    _conf_seen = sorted(set(ctrl_p.get("exec_seen", [])) | set(tgt_p.get("exec_seen", [])))
    _conf = bool(ctrl_p.get("exec_conflict") or tgt_p.get("exec_conflict"))
    _conf_note = exec_conflict_note(_conf_seen) if _conf else ""

    # R7（2026-09-20）批量必须靠 token：**全项**证据都来自 mem 兜底 ⇒ 不得判 PASS。
    # 判据与理由串的唯一来源 = common/judge.py（mem_only_evidence / MEM_ONLY_NOTE）；
    # 理由串挂在 mem_note 上 ⇒ 控制台 / .md / .json 三处同源可见。
    if mem_only_evidence(ev_srcs):
        case_pass = False
        _mem_note = ((_mem_note + "；") if _mem_note else "") + MEM_ONLY_NOTE

    # R8（2026-09-21，用户裁定 2026-09-20）：mem 取到的项**一律不算证据**。
    # 逐核状态已落 MEM_NOEVID（上面）⇒ 判定与非预期路径都堵住了；这里只把
    # **整 case 的可区分理由串**挂到 mem_note（控制台 / .md / .json 三处同源）。
    _mem_noevid = [(f"{c}:{n}", v)
                   for n, mv in mem_map.items() for c, v in sorted(mv.items())]
    if _mem_noevid:
        _weak = mem_weak_note([f"{n}(mem={v})" for n, v in _mem_noevid])
        _mem_note = ((_mem_note + "；") if _mem_note else "") + _weak

    return {
        "name": case_name,
        "result": "PASS" if case_pass else "FAIL",
        "ctrl_done": exec_label(ctrl_done, truncated,
                                 conflict=bool(ctrl_p.get("exec_conflict"))),
        "tgt_done_conflict": bool(tgt_p.get("exec_conflict")),
        "exec_conflict": _conf,
        "exec_conflict_note": _conf_note,
        "tgt_done": tgt_done,
        "mem_unavailable": mem_data is None,
        "mem_state": _mem_state,
        "zero_evidence": not has_evidence(tests),
        "evidence_gap": _gaps,
        "evidence_note": _gap_note,
        "mem_note": _mem_note,
        # R8：只在该 case 真有 mem 项时才出现（新增字段，且不污染非 mem case 的产物）
        **({"mem_map": mem_map} if mem_map else {}),
        "tests": tests,
    }


def print_report(results: List[Dict]) -> None:
    print("\n" + "=" * 80)
    print("OTBN Delegation Dual-Core Regression Report")
    print("=" * 80)

    overall = "PASS"
    for case in results:
        print(f"\n## {case['name']}: {case['result']}")
        if case["mem_unavailable"]:
            print("  (mem-unavailable: 内存兜底未生效，MISSING 项按 FAIL 判定)")
        print("  " + mem_state_line(case.get("mem_state", ""),
                                    case.get("mem_note", "")))
        if case.get("evidence_gap"):
            print("  " + case.get("evidence_note", ""))
        if case.get("exec_conflict"):
            print("  " + case.get("exec_conflict_note", ""))
        print(f"| Test | Controller | Target | Result | Source | INFO(actual) | Reason |")
        print(f"|------|------------|--------|--------|--------|--------------|--------|")
        for t in case["tests"]:
            _mv = (case.get("mem_map") or {}).get(t["name"]) or {}
            print(f"| {t['name']} | {mem_noevid_cell(t['ctrl'], _mv.get('CTRL'))} "
                  f"| {mem_noevid_cell(t['tgt'], _mv.get('TGT'))} | {result_text(t['result'])} "
                  f"| {t['src']} | {t['info']} | {t['reason']} |")
        print(f"EXECUTION: CTRL={case['ctrl_done']}, TGT={case['tgt_done']}")
        if case["result"] != "PASS":
            overall = "FAIL"

    print("\n" + "=" * 80)
    print(f"OVERALL: {overall}")
    print("=" * 80)


def write_markdown_report(results: List[Dict], expected: Dict[str, str],
                          report_dir: Path, driver_name: str) -> None:
    """§6.4.2 报告：列序 / 表头 / 单元格转义统一走 common/judge.py。

    与全家族逐字一致（REPORT_TABLE_HEADER）；「期望」列超长截断（完整值见
    tests/COVERAGE.md）。多 case 时表头字段取聚合值，逐 case 明细见下方小节。
    """
    now = datetime.now()
    all_tests = [t for c in results for t in c["tests"]]
    passed = sum(1 for t in all_tests if t["result"] == "PASS")
    failed = sum(1 for t in all_tests if t["result"] == "FAIL")
    # R8：分母不得计入 mem 非证据项；G3：SKIP（主动跳过）同样**不计分母**。
    mem_noevid_n = sum(1 for t in all_tests if t["result"] == MEM_NOEVID)
    skip_n = sum(1 for t in all_tests if t["result"] == "SKIP")
    denom = len(all_tests) - mem_noevid_n - skip_n
    overall = "PASS" if all(r["result"] == "PASS" for r in results) else "FAIL"
    single = len(results) == 1
    names = "、".join(c["name"] for c in results) or "-"
    if single:
        exec_txt = (f"CTRL={results[0].get('ctrl_done')}, "
                    f"TGT={results[0].get('tgt_done')}")
        mem_txt = mem_state_line(results[0].get("mem_state", ""),
                                 results[0].get("mem_note", ""))
    else:
        exec_txt = "; ".join(
            f"{c['name']}: CTRL={c.get('ctrl_done')}, TGT={c.get('tgt_done')}"
            for c in results)
        mem_txt = "; ".join(
            f"{c['name']}: " + mem_state_line(c.get("mem_state", ""),
                                              c.get("mem_note", ""))
            for c in results)

    lines = [
        f"# {driver_name} 测试报告",
        "",
        *format_report_header(
            driver_name, names, "lsqsh_evb@2os",
            date=f"{now:%Y-%m-%d %H:%M:%S}",
            env="见 810_driver_test/README.md 外设接线记录（涉及外部路径的测试项在"
                " COVERAGE.md 标注，SPEC_REFERENCE.md §6.6）",
            execution=exec_txt, mem_line=mem_txt,
            result=f"{overall} ({passed}/{denom}"
                   + (f"；失败 {failed}" if failed else "")
                   + (f"；SKIP {skip_n}" if skip_n else "")
                   + (f"；mem 非证据 {mem_noevid_n}" if mem_noevid_n else "") + ")"),
        "",
    ]
    for case in results:
        lines.append(f"## {case['name']} ({case['result']})")
        lines.append("")
        if case.get("mem_unavailable"):
            lines.append("> mem-unavailable: 内存兜底未生效，token 缺失项按 FAIL 判定")
        if case.get("zero_evidence") or not case.get("tests"):
            lines.append("> 零证据: 未捕获到任何测试项 ⇒ 不得判 PASS")
        if case.get("evidence_gap"):
            lines.append("> " + case.get("evidence_note", ""))
        if case.get("exec_conflict"):
            lines.append("> " + case.get("exec_conflict_note", ""))
        lines.append("")
        lines.append(REPORT_TABLE_HEADER)
        lines.append(REPORT_TABLE_SEP)
        for t in case.get("tests", []):
            _mv = (case.get("mem_map") or {}).get(t["name"]) or {}
            # R8：mem 非证据项的**取值**写进「来源」列（人得能查），否则只标"非证据"
            _src = (mem_noevid_source("/".join(f"{c}={v}" for c, v in sorted(_mv.items())))
                    if _mv else source_label(t.get("src")))
            _why = t.get("reason", "")
            if t["result"] == MEM_NOEVID:
                _why = MEM_WEAK_ITEM_NOTE + (f"；{_why}" if _why else "")
            lines.append(md_table_row([
                t["name"], result_text(t["result"]), _src,
                clip_cell(expected.get(t["name"], "") or "-"),
                t.get("info") or "-", _why]))
        lines.append("")
    lines.append(f"## 汇总\n\nOVERALL: {overall}\n")

    path = report_dir / f"{REPORT_BASE}.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(f"\nMarkdown report written to: {path}")


def write_json_report(results: List[Dict], path: Path) -> None:
    overall = "PASS" if all(r["result"] == "PASS" for r in results) else "FAIL"
    report = {
        "timestamp": datetime.now().isoformat(),
        "board": "lsqsh_evb@2os",
        "overall": overall,
        "cases": results,
    }
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"JSON report written to: {path}")


def list_serial_ports() -> List[str]:
    return [p.device for p in serial.tools.list_ports.comports()]


# ---- L2 处理层规格（common/process.py）：只声明**本工程特有**的接入点 ----
# 本工程判定族特有（D3）：双核同名项 + CTRL 收尾标记 + TGT 结构性无收尾标记的例外
# 规则、mem 兜底、R6 矛盾标记、R7 token 守卫 —— 全在 evaluate_case 里，原样保留。
# 归档按核分文件（serial_CTRL.log / serial_TGT.log）⇒ L2 直接用两个核的行。
# **聚合形态**：一次跑多个 case，但只写**一份**报告（results 列表整体交给渲染）。
# 契约：每工程暴露 l2_spec(facts=None, test_names=None)；facts = 未归档采集事实。
def l2_spec(facts=None, test_names=None):
    # Per-case item sets: the default TEST_NAMES is only a fallback. A case whose
    # test surface differs (KNOWN_ISSUES #60 -- throughput/seq_soak/explicit_api)
    # must resolve its own names, in BOTH the single-case and the multi-case batch
    # path. Resolving it here (inside _judge) rather than once in main() is what
    # fixes the batch defect: main() previously collapsed every case to the
    # default set whenever len(cases) != 1, judging the one-item cases against 24
    # names -> all MISSING -> false FAIL.
    default_names = test_names or TEST_NAMES
    expected = parse_expected_from_coverage()

    def _judge(ci):
        names = TEST_NAMES_BY_CASE.get(ci.case, default_names)
        return evaluate_case(ci.case, ci.lines.get("CTRL", []),
                             ci.lines.get("TGT", []), names, ci.mem)

    def _render_md(jl, cdir):
        write_markdown_report(jl, expected, cdir, DRIVER_NAME)
        return cdir / f"{REPORT_BASE}.md"

    def _render_json(jl, cdir):
        write_json_report(jl, cdir / f"{REPORT_BASE}.json")
        return cdir / f"{REPORT_BASE}.json"

    return process.ProcessSpec(
        project=DRIVER_NAME, driver=DRIVER_NAME, report_base=REPORT_BASE,
        judge=_judge, render_md=_render_md, render_json=_render_json,
        console=lambda jl: print_report(jl),
        status=lambda j: j["result"],
        summary=lambda jl: (
            "PASS" if all(x["result"] == "PASS" for x in jl) else "FAIL",
            sum(1 for c in jl for t in c["tests"] if t["result"] == "PASS"),
            sum(1 for c in jl for t in c["tests"]
                if t["result"] not in ("NOT_RUN", MEM_NOEVID))),
        log_archived=lambda jp, mp: print(f"[run] archived -> {mp}"),
    )


def main() -> int:
    # 行缓冲输出：批量回归跑 ~10min 级，块缓冲会让 [BUILD]/[RUN] 进度
    # 全程不可见（误判为"卡死"）
    try:
        sys.stdout.reconfigure(line_buffering=True)
        sys.stderr.reconfigure(line_buffering=True)
    except Exception:
        pass

    parser = argparse.ArgumentParser(description="OTBN delegation dual-core regression agent (ztest-native)")
    parser.add_argument("--case", help="Run a single case (default: all case_* dirs)")
    parser.add_argument("--ctrl-uart", help="Controller UART port")
    parser.add_argument("--tgt-uart", help="Target UART port")
    parser.add_argument("--session-dir", default=None,
                        help="归档会话目录（reports/<date>-<idx> 或任意路径）；"
                             "不带则按当天自动分配新会话目录")
    parser.add_argument(
        "--case-timeout",
        type=int,
        default=int(os.environ.get("I3C_CASE_TIMEOUT", "60")),
        help="Seconds per case run (default: 60; 仅兜底——正常由 10s 静默判据提前结束)",
    )
    parser.add_argument(
        "--no-archive",
        action="store_true",
        help="Print report to console only, skip reports/ archiving",
    )
    parser.add_argument(
        "--build-only",
        action="store_true",
        help="Build all cases and exit",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Assume images are already built",
    )
    args = parser.parse_args()
    ctrl_port = args.ctrl_uart or os.environ.get("I3C_CTRL_UART")
    tgt_port = args.tgt_uart or os.environ.get("I3C_TGT_UART")

    if not ctrl_port or not tgt_port:
        print("ERROR: I3C_CTRL_UART and I3C_TGT_UART must be set or passed via --ctrl-uart/--tgt-uart", file=sys.stderr)
        ports = list_serial_ports()
        if ports:
            print("Available serial ports:", ", ".join(ports), file=sys.stderr)
        else:
            print("No serial ports detected.", file=sys.stderr)
        return 1

    cases = find_cases(args.case)
    if not cases:
        print(f"ERROR: no cases found in {TESTS_DIR}", file=sys.stderr)
        return 1

    print(f"Cases: {[c.name for c in cases]}")
    print(f"Controller UART: {ctrl_port}")
    print(f"Target UART: {tgt_port}")
    print(f"Case timeout: {args.case_timeout}s")
    session_dir = None   # 延迟分配：原始串口归档与报告共用同一会话目录
    # ★ 必须在 `_DUAL` 的 session lambda **之前**求值：lambda 闭包捕获本变量，
    # 而 dual_run() 在 L1 采集成功后立刻调用它（早于本函数尾部）。带
    # `--session-dir` 时 `session_dir or (...)` 短路，这条路径被掩盖过。
    date_str = datetime.now().strftime("%Y-%m-%d")

    # Build phase.
    if not args.skip_build:
        bash = acquire.find_git_bash("filtered")
        for case_dir in cases:
            try:
                acquire.build_via_test_sh_raise(case_dir, bash)
            except RuntimeError as exc:
                print(f"ERROR: {exc}", file=sys.stderr)
                return 1

    if args.build_only:
        print("\nBuild-only requested; skipping run phase.")
        return 0

    # L2 规格：expected 由 l2_spec() 内部构造。test_names=None ⇒ l2_spec 内部
    # 按 case 解析（TEST_NAMES_BY_CASE，缺省 TEST_NAMES）——单跑与批量走同一路径
    # （批量缺陷的修复点：不再在 main() 里把多 case 折叠成默认 24 项）。
    _L2_SPEC = l2_spec({"ctrl_uart": ctrl_port, "tgt_uart": tgt_port}, None)
    print("Tests per case: "
          + ", ".join(f"{c.name}={len(TEST_NAMES_BY_CASE.get(c.name, TEST_NAMES))}"
                      for c in cases))

    # L1 采集规格（common/acquire.py）：两遍烧录 + 双核采集 + 静默判完成 + §10.22 自愈重试
    _DUAL = acquire.DualRunSpec(
        driver=DRIVER_NAME, root=SAMPLE_ROOT, tests_dir=SAMPLE_ROOT / "tests",
        board="lsqsh_evb@2os", ctrl_uart=ctrl_port, tgt_uart=tgt_port,
        case_timeout=args.case_timeout, archive=not args.no_archive,
        script=__file__, banner_key="ZT_BOOT", announce=False,
        abort_on_load_fail=False,
        ctrl_banners=CTRL_BANNERS, tgt_banners=TGT_BANNERS,
        # 双核各自把 ztest 逐项输出打到本核串口（无单出口/无 RLOG）⇒ 两核
        # 行都按自己端口采集，relay_prefixes 留空（不从中继流分拣）。
        relay_prefixes=(),
        relay_strip=False,
        session=lambda: session_dir or (
            Path(args.session_dir) if args.session_dir
            else allocate_session_dir(REPORTS_ROOT, date_str)))

    # Run phase: preflight -> each case: L1（clean → 烧录采集 → 落归档 → mem）-> L2 judge
    acquire.preflight_gdb(style="d3")
    results: List[Dict] = []
    for case_dir in cases:
        acq = acquire.dual_run(_DUAL, case_dir)
        if acq.rc:
            return acq.rc
        if not acq.ok:
            return 1
        session_dir = acq.session
        # L1→L2 边界（L0 归档形状）：两核原始行**分核**归一；mem 兜底是 L1 侧产物
        # （读目标内存，不进归档）⇒ 作为独立入参交给 L2（common/process.py）。
        results.append(process.judge(_L2_SPEC, process.L2Input(
            case=case_dir.name, lines=acq.lines, mem=acq.mem)))

    print_report(results)

    if args.no_archive:
        print("\n--no-archive: skipping reports/ archiving.")
        return 0 if all(r["result"] == "PASS" for r in results) else 1

    # Archive (§12)：会话目录 reports/<date>-<idx>/，每 case 子目录
    # <driver>_<case>/<driver>_report.{md,json} + summary.md 索引行
    # （落盘 / 汇总走 common/process.py::emit，与既有写法逐字一致）。
    # date_str 已在 `_DUAL` 之前求值（同一天、同一进程内取值不变），此处不重复赋值。
    session_dir = session_dir or (
            Path(args.session_dir) if args.session_dir
        else allocate_session_dir(REPORTS_ROOT, date_str))
    case_label = args.case or (cases[0].name if len(cases) == 1 else "all_cases")
    process.emit(_L2_SPEC, results, session_dir, case_label=case_label)
    return process.rc(_L2_SPEC, results)


if __name__ == "__main__":
    sys.exit(main())
