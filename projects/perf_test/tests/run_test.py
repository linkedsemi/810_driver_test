#!/usr/bin/env python3
"""perf_test 双核性能回归入口（cpu1 硬件引擎 + cpu2 OTBN，结果统一 cpu1）。

判定（本项目专属，与 app_iwdg_cpu2_test 同型）：
    case PASS = 两核各自 ZT_BOOT + 至少 1 个测试项 + 全部项 PASS +
                PROJECT EXECUTION SUCCESSFUL
cpu1(TGT) 运行 perf_crypto / perf_dma / perf_mbox / perf_trng；
cpu2(CTRL) 运行 mbox echo 伴测并跑一个 peer_alive 测试项。
两核测试项结构上不同名（被测对象不同）——这是与通用双核判据的有意偏离，
与 app_iwdg_cpu2_test / i3c_dualcore_test 同源，见 SPEC_REFERENCE §6.2.4。

性能数值以 `PERF <module> | <op> | ...` 行输出；速率不设硬门限，
功能通路仍由各套件的 zassert 守住。

归档：reports/<YYYY-MM-DD>-<idx>/<driver>_<case>/<driver>_report.{md,json}
"""

import argparse
import os
import re
import sys
from datetime import datetime
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

SAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
from ztest_report import parse_ztest  # noqa: E402
import process  # noqa: E402  —— L2（归档 → 判定 → 报告）
import acquire  # noqa: E402  —— L1（构建/烧录/采集/落归档）
from report_dir import allocate_session_dir  # noqa: E402
from judge import (REPORT_TABLE_HEADER, REPORT_TABLE_SEP,  # noqa: E402
                   format_report_header, has_evidence, md_table_row,
                   result_text, truncation_evidence)

DRIVER_NAME = "perf_test"
REPORT_BASE = f"{DRIVER_NAME}_report"

CTRL_BANNERS = ("CPU2 PERF_PEER",)
TGT_BANNERS = ("CPU1 PERF",)

# Link order = SORT_BY_NAME over z_ztest_unit_test__<suite>__<test>:
#   perf_crypto (11) -> perf_dma (1) -> perf_flash (6) -> perf_mbox (1)
#   -> perf_trng (2).
TGT_TEST_NAMES = [
    "aes128_cbc", "aes128_ctr", "aes128_ecb",
    "sha256", "sha256_unaligned",
    "sha512", "sha512_unaligned",
    "sm3", "sm3_unaligned",
    "sm4_ctr", "sm4_ecb",
    "mem2mem",
    "flash1_erase", "flash1_read", "flash1_write",
    "flash2_erase", "flash2_read", "flash2_write",
    "roundtrip",
    "trng0_throughput", "trng1_throughput",
]
CTRL_TEST_NAMES = [
    "sha256_otbn", "sha256_otbn_unaligned",
    "sha512_otbn", "sha512_otbn_unaligned",
    "sm3_otbn", "sm3_otbn_unaligned",
    "rsa2048_modexp",
    "proxy_erase", "proxy_read", "proxy_write", "peer_alive",
]

CORE_PREFIX = {"CTRL": "cpu2::", "TGT": "cpu1::"}

# cpu2 does not print test data locally: its lines arrive on cpu1's console
# with this envelope (see cpu1_perf_test/src/perf_telemetry.c).
CTRL_RELAY_PREFIX = "PERF_TEST:CTRL:"


def split_ctrl_relay(lines_by_core):
    """Move relayed cpu2 lines from the cpu1 stream back to the CTRL stream.

    The framework's own split_relay_lines assumes CTRL is the primary serial
    outlet; this project is the opposite way round, so do the split here.
    """
    ctrl = list(lines_by_core.get("CTRL") or [])
    tgt = []
    for ln in lines_by_core.get("TGT") or []:
        s = str(ln)
        idx = s.find(CTRL_RELAY_PREFIX)
        if idx >= 0:
            ctrl.append(s[:idx] + s[idx + len(CTRL_RELAY_PREFIX):])
        else:
            tgt.append(ln)
    return {"CTRL": ctrl, "TGT": tgt}


