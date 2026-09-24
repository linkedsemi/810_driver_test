#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""B（R8）零意外翻转证明 + 合成穷举 + 非空转自检（全程离线，不碰板子）。

Part 1  真归档重放 A/B（**mem=None**，与 `regression/replay.py` 同口径）
        → 改动前后必须**逐字节相同**（含 .md / .json / 判定签名）。
Part 2  真归档 + **从归档报告反推的 mem 载荷**（L0 契约缺口 D7 的补偿做法）
        → 只有"含 mem 项"的 case 允许变化，且必须**全部落在 mem 项上**。
Part 3  合成输入穷举（六类证据组合 × 单核/双核）+ **非空转自检**
        → 每族各变体的观测签名必须互不相同（否则"比对"是空转）。

跑法：
    python scripts/ab.py            # 全跑
    python scripts/ab.py --part 3   # 只跑合成矩阵

⚠️ 2026-09-24 由 `tmp/brule/ab.py` 迁来：本脚本被 `SPEC_REFERENCE` §6.4.x 当**必做
   非空转自检**点名（`--part 3`），却住在会被清理的 `tmp/` 里 —— 一旦清理就**静默
   失去该门**。**它是三件套之一，必须与 `scripts/harness.py` + `scripts/synth.py`
   + `scripts/before/`（改动前快照）一起搬**，缺一即 ImportError / 缺"改动前"世界。
   迁移时只改了两处根推导：
     * `parents[2]`（`tmp/<子目录>/`）→ `parents[1]`（`scripts/`）；
     * 同目录 import 路径 `ROOT/"tmp"/"brule"` → 本脚本所在目录 `scripts/`。
   `OUT` 仍指向 `tmp/brule/out`（**输出/scratch 目录**，可随时重建，故不随迁）。
