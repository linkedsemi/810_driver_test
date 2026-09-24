#!/usr/bin/env python3
"""B1 审计：逐个策略核对"补丁目标是否都落在 `acquire` 模块的既有全局名上"。

对每个策略：
  1. 重置 `acquire`（reload）拿一份原始命名空间；
  2. 跑该策略的适配器；
  3. 列出**值发生变化**的 `acquire` 属性，并断言每个名字在步骤 1 里**已存在**
     —— 证明"补丁打在 acquire 的真正全局名上"，而不是凭空挂了一个新属性
     （凭空挂属性 = 内部调用点走的是别处 = 补丁打空 = 会落真硬件）。
另外打印该策略下 `acquire` 的采集入口里"被换成替身"的名字，便于逐策略勾对。

⚠️ 2026-09-24 由 `tmp/b1/audit_patch_targets.py` 迁来：本脚本被耐久文档当**必做验证**
   点名，却住在会被清理的 `tmp/` 里 —— 一旦清理就**静默失去该证据脚本**。
   迁移时只改了一处：根目录由 `parents[2]`（`tmp/<子目录>/`）改为 `parents[1]`（`scripts/`）。
"""
import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]        # scripts/ 的上一级 = 810_driver_test
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "common"))
sys.path.insert(0, str(ROOT / "regression"))
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

import acquire                                       # noqa: E402
import replay                                        # noqa: E402

# 每条：策略名 → (代表工程, 该策略**必须**被替换掉的采集入口)
STRATEGY = {
    "single_cap": ("adc_temper_test", ["run_case_shell"]),
    "ports_dict": ("rtc_ls_test", ["capture_case"]),
    "dual_run": ("i3c_dualcore_test", ["run_case_dual"]),
    "mbox": ("mbox_test", ["run_case_mbox"]),
    "iwdg": ("app_iwdg_cpu2_test", ["SerialCapture", "run_pass_iwdg"]),
}

MOD = type("M", (), {})()                            # 占位 mod（适配器已不用它打补丁）
bad = 0
for name, (project, must) in STRATEGY.items():
    acq = importlib.reload(acquire)
    before = dict(vars(acq))
    replay.apply_dispatch = getattr(replay, replay.ADAPTERS[project].__name__)
    replay.apply_dispatch(MOD, {}, {})
    after = vars(acq)
    changed = sorted(k for k in after if after[k] is not before.get(k))
    phantom = [k for k in changed if k not in before]
    print(f"\n=== {name}（{project}）===")
    print(f"  被替换的 acquire 全局名（{len(changed)}）: {', '.join(changed)}")
    print(f"  其中**原本不存在**的（补丁打空风险）: {phantom or '无'}")
    missing = [m for m in must if m not in changed]
    print(f"  策略必需入口 {must} 已替换: {'是' if not missing else '否 -> ' + str(missing)}")
    bad += len(phantom) + len(missing)
print(f"\n=== 审计结论：{'PASS（补丁全部命中 acquire 既有全局名）' if bad == 0 else f'FAIL（{bad} 处）'} ===")