# --- PERF lines -> report "实测" column -------------------------------------
# The firmware prints rates with printk (not ZT_INFO), so parse_ztest cannot
# attach them to a test item.  Extract them here and merge into the report's
# per-item info dict, keyed by the test name.
_PERF_RE = re.compile(
    r"PERF\s+(\w+)\s*\|\s*([A-Za-z0-9_]+)\s*\|\s*(.*)$")


def _perf_test_name(module, op):
    if module == "flash":
        return op.replace("qspi1_", "flash1_").replace("qspi2_", "flash2_")
    if module == "trng":
        return {"trng0_apb": "trng0_throughput",
                "trng1_dw": "trng1_throughput"}.get(op, op)
    if module == "mbox":
        return "roundtrip"
    if module == "flash_proxy":
        return "proxy_" + op
    return op


def perf_infos(lines, core):
    out = {}
    for ln in lines or []:
        m = _PERF_RE.search(str(ln))
        if not m:
            continue
        module, op, rest = m.group(1), m.group(2), m.group(3)
        fields = {}
        for part in rest.split("|"):
            part = part.strip()
            if "=" in part:
                key, val = part.split("=", 1)
                fields[key.strip()] = val.strip()
        entry = out.setdefault(f"{core}::{_perf_test_name(module, op)}", {})
        if "bytes" in fields and "MB/s" in fields:
            try:
                kb = int(fields["bytes"]) // 1024
            except ValueError:
                kb = 0
            entry[f"mb_s_{kb}k"] = fields["MB/s"]
        if "ops/s" in fields:
            entry["ops_s"] = fields["ops/s"]
    return out


def log(msg):
    print(msg, flush=True)


def evaluate(case_name, ctrl_lines, tgt_lines):
    ctrl = parse_ztest(ctrl_lines, ctrl_banners=CTRL_BANNERS,
                       tgt_banners=TGT_BANNERS)
    tgt = parse_ztest(tgt_lines, ctrl_banners=CTRL_BANNERS,
                      tgt_banners=TGT_BANNERS)

    items = []
    for name, v in sorted(ctrl["tests"].items()):
        items.append({"name": f"cpu2::{name}", "result": v.get("result"),
                      "reason": v.get("reason", "")})
    for name, v in sorted(tgt["tests"].items()):
        items.append({"name": f"cpu1::{name}", "result": v.get("result"),
                      "reason": v.get("reason", "")})

    core_items = {"CTRL": [i for i in items if i["name"].startswith("cpu2::")],
                  "TGT": [i for i in items if i["name"].startswith("cpu1::")]}

    ctrl_ok = (ctrl["boot_seen"] and ctrl["execution"] == "SUCCESSFUL"
               and has_evidence(core_items["CTRL"])
               and all(i["result"] == "PASS" for i in core_items["CTRL"]))
    tgt_ok = (tgt["boot_seen"] and tgt["execution"] == "SUCCESSFUL"
              and has_evidence(core_items["TGT"])
              and all(i["result"] == "PASS" for i in core_items["TGT"]))

    if not ctrl["boot_seen"]:
        items.append({"name": "cpu2::boot", "result": "FAIL",
                      "reason": "未收到 ZT_BOOT（load 后无日志 / 固件未跑）"})
    elif not has_evidence(core_items["CTRL"]):
        items.append({"name": "cpu2::evidence", "result": "FAIL",
                      "reason": "零证据：cpu2 没有任何测试项"})
    if not tgt["boot_seen"]:
        items.append({"name": "cpu1::boot", "result": "FAIL",
                      "reason": "未收到 ZT_BOOT（load 后无日志 / 固件未跑）"})
    elif not has_evidence(core_items["TGT"]):
        items.append({"name": "cpu1::evidence", "result": "FAIL",
                      "reason": "零证据：cpu1 没有任何测试项"})

    # F1 truncation acceptance: a window that ends after every declared item
    # has a definite result (but before the closing marker) is not a failure.
    ctrl_trunc = tgt_trunc = False
    for core, declared, boot, execution in (
            ("CTRL", CTRL_TEST_NAMES, ctrl["boot_seen"], ctrl["execution"]),
            ("TGT", TGT_TEST_NAMES, tgt["boot_seen"], tgt["execution"])):
        prefix = CORE_PREFIX[core]
        status = {i["name"][len(prefix):]: i["result"]
                  for i in core_items[core]}
        ev_ok, _missing = truncation_evidence(execution, declared, status)
        trunc = (ev_ok and boot and bool(core_items[core])
                 and all(i["result"] == "PASS" for i in core_items[core]))
        if core == "CTRL":
            ctrl_trunc = trunc
        else:
            tgt_trunc = trunc

    if ctrl_trunc:
        ctrl_ok = True
    if tgt_trunc:
        tgt_ok = True

    infos = {**{f"cpu2::{k}": v for k, v in ctrl["infos"].items()},
             **{f"cpu1::{k}": v for k, v in tgt["infos"].items()}}
    for key, val in {**perf_infos(ctrl_lines, "cpu2"),
                     **perf_infos(tgt_lines, "cpu1")}.items():
        infos.setdefault(key, {}).update(val)

    return {
        "case": case_name,
        "result": "PASS" if (ctrl_ok and tgt_ok) else "FAIL",
        "ctrl_execution": ("TRUNCATED_AFTER_ALL_ITEMS" if ctrl_trunc
                           else (ctrl["execution"] or "UNKNOWN")),
        "tgt_execution": ("TRUNCATED_AFTER_ALL_ITEMS" if tgt_trunc
                          else (tgt["execution"] or "UNKNOWN")),
        "items": items,
        "infos": infos,
    }


