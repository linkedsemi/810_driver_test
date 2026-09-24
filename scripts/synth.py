#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""B（R8）合成输入矩阵：**每个判定族 × 六类证据组合**，逐条给出观测签名。

组合覆盖（任务 §3.2）：
    全 token / 全 mem / token+mem 混合 / mem 但有 ZT_BOOT / mem 且无 ZT_BOOT（E6 门）
    / mem_state != ok（R2 门，dumper 侧已清空 TGT）× 单核 / 双核

★ 非空转自检（任务 §3.2 硬要求）：合成输入**必须真的走到判定分支** ——
  由 `ab.py` Part 3 用"同一工程各变体的观测签名互不相同"来证（先例：
  `tmp/a1/coverage.py`）。

⚠️ 2026-09-24 随 `tmp/brule/ab.py` 一起由 `tmp/brule/synth.py` 迁来（见 ab.py 文件头）。
   迁移时只改了一处：根推导 `parents[2]`（`tmp/<子目录>/`）→ `parents[1]`（`scripts/`）。
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]      # scripts/ 的上一级 = 810_driver_test
sys.path.insert(0, str(ROOT / "common"))

import judge  # noqa: E402


def zt(name, st="PASS"):
    return f"{st} - {name} in 0.10 seconds"


def zt_lines(names, st="PASS"):
    return [zt(n, st) for n in names]


def mem_ok(values_by_core, reasons=None):
    return {"valid": True, "mem_state": "ok",
            "TGT": values_by_core.get("TGT", {}),
            "CTRL": values_by_core.get("CTRL", {}),
            "reasons": reasons or {}}


def mem_bad(state="invalid", note="哨兵未置位"):
    """R2 哨兵门：非 ok ⇒ dumper 侧已把 TGT 清空（此处忠实复刻）。"""
    return {"valid": False, "mem_state": state, "note": note,
            "TGT": {}, "CTRL": {}, "reasons": {}}