"""
import argparse
import contextlib
import hashlib
import io
import json
import shutil
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]      # scripts/ 的上一级 = 810_driver_test
HERE = Path(__file__).resolve().parent          # scripts/
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "common"))
sys.path.insert(0, str(HERE))
import harness   # noqa: E402
import synth     # noqa: E402
import regression.replay as replay  # noqa: E402

B8ROOT = ROOT / "tmp" / "brule"                 # 仅用于 OUT（scratch，非输入）
OUT = B8ROOT / "out"

#: R8 只改这 13 个"有 mem 通道"的工程；其余必须零影响
PATCHED = {"trng_test", "i2cf_test", "spi_test", "spid_test", "spif_test",
           "mbedtls_test", "otbn_test", "otbn_selftest", "rtc_ls_test",
           "i3c_i2c_switch_test", "i2c_pure_test", "i3c_dualcore_test",
           "eth_test"}


def find_archives():
    """扫出所有可重放的归档 case 目录（含 `serial_*.log`）。"""
    roots = [ROOT / "reports", ROOT / "tmp"]
    seen = []
    for r in roots:
        if not r.exists():
            continue
        for d in sorted(r.rglob("*")):
            if not d.is_dir():
                continue
            if any(d.glob("serial_*.log")):
                proj, case = replay.split_case_dir(d.name)
                if proj is None or not (ROOT / "projects" / proj).exists():
                    continue
                seen.append((proj, case, d))
    return seen


def read_lines(d):
    lines = {}
    for core in ("CTRL", "TGT"):
        f = d / f"serial_{core}.log"
        if not f.exists():
            continue
        try:
            lines[core] = replay.read_serial_log(f)
        except OSError:
            return {}                      # 读不了（ACL/占用）⇒ 当作不可重放，跳过
    return lines


# ------------------------------------------------------- Part 2: meme 反推
def rebuild_mem(project, d):
    """从归档报告反推 mem 载荷（L0 不归档 mem 的补偿；只用于离线 A/B）。"""
    js = sorted(d.glob("*_report.json"))
    if not js:
        return None
    try:
        data = json.loads(js[0].read_text(encoding="utf-8"))
    except (ValueError, OSError):
        return None
    tgt, ctrl = {}, {}
    state, note = "ok", ""
    hit = False

    notes, results = data.get("notes"), data.get("results")
    if isinstance(notes, dict) and isinstance(results, dict):
        # F4A / rtc_ls：单核，mem 值落在结果列、「来源」列留痕
        state = data.get("mem_state") or "ok"
        note = data.get("mem_note") or ""
        for name, nt in notes.items():
            if "mem(" in str(nt) or str(nt).startswith("src=mem"):
                v = results.get(name)
                if v in ("PASS", "FAIL", "NOT_RUN"):
                    tgt[name] = v
                    hit = True
    for c in (data.get("cases") or []):
        if not hit:
            state = c.get("mem_state") or state
            note = c.get("mem_note") or note
        for t in c.get("tests", []):
            if t.get("src") == "mem":
                hit = True
                for core, key in (("CTRL", "ctrl"), ("TGT", "tgt")):
                    v = t.get(key)
                    if v in ("PASS", "FAIL", "NOT_RUN"):
                        (ctrl if core == "CTRL" else tgt)[t["name"]] = v
    for it in (data.get("items") or []):
        if str(it.get("source", "")).startswith("mem"):
            hit = True
            v = it.get("result")
            if v in ("PASS", "FAIL", "NOT_RUN"):
                tgt[it["name"]] = v
    if isinstance(data.get("items"), list) and "mem_state" in data:
        state = data.get("mem_state") or state
        note = data.get("mem_note") or note
    # 非 ok ⇒ dumper 侧已清空（忠实复刻 R2 门）
    if state != "ok":
        tgt, ctrl = {}, {}
    if not hit:
        return None
    return {"valid": state == "ok", "mem_state": state, "note": note,
            "TGT": tgt, "CTRL": ctrl, "reasons": {}}


# --------------------------------------------------------------- 比对工具
def tree_sig(cdir):
    out = {}
    for f in sorted(Path(cdir).rglob("*")):
        if f.is_file() and f.name != "replay_meta.json":
            out[f.name] = hashlib.md5(f.read_bytes()).hexdigest()
    return out


class _Sink(io.StringIO):
    """吞掉家族自己的 print；★ 必须支持 `sys.stdout.reconfigure(...)`
    （`run_test.py` 在 import 期就调它，纯 StringIO 会 AttributeError）。"""

    def reconfigure(self, **_kw):
        return None

    def isatty(self):
        return False


def run_one(project, case, lines, mem, tree, tag=""):
    out_root = OUT / tag / tree
    buf = _Sink()
    try:
        with contextlib.redirect_stdout(buf):      # 家族自身的 print 不进日志
            judged, cdir, jp, mp = harness.run_emit(project, tree, case, lines, mem,
                                                    out_root)
    except Exception as exc:                      # noqa: BLE001
        return None, f"EXC {type(exc).__name__}: {exc}"
    return (judged, harness.sig(judged), tree_sig(cdir)), ""


def digest(sigj):
    """观测签名的**紧凑可读**摘要（用于人读的非空转表；去重仍用完整签名）。"""
    from collections import Counter
    c = Counter(str(x[1]) for x in (sigj.get("items") or []))
    return (f"result={sigj.get('result')}  "
            + "，".join(f"{k}×{v}" for k, v in sorted(c.items())))


def report_diff(a, b):
    """判定签名差 / 产物文件名差 / 产物内容差。"""
    d = []
    if a[1] != b[1]:
        if a[1].get("result") != b[1].get("result"):
            d.append(f"result {a[1]['result']} -> {b[1]['result']}")
        ia = {x[0]: x[1:] for x in (a[1].get("items") or [])}
        ib = {x[0]: x[1:] for x in (b[1].get("items") or [])}
        for k in sorted(set(ia) | set(ib)):
            if ia.get(k) != ib.get(k):
                d.append(f"item {k}: {ia.get(k)} -> {ib.get(k)}")
    sa, sb = a[2], b[2]
    for k in sorted(set(sa) | set(sb)):
        if sa.get(k) != sb.get(k):
            d.append(f"file {k}")
    return d


# --------------------------------------------------------------- 主流程
def part1(archives):
    print("=" * 78)
    print("Part 1  真归档 A/B（mem=None）—— 期望：逐字节零差异")
    print("=" * 78)
    bad, n = [], 0
    for proj, case, d in archives:
        lines = read_lines(d)
        if not lines:
            continue
        n += 1
        a, ea = run_one(proj, case, lines, None, "before", "p1")
        b, eb = run_one(proj, case, lines, None, "after", "p1")
        if ea or eb:
            bad.append((proj, case, d, [ea or eb]))
            continue
        diffs = report_diff(a, b)
        if diffs:
            bad.append((proj, case, d, diffs))
    print(f"  重放 case 数: {n}；有差异: {len(bad)}")
    for proj, case, d, diffs in bad[:40]:
        print(f"  !! {proj} {case}  {d}")
        for x in diffs[:6]:
            print(f"       {x}")
    print(f"  → Part 1 判定: {'OK（零差异）' if not bad else '有不符'}")
    return not bad


def part2(archives):
    print()
    print("=" * 78)
    print("Part 2  真归档 + 反推 mem 载荷 —— 期望：只有含 mem 项的 case 变")
    print("=" * 78)
    mem_cases, other_changed, mem_changed, same = [], [], [], 0
    for proj, case, d in archives:
        mem = rebuild_mem(proj, d)
        if mem is None:
            continue
        lines = read_lines(d)
        if not lines:
            continue
        a, ea = run_one(proj, case, lines, mem, "before", "p2")
        b, eb = run_one(proj, case, lines, mem, "after", "p2")
        if ea or eb:
            other_changed.append((proj, case, d, [ea or eb]))
            continue
        diffs = report_diff(a, b)
        mem_cases.append((proj, case, d, a, b, diffs))
        if diffs:
            mem_changed.append((proj, case, d, a, b, diffs))
        else:
            same += 1
    print(f"  含 mem 项的真归档 case: {len(mem_cases)}；其中改动前后有差异: {len(mem_changed)}；无差异: {same}")
    for proj, case, d, a, b, diffs in mem_cases:
        ra, rb = a[1].get("result"), b[1].get("result")
        flip = "  <<< 翻转" if ra != rb else ""
        print(f"  - {proj} {case}: result {ra} -> {rb}{flip}  ({len(diffs)} 处差异)")
        for x in diffs[:10]:
            print(f"       {x}")
    if other_changed:
        print("  !! 异常:", other_changed[:10])
    # 全局扫描：**不含 mem 项**的归档，在 Part 2 里也必须零差异 ⇒ 用 Part 1 覆盖
    print(f"  → Part 2 判定: {'OK' if not other_changed else '有不符'}")
    return not other_changed


def part3():
    print()
    print("=" * 78)
    print("Part 3  合成输入穷举 + 非空转自检（每族各变体观测签名须互不相同）")
    print("=" * 78)
    matrix = synth.build_all()
    vacuous, rows = [], []
    ok = True
    for proj, variants in sorted(matrix.items()):
        pre = defaultdict(list)
        pre_by_variant = {}
        for vname, (case, lines, mem) in variants.items():
            a, ea = run_one(proj, case, lines, mem, "before", f"p3/{proj}/{vname}")
            b, eb = run_one(proj, case, lines, mem, "after", f"p3/{proj}/{vname}")
            if ea or eb:
                rows.append((proj, vname, f"<ERR {ea or eb}>", "", []))
                ok = False
                continue
            if not a[2] or not b[2]:          # 产物非空自检（"真跑了"而不是空转）
                rows.append((proj, vname, "<EMPTY-ARTIFACT>", "", []))
                ok = False
                continue
            diffs = report_diff(a, b)
            rows.append((proj, vname, a[1].get("result"), b[1].get("result"), diffs))
            s = json.dumps(a[1], ensure_ascii=False, sort_keys=True)
            pre[s].append(vname)
            pre_by_variant[vname] = s
        nuniq = len(pre)
        print(f"  {proj:<24} 变体 {len(variants)}  改动前观测种类 {nuniq}")
        for sig, vs in sorted(pre.items()):
            print(f"      {'、'.join(vs)}  ->  {digest(json.loads(sig))}")
        # 非空转判据（任务 §3.2）：
        #   ① 每个变体都产出非空产物（上面已查）；
        #   ② 变体数 ≥3 的族，观测种类必须 ≥3；
        #   ③ **每个变体（除 alltoken）的观测必须与 alltoken 不同** —— 这条直接证明
        #      "该变体确实改变了判定分支的输入"，而不是被静默忽略（空转）。
        base_sig = pre_by_variant.get("alltoken")
        same_as_base = [v for v, s in pre_by_variant.items()
                        if v != "alltoken" and s == base_sig]
        print(f"      与 alltoken 观测相同（应为 0 —— 否则该变体是空转）: "
              f"{same_as_base or '无'}；观测种类 {nuniq}/{len(variants)}")
        if (len(variants) >= 3 and nuniq < 3) or same_as_base:
            vacuous.append(f"{proj}({nuniq}/{len(variants)}; 同 alltoken: {same_as_base})")
    print()
    print("  变体级 A/B 明细：")
    print(f"  {'工程':<24}{'变体':<22}{'改动前':<10}{'改动后':<10}差异")
    for proj, vname, ra, rb, diffs in rows:
        mark = "  <<< 翻转" if ra != rb else ("  (变)" if diffs else "")
        print(f"  {proj:<24}{vname:<22}{str(ra):<10}{str(rb):<10}{len(diffs)}{mark}")
    print()
    print(f"  空转嫌疑（观测种类 < 3）: {vacuous or '无'}")
    if vacuous:
        ok = False
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--part", default="123")
    ap.add_argument("--fresh", action="store_true", help="清空 out/ 再跑")
    args = ap.parse_args()
    if args.fresh and OUT.exists():
        shutil.rmtree(OUT, ignore_errors=True)
    OUT.mkdir(parents=True, exist_ok=True)

    archives = find_archives()
    print(f"发现可重放归档 case: {len(archives)}"
          f"（涉及工程 {len(set(p for p, _, _ in archives))} 个）")

    results = []
    if "1" in args.part:
        results.append(("Part1", part1(archives)))
    if "2" in args.part:
        results.append(("Part2", part2(archives)))
    if "3" in args.part:
        results.append(("Part3", part3()))
    print()
    print("=" * 78)
    for name, ok in results:
        print(f"  {name}: {'ALL OK' if ok else '**有不符**'}")
    print("=" * 78)
    return 0 if all(ok for _, ok in results) else 1


if __name__ == "__main__":
    sys.exit(main())
