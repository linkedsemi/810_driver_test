#!/usr/bin/env python3
"""B1 证据：`regression/replay.py` 的"硬件层隔离网"真的是绊线（不是装饰）。

三段自检：

  A. **逐策略**：本策略的采集入口必须已换成"喂归档行"的替身（调用不抛
     `HardwareTouchAttempt`）；其余 4 个采集入口必须仍是绊线。
  B. **全局**（single_cap 策略下取样）：烧录 / 起 gdb / 起 test.sh / 开串口 /
     构建 / pyserial 全部必须抛 `HardwareTouchAttempt`。
  C. **合法但无害**入口（清残留 / 起 server / 读内存 / 找 bash）必须静默返回。

任一条不符合 ⇒ 打印 `<<< BAD` 并 FAIL。

⚠️ 2026-09-24 由 `tmp/b1/guard_demo.py` 迁来：本脚本被耐久文档当**必做验证**点名，
   却住在会被清理的 `tmp/` 里 —— 一旦清理就**静默失去该证据脚本**。
   迁移时只改了一处：根目录由 `parents[2]`（`tmp/<子目录>/`）改为 `parents[1]`（`scripts/`）。
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]      # scripts/ 的上一级 = 810_driver_test
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "common"))
sys.path.insert(0, str(ROOT / "regression"))
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

import replay                                          # noqa: E402
import acquire                                         # noqa: E402

STRATEGY = {
    "single_cap": ("adc_temper_test", "run_case_shell"),
    "ports_dict": ("rtc_ls_test", "capture_case"),
    "dual_run": ("i3c_dualcore_test", "run_case_dual"),
    "mbox": ("mbox_test", "run_case_mbox"),
    "iwdg": ("app_iwdg_cpu2_test", "run_pass_iwdg"),
}
ALL_ENTRIES = ["run_case_shell", "run_case_dual", "run_case_mbox",
               "run_pass_iwdg", "capture_case"]
CALL = {
    "run_case_shell": lambda: acquire.run_case_shell(*([None] * 5)),
    "run_case_dual": lambda: acquire.run_case_dual(*([None] * 3)),
    "run_case_mbox": lambda: acquire.run_case_mbox(*([None] * 8)),
    "run_pass_iwdg": lambda: acquire.run_pass_iwdg(*([None] * 6)),
    "capture_case": lambda: acquire.capture_case(*([None] * 4)),
}
FORBIDDEN = [
    ("west_build（构建）", lambda: acquire.west_build(None, None)),
    ("build_via_test_sh（构建）", lambda: acquire.build_via_test_sh(None, None, None)),
    ("gdb_flash_once（烧录）", lambda: acquire.gdb_flash_once("x", 1)),
    ("gdb_watch_session（烧录+采集）", lambda: acquire.gdb_watch_session("x", 1)),
    ("spawn_test_run（起 test.sh）", lambda: acquire.spawn_test_run(None, None, None)),
    ("kill_proc_group（杀进程组）", lambda: acquire.kill_proc_group(None)),
    ("_serial（pyserial）", lambda: acquire._serial()),
]

bad = 0
print("== A. 逐策略：本策略入口已替换 / 其余采集入口仍是绊线 ==")
for strat, (project, own) in STRATEGY.items():
    getattr(replay, replay.ADAPTERS[project].__name__)(None, {}, {})
    row = []
    for e in ALL_ENTRIES:
        try:
            CALL[e]()
            ok = (e == own)
        except replay.HardwareTouchAttempt:
            ok = (e != own)
        except TypeError:
            ok = (e == own)
        if not ok:
            bad += 1
        row.append(f"{e}={'替身' if e == own else '绊线' if ok else 'BAD'}")
    print(f"  {strat:<11} {'  '.join(row)}")
    if strat == "iwdg":
        try:
            cap = acquire.SerialCapture("COM_NOT_OURS")
            print(f"    （iwdg）SerialCapture 已被替换为替身，非本工程端口 -> lines={cap.lines}")
        except replay.HardwareTouchAttempt:
            print("    （iwdg）SerialCapture 未被替换 <<< BAD")
            bad += 1

print("\n== B. 全局硬件入口必须抛 HardwareTouchAttempt（single_cap 策略下取样）==")
replay.apply_single_cap(None, {}, {})
for name, call in FORBIDDEN + [("SerialCapture（开串口）",
                                lambda: acquire.SerialCapture("COM16"))]:
    try:
        call()
        print(f"  {name}: 未拦截 <<< BAD")
        bad += 1
    except replay.HardwareTouchAttempt:
        print(f"  {name}: 仍是绊线（已拦截）")

print("\n== C. 合法但无害入口必须静默（不得有硬件副作用）==")
for name, call in (("clean_gdb_residue", lambda: acquire.clean_gdb_residue()),
                   ("preflight_gdb", lambda: acquire.preflight_gdb()),
                   ("restart_debugserver_stable", lambda: acquire.restart_debugserver_stable()),
                   ("kill_residual_gdb", lambda: acquire.kill_residual_gdb()),
                   ("memory_backfill", lambda: acquire.memory_backfill("c")),
                   ("dump_mem", lambda: acquire.dump_mem("c", "d")),
                   ("find_git_bash", lambda: acquire.find_git_bash()),
                   ("find_tgt_uart", lambda: acquire.find_tgt_uart())):
    print(f"  {name}() -> {call()!r}")

# ★ O-40（2026-09-24 补，本自检原先**完全不测**这一条）：`ensure_debugserver_alive`
#   的替身**必须返回 `(True, msg)`**。真身会 netstat 1025 / taskkill / Popen
#   DebugServer —— 漏收它 ⇒ PORTS_DICT 族 11 个工程的离线重放真碰硬件。
#   返回 None（通用 `_noop`）⇒ 调用方解包时 `TypeError`；返回 `ok=False` ⇒
#   每个 case 走 fail-closed 分支变成 "CAPTURE FAILED"。故连签名一起断言。
_ensure = acquire.ensure_debugserver_alive(style="d3")
if isinstance(_ensure, tuple) and len(_ensure) == 2 and _ensure[0] is True:
    print(f"  ensure_debugserver_alive() -> {_ensure!r}  [签名/语义正确]")
else:
    print(f"  ensure_debugserver_alive() -> {_ensure!r}  <<< BAD（应为 (True, msg)）")
    bad += 1

print(f"\n=== 隔离网自检：{'PASS' if bad == 0 else f'FAIL（{bad} 处）'} ===")
sys.exit(1 if bad else 0)