def _scored_items(items):
    return [it for it in items if not str(it.get("name", "")).startswith("run::")]


def print_report(results):
    print("\n--- perf_test 双核测试结果 ---")
    for r in results:
        items = r.get("items", [])
        scored = _scored_items(items)
        passed = sum(1 for i in scored if i.get("result") == "PASS")
        print(f"## {r['case']}: {r['result']} ({passed}/{len(scored)}) "
              f"[CTRL(cpu2)={r.get('ctrl_execution')}, "
              f"TGT(cpu1)={r.get('tgt_execution')}]")
        for it in items:
            extra = f"  reason: {it['reason']}" if it.get("reason") else ""
            print(f"  {it['name']:<44} {it.get('result')}{extra}")


def write_markdown_report(cdir, result):
    base = cdir / f"{REPORT_BASE}.md"
    items = result.get("items", [])
    scored = _scored_items(items)
    passed = sum(1 for it in scored if it.get("result") == "PASS")
    status = result.get("result") or "FAIL"
    core_exec = ", ".join(
        f"{k.upper()}={result[k]}"
        for k in ("ctrl_execution", "tgt_execution") if result.get(k))
    lines = [
        f"# {DRIVER_NAME} 测试报告",
        "",
        *format_report_header(
            DRIVER_NAME, result.get("case", ""),
            "lsqsh_evb@2os（cpu1 / cpu2）",
            date=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            env="无外部接线（TRNG/crypto/DMA 片内；mbox 走共享 SRAM）",
            # The dual-core judge returns ctrl_execution/tgt_execution, not
            # "execution"; omitting the field avoids a misleading
            # "执行标记: UNKNOWN".  The real values are in the next line.
            mem_line="内存兜底: 不适用（本工程无内存兜底）",
            result=f"{status} ({passed}/{len(scored)})"),
        *([f"- 核执行标记: {core_exec}"] if core_exec else []),
        "",
        "## 测试项结果",
        "",
        REPORT_TABLE_HEADER,
        REPORT_TABLE_SEP,
    ]
    infos = result.get("infos", {})
    for it in items:
        name = it.get("name", "?")
        kv = infos.get(name) or infos.get(
            name.replace("cpu2::", "").replace("cpu1::", ""))
        actual = ", ".join(f"{k}={v}" for k, v in kv.items()) if kv else "-"
        lines.append(md_table_row([
            name, result_text(it.get("result", "?")), "token",
            "-", actual, it.get("reason", "")]))
    lines.append("")
    lines.append(f"## 汇总\n\nOVERALL: {status}\n")
    base.write_text("\n".join(lines), encoding="utf-8")
    return base