def names_of(project, case):
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        f"_synth_{project}", str(ROOT / "projects" / project / "tests" / "run_test.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    tn = getattr(m, "TEST_NAMES_BY_CASE", None)
    if tn is not None:
        return list(tn.get(case) or next(iter(tn.values())))
    names = getattr(m, "TEST_NAMES", None)
    if names:
        return [str(n) for n in names]
    # Z 族：真源 = 同工程 `scripts/dump_results.py`（与 run_test.py 同一取法）
    return [str(n) for n in judge.declared_names_from_dumper(
        ROOT / "projects" / project / "tests" / "scripts" / "dump_results.py", case)]


# --------------------------------------------------------------- 单核 ztest（F4A/F4B）
def single_builders(project, case, banner, done_line, names, item_line):
    """单核族：tokens 走 item_line(name)；收尾标记 done_line。"""
    base = [banner, "ZT_BOOT TGT"]

    def tokens(subset=None, st="PASS", boot=True):
        ns = names if subset is None else subset
        head = [banner] + (["ZT_BOOT TGT"] if boot else [])
        return head + [item_line(n, st) for n in ns]

    mem_all = mem_ok({"TGT": {n: "PASS" for n in names}})
    mem_first3 = mem_ok({"TGT": {n: "PASS" for n in names[:3]}})
    mem_last3 = mem_ok({"TGT": {n: "PASS" for n in names[-3:]}})
    mem_fail1 = mem_ok({"TGT": {n: "PASS" for n in names[:2]} | {names[2]: "FAIL"}})

    v = {}
    v["alltoken"] = (case, {"TGT": tokens() + [done_line]}, None)
    v["allmem"] = (case, {"TGT": base + [done_line]}, mem_all)
    v["mixed_first3_mem"] = (
        case, {"TGT": tokens(names[3:]) + [done_line]}, mem_first3)
    v["mixed_last3_mem"] = (
        case, {"TGT": tokens(names[:-3]) + [done_line]}, mem_last3)
    v["mixed_memfail"] = (
        case, {"TGT": tokens(names[3:]) + [done_line]}, mem_fail1)
    v["mixed_truncated"] = (case, {"TGT": tokens(names[3:])}, mem_first3)
    # E6 门（只有 eth 消费 ZT_BOOT）：本变体**故意去掉 ZT_BOOT**；
    # 对 F4A/F4B 而言 ZT_BOOT 不参与判定 ⇒ 与 mixed_first3_mem 观测必然相同
    # （这本身就是"R8 没有引入新依赖"的正面证据）。
    v["mem_noboot"] = (case, {"TGT": tokens(names[3:], boot=False) + [done_line]},
                       mem_first3)
    v["memstate_bad"] = (case, {"TGT": tokens(names[3:]) + [done_line]}, mem_bad())
    v["token_fail1"] = (
        case, {"TGT": tokens(names[1:]) + [zt(names[0], "FAIL")] + [done_line]}, None)
    return v


def build_all():
    """返回 {project: {变体名: (case, {核: [行]}, mem 载荷)}}。"""
    out = {}

    # --- F4A：trng_test（15 项，单核） ---
    n = names_of("trng_test", "case_default")
    out["trng_test"] = single_builders(
        "trng_test", "case_default", "CPU1 TRNG test banner",
        "PROJECT EXECUTION SUCCESSFUL", n, zt)

    # --- F4B：rtc_ls_test（自定义 token + DONE 收尾） ---
    n = names_of("rtc_ls_test", "case_default")
    out["rtc_ls_test"] = single_builders(
        "rtc_ls_test", "case_default", "CPU1 RTC test banner",
        "RTC_TEST:DONE:PASS", n,
        lambda name, st="PASS": f"RTC_TEST:TGT:{st}:{name}")

    # --- eth_test（单核 ztest + E6 独立性门） ---
    n = names_of("eth_test", "case_mdio")
    out["eth_test"] = single_builders(
        "eth_test", "case_mdio", "TGT eth banner",
        "PROJECT EXECUTION SUCCESSFUL", n, zt)

    # --- D3：i3c_i2c_switch_test（双核原生 token） ---
    n = names_of("i3c_i2c_switch_test", "case_switch_core")
    out["i3c_i2c_switch_test"] = d3_builders(
        "i3c_i2c_switch_test", "case_switch_core", n, "I3C_I2C_TEST")

    # --- D3：i3c_dualcore_test（双核；含 incomplete 容忍路径） ---
    n = names_of("i3c_dualcore_test", "case_default")
    out["i3c_dualcore_test"] = d3_builders(
        "i3c_dualcore_test", "case_default", n, "I3C_TEST")

    # --- Z 族：dma_test（**无 mem 通道**，用于反证 R8 对其不可达） ---
    n = names_of("dma_test", "case_default")
    out["dma_test"] = {
        "alltoken": ("case_default",
                     {"TGT": ["TGT dma banner", "ZT_BOOT TGT"] + zt_lines(n)
                      + ["PROJECT EXECUTION SUCCESSFUL"]}, None),
        # 即便人为塞一个全 PASS 的 mem 载荷，本族也不读它（结构上无 mem 通道）
        "mem_payload_ignored": ("case_default",
                                {"TGT": ["TGT dma banner", "ZT_BOOT TGT"]},
                                mem_ok({"TGT": {x: "PASS" for x in n}})),
    }
    return out


def d3_builders(project, case, names, prefix):
    """双核原生 token 族：每核各自一套 token；收尾标记两核都打。"""
    NB = "PROJECT EXECUTION SUCCESSFUL"

    def core_lines(core, subset=None, st="PASS"):
        ns = names if subset is None else subset
        return ([f"{prefix}:{core}:{st}:{k}" for k in ns])

    def full(ctrl_sub=None, tgt_sub=None, ctrl_st="PASS", tgt_st="PASS"):
        return {"CTRL": core_lines("CTRL", ctrl_sub, ctrl_st) + [NB],
                "TGT": core_lines("TGT", tgt_sub, tgt_st) + [NB]}

    mem_tgt3 = mem_ok({"TGT": {k: "PASS" for k in names[:3]},
                       "CTRL": {k: "PASS" for k in names[:3]}})
    mem_tgt_notrun = mem_ok({"TGT": {k: "NOT_RUN" for k in names},
                             "CTRL": {k: "PASS" for k in names}})
    mem_last3 = mem_ok({"TGT": {k: "PASS" for k in names[-3:]},
                        "CTRL": {k: "PASS" for k in names[-3:]}})
    v = {}
    v["alltoken"] = (case, full(), None)
    v["allmem"] = (case, {"CTRL": [NB], "TGT": [NB]},
                   mem_ok({"TGT": {k: "PASS" for k in names},
                           "CTRL": {k: "PASS" for k in names}}))
    v["mixed_tgt3_mem"] = (case, full(tgt_sub=names[3:]), mem_tgt3)
    v["mixed_both3_mem"] = (case, full(ctrl_sub=names[3:], tgt_sub=names[3:]),
                            mem_tgt3)
    v["mixed_last3_mem"] = (case, full(tgt_sub=names[:-3]), mem_last3)
    v["ctrl_fail_tgt_mem"] = (
        case,
        {"CTRL": [f"{prefix}:CTRL:PASS:{k}" for k in names[1:]]
         + [f"{prefix}:CTRL:FAIL:{names[0]}:why"] + [NB],
         "TGT": [f"{prefix}:TGT:PASS:{k}" for k in names[1:]] + [NB]},
        mem_ok({"TGT": {names[0]: "PASS"}, "CTRL": {names[0]: "PASS"}}))
    v["tgt_all_mem_notrun"] = (
        case,
        {"CTRL": [f"{prefix}:CTRL:PASS:{k}" for k in names] + [NB], "TGT": [NB]},
        mem_tgt_notrun)
    v["mixed_truncated"] = (case,
                            {"CTRL": core_lines("CTRL", names[3:])
                             + [f"{prefix}:CTRL:INFO:{names[0]}:k=v"],
                             "TGT": core_lines("TGT", names[3:])},
                            mem_tgt3)
    v["memstate_bad"] = (case, full(tgt_sub=names[3:]), mem_bad())
    return v


if __name__ == "__main__":
    import json
    allv = build_all()
    for p, vs in sorted(allv.items()):
        print(f"{p}: {len(vs)} 变体 -> {sorted(vs)}")
