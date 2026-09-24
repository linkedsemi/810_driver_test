#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""B（R8）离线 A/B 线束：同一份「串口行 + mem 载荷」喂给**改动前 / 改动后**两份
`run_test.py`，逐字节比对**判定结果 + 渲染产物**。

为什么要自建（而不是直接用 `regression/replay.py`）：
  * `replay.py` 只能加载**当前**源码；B 的对照物是快照 `tmp/brule/before/`；
  * 本改动的两半分别在 `common/judge.py`（新增 R8 原语）与
    `projects/*/tests/run_test.py`（各族接线），所以 A/B 必须能**同时**换掉
    被 import 的 `judge` 模块 —— 这也是本文件与 `tmp/a1/harness.py` 的唯一差别；
  * 判定链的**唯一入口**仍是各工程自己的 `l2_spec().judge` + `common/process.emit`
    （不重写判定/渲染一行）。

★ mem 载荷是 L1 侧产物、**不进 L0 归档**（`common/process.py` 的 D7 缺口）⇒
  纯重放恒为 `mem=None`，mem 分支根本走不到。故本线束显式接受 `mem` 参数
  （真归档 A/B 用它喂"从归档报告反推出来的 mem 载荷"，见 `ab.py` Part 2）。
"""
import importlib.util
import inspect
import json
import sys
import types
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]       # scripts/ 的上一级 = 810_driver_test
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "common"))
import process                                   # noqa: E402
import regression.replay as replay               # noqa: E402

# ⚠️ 2026-09-24 随 `tmp/brule/ab.py` 一起由 `tmp/brule/harness.py` 迁来（见 ab.py 文件头）。
#    "改动前"快照随之迁到 `scripts/before/`；根推导 `parents[2]` → `parents[1]`。
BEFORE_ROOT = ROOT / "scripts" / "before"
FIXED = datetime(2026, 9, 21, 12, 0, 0)


def load_judge(tree):
    """加载对应版本的 `common/judge.py` 作为模块 `judge`。"""
    src = (ROOT / "common" / "judge.py") if tree == "after" else (BEFORE_ROOT / "judge.py")
    spec = importlib.util.spec_from_file_location("judge", str(src))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def load(project, tree, freeze=True):
    """加载工程 `run_test.py`：`after` = 真实路径；`before` = 快照源码 + 伪装 `__file__`。

    `__file__` 必须指向**真实工程路径**，否则 `SAMPLE_ROOT` / `REPORTS_ROOT` /
    `DECLARED_DUMPER` 全部错位（declared 表读不到 ⇒ 判定会变，A/B 失去意义）。
    """
    real = ROOT / "projects" / project / "tests" / "run_test.py"
    src = real if tree == "after" else (BEFORE_ROOT / project / "run_test.py")
    name = f"_b8_{tree}_{project}"
    mod = types.ModuleType(name)
    mod.__file__ = str(real)
    sys.modules[name] = mod
    sys.modules["judge"] = load_judge(tree)
    sys.modules.pop("dump_results", None)
    code = compile(src.read_text(encoding="utf-8"), str(real), "exec")
    exec(code, mod.__dict__)
    sys.modules.pop("dump_results", None)
    if freeze:
        replay.freeze_datetime(mod, FIXED)
    return mod


def make_spec(mod):
    """按 `l2_spec` 的实参个数选调用形（双核族多一个 `test_names`）。"""
    params = list(inspect.signature(mod.l2_spec).parameters)
    facts = {"tgt_uart": "REPLAY_TGT", "ctrl_uart": "REPLAY_CTRL",
             "timeout": 30.0, "case_timeout": 120}
    return mod.l2_spec(facts, None) if len(params) >= 2 else mod.l2_spec(facts)


def judge(project, tree, case, lines, mem):
    mod = load(project, tree)
    spec = make_spec(mod)
    return spec, spec.judge(process.L2Input(case=case, lines=lines, mem=mem))


def run_emit(project, tree, case, lines, mem, out_root):
    """判定 + 渲染 + 落盘（走 `common/process.emit`，与在线路径同一接线）。"""
    spec, judged = judge(project, tree, case, lines, mem)
    session = Path(out_root) / tree
    session.mkdir(parents=True, exist_ok=True)
    cdir, jp, mp = process.emit(spec, [judged], session, case_label=case)
    return judged, cdir, jp, mp


# ------------------------------------------------------------------ 观测面
def sig(judged):
    """把判定结果压成**族无关的观测签名**（用来看"有没有真的走到判定分支"）。"""
    res = judged.get("result") or judged.get("overall")
    if res is None:
        # F4 族 / rtc_ls 的 `_judge` 返回的是 `case_ok` 而非 `result`
        if "case_ok" in judged:
            res = "PASS" if judged["case_ok"] else "FAIL"
        elif isinstance(judged.get("verdict"), dict):
            res = "PASS" if judged["verdict"].get("ok") else "FAIL"
    out = {"result": res}
    # 收尾标记 / 截断标签也进签名 —— 否则 `mixed_truncated` 与 `mixed_first3_mem`
    # 在"改动前"会给出同一观测，看不出"截断接受"这条分支有没有被走到。
    for k in ("execution", "exec_report", "overall", "ctrl_done", "tgt_done"):
        v = judged.get(k)
        if isinstance(v, str):
            out[k] = v

    def item_sig(it):
        return (it.get("name"), it.get("result"), it.get("source") or it.get("src"))

    tests = judged.get("tests")
    if isinstance(tests, list):
        out["items"] = [item_sig(t) for t in tests]
    items = judged.get("items")
    if isinstance(items, list):
        out["items"] = [item_sig(t) for t in items]
    results = judged.get("results")
    if isinstance(results, dict):
        out["items"] = [(n, v, (judged.get("notes") or {}).get(n)) for n, v in results.items()]
    return out


def read_out(cdir, project):
    """读回渲染产物（.md 原文 + .json 解析）。"""
    md = sorted(Path(cdir).glob("*_report.md"))
    js = sorted(Path(cdir).glob("*_report.json"))
    return (md[0].read_bytes() if md else None,
            json.loads(js[0].read_text(encoding="utf-8")) if js else None,
            md[0] if md else None, js[0] if js else None)