def l2_spec(facts=None):
    def _judge(ci):
        result = evaluate(ci.case, ci.lines["CTRL"], ci.lines["TGT"])
        if ci.facts.get("timed_out"):
            result["items"].append({
                "name": "run::timeout", "result": "NOTE",
                "reason": f"case_timeout={ci.facts.get('case_timeout')}s "
                          "到点截断（非失败）"})
        return result

    return process.ProcessSpec(
        project=DRIVER_NAME, driver=DRIVER_NAME, report_base=REPORT_BASE,
        judge=_judge,
        render_md=lambda jl, cdir: write_markdown_report(cdir, jl[0]),
        console=lambda jl: print_report(jl),
    )


def main():
    ap = argparse.ArgumentParser(description="perf_test dual-core runner")
    ap.add_argument("--case", default="case_default")
    ap.add_argument("--ctrl-uart", default=None, help="cpu2 控制台（uart5）")
    ap.add_argument("--tgt-uart", default=None, help="cpu1 控制台（uart1）")
    ap.add_argument("--session-dir", default=None)
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--no-archive", action="store_true")
    ap.add_argument("--build-only", action="store_true")
    ap.add_argument("--case-timeout", type=int, default=180)
    args = ap.parse_args()

    ctrl_uart = args.ctrl_uart or os.environ.get("I3C_CTRL_UART")
    tgt_uart = args.tgt_uart or os.environ.get("I3C_TGT_UART")
    if not ctrl_uart or not tgt_uart:
        log("ERROR: --ctrl-uart/--tgt-uart required（双核脚本不支持自动发现）")
        return 1

    log(f"Controller UART (cpu2): {ctrl_uart}")
    log(f"Target UART (cpu1):     {tgt_uart}")
    log(f"Case timeout: {args.case_timeout}s")

    case_dir = SAMPLE_ROOT / "tests" / args.case
    if not case_dir.is_dir():
        log(f"ERROR: case not found: {case_dir}")
        return 1

    if not args.skip_build:
        bash = acquire.find_git_bash("filtered")
        try:
            acquire.build_via_test_sh_raise(case_dir, bash)
        except RuntimeError as exc:
            log(f"ERROR: {exc}")
            return 1

    if args.build_only:
        log("Build-only requested; skipping run phase.")
        return 0

    date_str = datetime.now().strftime("%Y-%m-%d")
    session_dir = None

    spec = acquire.DualRunSpec(
        driver=DRIVER_NAME, root=SAMPLE_ROOT,
        tests_dir=SAMPLE_ROOT / "tests", case=args.case,
        board="lsqsh_evb@2os", ctrl_uart=ctrl_uart, tgt_uart=tgt_uart,
        case_timeout=args.case_timeout, skip_build=True,
        archive=not args.no_archive, script=__file__,
        ctrl_banners=CTRL_BANNERS, tgt_banners=TGT_BANNERS,
        abort_on_load_fail=True,
        session=lambda: session_dir or (
            Path(args.session_dir) if args.session_dir
            else allocate_session_dir(REPORTS_ROOT, date_str)))

    acquire.preflight_gdb(style="d3")
    acq = acquire.dual_run(spec, case_dir)
    if acq.rc:
        return acq.rc
    if not acq.ok:
        return 1
    session_dir = acq.session

    l2 = l2_spec()
    lines = split_ctrl_relay(acq.lines)
    results = [process.judge(l2, process.L2Input(
        case=case_dir.name, lines=lines, mem=acq.mem))]
    print_report(results)

    if args.no_archive:
        return 0 if all(r["result"] == "PASS" for r in results) else 1

    process.emit(l2, results, session_dir, case_label=args.case)
    return process.rc(l2, results)


if __name__ == "__main__":
    sys.exit(main())
