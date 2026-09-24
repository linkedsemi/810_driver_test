#!/usr/bin/env python3
"""R7 重放（离线、不碰板子）：

  Part A  四条基准场景 —— 新规则 vs 旧规则，必须**零翻转**（并演示"该翻的那个翻"）。
  Part B  全部归档报告 —— 只有「整个 case 一条串口 token 都没有」的 case 才翻转。
  Part C  dbg_run.py（调试路径）不受影响的结构证据。

做法：**直接调用各家族真实的判定函数**（import 真实 run_test.py 模块），
用 `mem_only_evidence` 的猴子补丁构造"R7 前/R7 后"两个世界 —— 这样新旧之差
恰好只有 R7 那一处，不是重写一遍判定式。

⚠️ 2026-09-24 由 `tmp/r7/replay_r7.py` 迁来：本脚本被 `SPEC_REFERENCE` 当**证据脚本**
   点名，却住在会被清理的 `tmp/` 里 —— 一旦清理就**静默失去该证据脚本**。
   迁移时只改了一处：数据 `tmp/r7/archive_sources.json` → `scripts/archive_sources.json`。
   根目录本来就是**硬编码绝对路径**（`d:/zephyr4_0/810_driver_test`），按"硬编码照搬"
   规则**未改**；换仓库根目录时需同步这一行。
"""
import importlib.util
import json
import sys
from pathlib import Path

ROOT = Path("d:/zephyr4_0/810_driver_test")
sys.path.insert(0, str(ROOT / "common"))
import judge  # noqa: E402
from judge import mem_only_evidence  # noqa: E402


def load_mod(name, path):
    spec = importlib.util.spec_from_file_location(name, str(path))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def set_r7(mod, on: bool):
    """构造"R7 前 / R7 后"两个世界（只翻转这一处守卫）。"""
    if on:
        mod.mem_only_evidence = judge.mem_only_evidence
    else:
        mod.mem_only_evidence = lambda sources: False


# ---------------- F4 族：main() 的组合（判据在 judge_case 之外） ----------------
def f4_pipeline(mod, r7):
    """逐字复刻 trng_test/tests/run_test.py main() 的判定组合：
        results, infos, reasons, notes, execution = evaluate_case(...)
        passed/ran 统计
        verdict = judge_case(results, passed, ran, execution, test_names)
        case_ok = verdict["ok"] and not mem_only          # ← R7 新增
    """
    def run(case, lines, mem):
        names = mod.TEST_NAMES
        results, infos, reasons, notes, execution = mod.evaluate_case(
            case, lines, names, mem)
        passed = sum(1 for n in names if results[n] == mod.PASS)
        skip = getattr(mod, "SKIP", "SKIP")
        ran = sum(1 for n in names if results[n] not in (mod.NOT_RUN, skip))
        verdict = mod.judge_case(results, passed, ran, execution, names)
        mem_only = judge.mem_only_evidence(notes.values()) if r7 else False
        return bool(verdict["ok"] and not mem_only), notes
    return run


# ---------------- D3 族：判据在 evaluate_case 内部 ----------------
def d3_pipeline(mod, r7):
    def run(case, ctrl_lines, tgt_lines, mem):
        set_r7(mod, r7)
        out = mod.evaluate_case(case, ctrl_lines, tgt_lines, mod.TEST_NAMES, mem)
        return out["result"] == "PASS", {t["name"]: t["src"] for t in out["tests"]}
    return run


def zt(name):
    return f"PASS - {name} in 0.10 seconds"


def mem_payload(names):
    return {"valid": True, "mem_state": "ok",
            "TGT": {n: "PASS" for n in names}, "CTRL": {n: "PASS" for n in names},
            "reasons": {}}


def main():
    ok = True
    # ============ Part A：四条基准场景 ============
    print("=" * 78)
    print("Part A  四条基准场景（新规则 vs 旧规则，须零翻转）")
    print("=" * 78)
    rows = []

    # --- F4（单核 ztest；代表工程 trng_test）---
    trng = load_mod("trng_rt", ROOT / "projects/trng_test/tests/run_test.py")
    names = trng.TEST_NAMES
    base = ["ZT_BOOT TGT", "CPU1 TRNG test banner"]
    scen_f4 = {
        "normal_all_pass": (base + [zt(n) for n in names]
                            + ["PROJECT EXECUTION SUCCESSFUL"], None),
        "trunc_all_pass": (base + [zt(n) for n in names], None),
        "trunc_missing_mem_ok": (
            base + [zt(n) for n in names[3:]],                       # 前 3 项 token 丢
            mem_payload(names[:3])),                                 # mem 补齐
    }
    old = f4_pipeline(trng, r7=False)
    new = f4_pipeline(trng, r7=True)
    for sname, (lines, mem) in scen_f4.items():
        a, notes_a = old("case_default", lines, mem)
        b, notes_b = new("case_default", lines, mem)
        rows.append(("trng_test(F4)", sname, a, b))
    # 预期翻转：全项 mem（单核形态 = 串口零 token）
    allmem = (base + ["PROJECT EXECUTION SUCCESSFUL"], mem_payload(names))
    a, _ = old("case_default", *allmem)
    b, _ = new("case_default", *allmem)
    rows.append(("trng_test(F4)", "**全项 mem（预期翻转）**", a, b))

    # --- D3（双核 ztest；代表工程 i3c_i2c_switch_test）---
    i3s = load_mod("i3s_rt", ROOT / "projects/i3c_i2c_switch_test/tests/run_test.py")
    dn = i3s.TEST_NAMES
    cb = ["ZT_BOOT CTRL", "CPU2 I3C_I2C_CTRL banner"]
    tb = ["ZT_BOOT TGT", "CPU1 I3C_I2C_TGT banner"]
    per_item = {
        "normal_all_pass": (cb + [zt(n) for n in dn] + ["PROJECT EXECUTION SUCCESSFUL"],
                            tb + [zt(n) for n in dn] + ["PROJECT EXECUTION SUCCESSFUL"], None),
        "trunc_all_pass": (cb + [zt(n) for n in dn],
                           tb + [zt(n) for n in dn], None),
        "trunc_missing_mem_ok": (cb + [zt(n) for n in dn[3:]],
                                 tb + [zt(n) for n in dn[3:]],
                                 mem_payload(dn[:3])),
        # TGT 整核丢行、mem 兜底补齐（R3 明确保留的合法路径）—— R7 不得翻转
        "tgt_all_missing_mem_ok": (cb + [zt(n) for n in dn]
                                   + ["PROJECT EXECUTION SUCCESSFUL"],
                                   tb + ["PROJECT EXECUTION SUCCESSFUL"],
                                   mem_payload(dn)),
    }
    oldd = d3_pipeline(i3s, r7=False)
    newd = d3_pipeline(i3s, r7=True)
    for sname, (c, t, mem) in per_item.items():
        a, _ = oldd("case_switch_core", c, t, mem)
        b, _ = newd("case_switch_core", c, t, mem)
        rows.append(("i3c_i2c_switch(D3)", sname, a, b))
    # 预期翻转：两核零 token（O-13 形态）
    bothmem = (cb + ["PROJECT EXECUTION SUCCESSFUL"],
               tb + ["PROJECT EXECUTION SUCCESSFUL"], mem_payload(dn))
    a, _ = oldd("case_switch_core", *bothmem)
    b, _ = newd("case_switch_core", *bothmem)
    rows.append(("i3c_i2c_switch(D3)", "**两核零 token（预期翻转）**", a, b))

    for fam, sname, a, b in rows:
        flip = "  <<< 翻转" if a != b else ""
        exp = "预期" if "预期翻转" in sname else "须零翻转"
        status = "OK" if ((a != b) == ("预期翻转" in sname)) else "!! 违反"
        if status != "OK":
            ok = False
        print(f"  [{status}] {fam:<22} {sname:<28} old={a!s:<5} new={b!s:<5} ({exp}){flip}")

    # ============ Part B：归档报告重放 ============
    print()
    print("=" * 78)
    print("Part B  归档报告重放（全 case 零 token 才翻转）")
    print("=" * 78)
    arch = json.loads((ROOT / "scripts/archive_sources.json").read_text(encoding="utf-8"))
    flips, already_fail = [], []
    n_tok_cases = 0
    for r in arch:
        srcs = [i["src"] for i in r["items"]]
        # 归档里 Z 族/mbox 无「来源」列（nosrc）= 结构上无 mem 通道 ⇒ 不可能 mem-only
        if not srcs or all(s == "nosrc" for s in srcs):
            continue
        memonly = mem_only_evidence(srcs)     # nosrc 不含 "token" ⇒ 需排除
        if any(s == "nosrc" for s in srcs):
            continue
        if not memonly:
            if any(s == "token" for s in srcs):
                n_tok_cases += 1
            continue
        if r["case_result"] == "PASS":
            flips.append(r)
        else:
            already_fail.append(r)
    print(f"  有 token 证据的归档 case: {n_tok_cases}")
    print(f"  已是 FAIL 且零 token 的 case（不构成翻转）: {len(already_fail)}")
    for r in already_fail:
        print(f"     FAIL  {r['report']}")
    print(f"  ★ PASS→FAIL 翻转数（应为 {len(flips)}，且全部是 O-13 那个工程）: {len(flips)}")
    for r in flips:
        print(f"     {r['report']}")
    bad = [r for r in flips if "i3c_i2c_switch_test" not in r["report"]]
    if bad:
        ok = False
        print("  !! 出现非预期翻转:", [r["report"] for r in bad])

    # ============ Part C：调试路径不受影响 ============
    print()
    print("=" * 78)
    print("Part C  dbg_run.py（调试路径）不受影响")
    print("=" * 78)
    dbgs = sorted(ROOT.glob("projects/*/tests/scripts/dbg_run.py"))
    hit = []
    for d in dbgs:
        t = d.read_text(encoding="utf-8", errors="replace")
        if "mem_only_evidence" in t or "MEM_ONLY_NOTE" in t:
            hit.append(d)
    print(f"  dbg_run.py 数量: {len(dbgs)}；引用 mem_only 守卫的: {len(hit)} {hit}")
    if hit:
        ok = False
    print("  → 守卫只挂在 run_test.py（批量判定链）上，dbg_run.py 既不 import judge "
          "也不调用它 ⇒ 调试路径的 mem 兜底行为逐字不变。")
    print()
    print("REPLAY:", "ALL OK" if ok else "**有不符**")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
