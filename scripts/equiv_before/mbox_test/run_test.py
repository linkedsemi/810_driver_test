#!/usr/bin/env python3
"""MBOX dual-core regression runner.

Mbox 测试不用 ztest，而是 cpu1/cpu2 各自运行 mbox_*_main() / mbox_cases_*_main()
后由固件按实际判定**择一**打印：
    PROJECT EXECUTION: SUCCESSFUL   或   PROJECT EXECUTION: FAILED

判定口径（2026-09-18 F3 修复）
-----------------------------
保留本工程原有的核心哲学：**不以 timed_out 判 FAIL**（RUN_TIMEOUT=0 下
gdb 永不退出，超时是常态，与测试成败无关）。补上此前缺失的两道门：

  1. **boot 门（§10.30）**：只有当端口的**本固件** ZT_BOOT 标记（cpu2 =
     `ZT_BOOT CTRL`、cpu1 = `ZT_BOOT TGT`）先出现时，后续标记才算数。
     他方残留镜像/上一轮输出没有本固件 ZT_BOOT ⇒ 其 PROJECT EXECUTION
     一律忽略，堵"任何来源的 SUCCESS 字样都算数"。
  2. **时序门**：`PROJECT EXECUTION` 必须出现在本端口 ZT_BOOT 行**之后**，
     即"load 完成后"才认。boot 之前的同名标记不当证据。

另加两条 fail-closed 守卫：
  - 每核恰应出现**一个** verdict 标记；同一端口上 SUCCESSFUL 与 FAILED
    并存时判 FAIL（修复前固件正是两个都打）。
  - 采集前清空串口输入缓冲（flush_stale），避免上一轮残留字节被当成
    本轮输出。

逐项结果（§1.3）：固件上报 `INFO <name>: pass=<0|1>[,<k=v>…]`，本脚本按
(name, 核) 收编成独立测试项；`observe=1` 的观察项与 `guard=1` 的结构性
守卫分别呈现，**不**计入"全项 PASS"（§1.4/§1.5）。

`<err>` 日志计数（ctrl_errs/tgt_errs）**仅作展示**，不参与判定 ——
异常参数 case 的预期拒绝路径本身就会产生 DEV_ERR（详见 COVERAGE.md §7）。

结果归档到 reports/<date>-<idx>/（report_dir 公共模块）。
"""
import argparse
import json
import os
import re
import subprocess
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import threading
import time
from datetime import datetime
from pathlib import Path

import serial
import serial.tools.list_ports as list_ports
import shutil

SAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"

sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
from report_dir import allocate_session_dir, case_dir_for, update_summary
from serial_archive import archive_serial  # noqa: E402
import process  # noqa: E402  —— L2 处理层（归档 → 判定 → 报告）

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST = os.environ.get("GDB_SERVER_HOST", "127.0.0.1")
GDB_PORT = os.environ.get("GDB_SERVER_PORT", "1025")
DRIVER_NAME = "mbox_test"
REPORT_BASE = f"{DRIVER_NAME}_report"

SUCCESS_MARKER = "PROJECT EXECUTION: SUCCESSFUL"
FAIL_MARKER = "PROJECT EXECUTION: FAILED"

# 本固件 BOOT 标记（ztest_glue.zt_init 的 printk）。boot 门 + 时序门都靠它。
BOOT_MARKER = {"CTRL": "ZT_BOOT CTRL", "TGT": "ZT_BOOT TGT"}

_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
# §1.3 逐项上报格式；用 search 不锚点（KNOWN_ISSUES.md #5：ANSI 前缀坑）
_ITEM_RE = re.compile(r"INFO\s+([A-Za-z0-9_]+):\s*pass=([01])(.*)")


def log(msg):
    print(msg, flush=True)


def _find_git_bash():
    # Prefer bin/bash.exe (not usr/bin which is MSYS2 alternate)
    candidates = [
        os.environ.get("GIT_BASH"),
        r"C:\Program Files\Git\bin\bash.exe",
        r"C:\Program Files (x86)\Git\bin\bash.exe",
    ]
    for c in candidates:
        if c and Path(c).exists():
            low = c.lower()
            if "system32" in low or "windowsapps" in low:
                continue
            return c
    # Fallback: any bash but not usr/bin
    p = shutil.which("bash")
    if p:
        return p
    return "bash"


Bash = _find_git_bash()
TESTS_DIR = SAMPLE_ROOT / "tests"


class SerialCapture:
    """Capture serial output in a background thread."""

    def __init__(self, port, baud=115200):
        self.port = port
        self.baud = baud
        self.lines = []
        self._stop = threading.Event()
        self._thread = None
        self._ser = None
        self.error = None

    def start(self):
        try:
            self._ser = serial.Serial(self.port, self.baud, timeout=0.1)
        except Exception as exc:
            self.error = f"failed to open {self.port}: {exc}"
            return
        # 时序门前置：丢掉本次采集开始前已积压在驱动缓冲里的字节，
        # 避免上一轮固件的收尾输出被当成本轮结果。
        try:
            self._ser.reset_input_buffer()
        except Exception:
            pass
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        while not self._stop.is_set():
            try:
                data = self._ser.read(4096)
            except Exception:
                break
            if data:
                self.lines.append((time.time(), data.decode("utf-8", errors="replace")))

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2.0)
        if self._ser and self._ser.is_open:
            self._ser.close()

    def flush_stale(self, duration=0.3):
        """Drop bytes already buffered before a capture window starts."""
        if self._ser is None or not self._ser.is_open:
            return
        deadline = time.time() + duration
        while time.time() < deadline:
            try:
                if self._ser.in_waiting:
                    self._ser.reset_input_buffer()
            except Exception:
                pass
            time.sleep(0.05)

    @property
    def text(self):
        return "\n".join(l for _, l in self.lines)


def _split_lines(capture_lines):
    """把 (时刻, 原始字节块) 序列还原成逐行文本。

    串口 read() 返回的是**字节块**不是行：一次 read 可能含多行，一行也可能
    跨两次 read。故必须先把所有块按原序**拼接**再按 '\\n' 切 —— 直接对每个块
    单独切会把跨块的半行拆成两行，逐行判据（boot 门/时序门/逐项 INFO）就会
    误判。时间戳不参与判定，此处丢弃。
    """
    text = "".join(chunk for _, chunk in capture_lines)
    return [_ANSI_RE.sub("", ln).strip("\r") for ln in text.split("\n")]


def _core_view(capture_lines, core):
    """单核视图：boot 门 + 时序门 + 逐项 INFO 解析。

    boot 门：端口必须见过本固件 BOOT 标记（BOOT_MARKER[core]）。
    时序门：只采信 BOOT 行**之后**的 verdict 标记与逐项 INFO。
    """
    lines = _split_lines(capture_lines)
    view = {
        "core": core,
        "boot": False,
        "verdict": None,          # "SUCCESSFUL" / "FAILED" / None
        "success_lines": 0,
        "failed_lines": 0,
        "items": [],
        "err_logs": sum(1 for ln in lines if "<err>" in ln.lower()),
    }

    boot_idx = None
    for i, ln in enumerate(lines):
        if BOOT_MARKER[core] in ln:
            boot_idx = i
            break
    if boot_idx is None:
        return view
    view["boot"] = True

    seen = set()
    for ln in lines[boot_idx:]:
        if SUCCESS_MARKER in ln:
            view["success_lines"] += 1
            if view["verdict"] is None:
                view["verdict"] = "SUCCESSFUL"
        if FAIL_MARKER in ln:
            view["failed_lines"] += 1
            # fail-closed：同端口 SUCCESSFUL 与 FAILED 并存时判 FAIL
            view["verdict"] = "FAILED"
        m = _ITEM_RE.search(ln)
        if not m:
            continue
        name, ok, kv = m.group(1), m.group(2) == "1", m.group(3)
        if name in seen:
            continue
        seen.add(name)
        observe = "observe=1" in kv
        guard = "guard=1" in kv
        detail = kv.lstrip(",")
        detail = detail.replace("observe=1", "").replace("guard=1", "")
        detail = detail.strip(",").strip()
        if guard:
            kind, result = "guard", "GUARD"
        elif observe:
            kind, result = "observe", ("OBS" if ok else "OBS_DIFF")
        else:
            kind, result = "behavior", ("PASS" if ok else "FAIL")
        view["items"].append({
            "name": name,
            "core": core,
            "kind": kind,
            "ok": ok,
            "result": result,
            "detail": detail,
        })
    return view


def _combined_lines(capture_lines):
    """窗口内所有字节（用于"gdb 是否还在跑/有无任何固件输出"的粗判）。"""
    return "\n".join(chunk for _, chunk in capture_lines)


def run_case(case_name, ctrl_uart, tgt_uart, timeout=120):
    """Run mbox dual-core test and capture both UARTs."""
    build_dir = TESTS_DIR / case_name / "build"
    target_elf = build_dir / "target" / "zephyr" / "zephyr.elf"
    controller_bin = build_dir / "controller" / "zephyr" / "zephyr.bin"

    if not target_elf.exists():
        log(f"[ERROR] Target ELF not found: {target_elf}")
        return None
    if not controller_bin.exists():
        log(f"[ERROR] Controller BIN not found: {controller_bin}")
        return None

    ctrl_cap = SerialCapture(ctrl_uart)
    tgt_cap = SerialCapture(tgt_uart)
    ctrl_cap.start()
    tgt_cap.start()

    if ctrl_cap.error:
        log(f"[ERROR] {ctrl_cap.error}")
        tgt_cap.stop()
        return None
    if tgt_cap.error:
        log(f"[ERROR] {tgt_cap.error}")
        ctrl_cap.stop()
        return None

    ctrl_cap.flush_stale()
    tgt_cap.flush_stale()

    # Run test.sh -r (stdout=DEVNULL, serial capture is the real output)
    test_sh = TESTS_DIR / case_name / "test.sh"
    env = dict(os.environ)
    env["SKIP_LOAD"] = "y"
    env["AGENT_RUN"] = "y"
    env["I3C_TEST_ROOT"] = str(SAMPLE_ROOT).replace("\\", "/")
    proc = subprocess.Popen(
        [Bash, str(test_sh).replace("\\", "/"), "-r"],
        cwd=str(SAMPLE_ROOT).replace("\\", "/"),
        env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )

    # Poll until test.sh exits (load_and_run stays alive via gdb continue)
    start = time.time()
    timed_out = False
    last_tick = -1
    while True:
        elapsed = time.time() - start
        if elapsed >= timeout:
            timed_out = True
            break
        if proc.poll() is not None:
            break
        # 提前收敛：两核都已见 BOOT + verdict 标记即可收工（RUN_TIMEOUT=0
        # 下 gdb 永不退出，否则每个 case 都要空等满 timeout）。只影响等待
        # 时长，不参与任何 PASS/FAIL 判定。
        c = _core_view(ctrl_cap.lines, "CTRL")
        t = _core_view(tgt_cap.lines, "TGT")
        if (c["boot"] and c["verdict"] is not None and
                t["boot"] and t["verdict"] is not None):
            break
        tick = int(elapsed)
        if tick != last_tick and tick % 5 == 0:
            last_tick = tick
            log(f"  [t+{tick}s] ctrl_boot={c['boot']} tgt_boot={t['boot']} "
                f"ctrl={len(_combined_lines(ctrl_cap.lines))}B "
                f"tgt={len(_combined_lines(tgt_cap.lines))}B")
        time.sleep(0.5)

    # Kill entire test.sh + gdb process tree (qsh-debug §2.3: gdb 残留
    # 会占 CKLink 连接，下次 load 失败)
    try:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass
    time.sleep(1)
    # 兜底：杀所有残留 gdb（进程树杀不净时）
    try:
        subprocess.run(["wmic", "process", "where", "name='riscv64-unknown-elf-gdb.exe'", "delete"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
    except Exception:
        pass
    time.sleep(1)
    ctrl_cap.stop()
    tgt_cap.stop()

    return {
        "case": case_name,
        "ctrl_lines": ctrl_cap.lines,
        "tgt_lines": tgt_cap.lines,
        "timed_out": timed_out,
        "elapsed_s": time.time() - start,
    }


def evaluate_case(case_name, ctrl_lines, tgt_lines):
    """Mbox 双核判定（boot 门 + 时序门 + 逐项）。

    判定构成：
      - 每核一条 execution 项：必须见过本固件 ZT_BOOT，且 boot 之后恰有
        一个 verdict 标记且为 SUCCESSFUL（NO_BOOT / NO_MARKER 一律 FAIL）
      - 行为项：所有 kind == behavior 的（name, 核）项必须全 PASS
      - 零证据守卫：一个行为项都没有 ⇒ 不得判 PASS
      - 观察项（observe=1）与结构性守卫（guard=1）不计入 PASS/FAIL，
        只作呈现（§1.4/§1.5）
    注意：**不**使用 timed_out 判 FAIL —— 见文件头"核心哲学"。
    """
    ctrl = _core_view(ctrl_lines, "CTRL")
    tgt = _core_view(tgt_lines, "TGT")

    def exec_item(view):
        if not view["boot"]:
            result, detail = "NO_BOOT", "未见本固件 BOOT 标记（他方输出或未启动）"
        elif view["verdict"] is None:
            result, detail = "NO_MARKER", "BOOT 之后无 verdict 标记"
        elif view["verdict"] == "SUCCESSFUL":
            result, detail = "PASS", "PROJECT EXECUTION: SUCCESSFUL"
        else:
            result, detail = "FAIL", "PROJECT EXECUTION: FAILED"
        if view["success_lines"] and view["failed_lines"]:
            result = "FAIL"
            detail = (f"标记自相矛盾：SUCCESSFUL×{view['success_lines']} + "
                      f"FAILED×{view['failed_lines']}")
        elif result == "PASS" and view["success_lines"] != 1:
            result = "FAIL"
            detail = f"verdict 标记出现 {view['success_lines']} 次（应恰 1 次）"
        return {
            "name": f"{view['core'].lower()}_execution",
            "core": view["core"],
            "kind": "execution",
            "result": result,
            "detail": detail,
        }

    items = [exec_item(ctrl), exec_item(tgt)]
    items.extend(ctrl["items"])
    items.extend(tgt["items"])

    behavior = [it for it in items if it["kind"] == "behavior"]
    behavior_failed = [it for it in behavior if it["result"] != "PASS"]
    obs_diff = [it for it in items if it["result"] == "OBS_DIFF"]
    guards = [it for it in items if it["kind"] == "guard"]

    exec_ok = all(it["result"] == "PASS" for it in items if it["kind"] == "execution")

    notes = []
    if not behavior:
        notes.append("零证据（非驱动失败）：本 case 一个行为判据项都没有 ⇒ 不得判 PASS")
    if not ctrl["boot"] or not tgt["boot"]:
        missing = [c for c, v in (("CTRL", ctrl), ("TGT", tgt)) if not v["boot"]]
        notes.append("测试环境可疑（非驱动失败）：" + "、".join(
            f"{c} 端口未见 ZT_BOOT" for c in missing) +
            " —— 烧录链路/串口/镜像陈旧")
    if obs_diff:
        notes.append("观察项与记录不符（不判负）：" + "、".join(
            f"{it['core']}:{it['name']}" for it in obs_diff) +
            " —— 驱动可能已修复，须同步改判（COVERAGE.md §3.4/§4）")
    if guards:
        notes.append("结构性守卫（非行为证据）：" + "、".join(
            f"{it['core']}:{it['name']}" for it in guards))

    case_pass = (exec_ok and bool(behavior) and not behavior_failed)

    return {
        "case": case_name,
        "result": "PASS" if case_pass else "FAIL",
        "ctrl_execution": ctrl["verdict"] or ("NO_BOOT" if not ctrl["boot"] else "NO_MARKER"),
        "tgt_execution": tgt["verdict"] or ("NO_BOOT" if not tgt["boot"] else "NO_MARKER"),
        "ctrl_errs": ctrl["err_logs"],      # 仅展示，不参与判定（见文件头）
        "tgt_errs": tgt["err_logs"],        # 仅展示，不参与判定
        "items": items,
        "notes": notes,
        "behavior_total": len(behavior),
        "behavior_passed": len(behavior) - len(behavior_failed),
    }


def print_report(results):
    print("\n--- MBOX dual-core test results ---")
    for r in results:
        items = r.get("items", [])
        behavior = [i for i in items if i.get("kind") == "behavior"]
        bpass = sum(1 for i in behavior if i.get("result") == "PASS")
        print(f"## {r['case']}: {r.get('result', 'UNKNOWN')} "
              f"(行为项 {bpass}/{len(behavior)}) "
              f"[CTRL={r.get('ctrl_execution')}, TGT={r.get('tgt_execution')}]")
        for item in items:
            print(f"  [{item.get('core')}] {item.get('name')}: {item.get('result')} "
                  f"{item.get('detail', '')}")
        print(f"  (err 日志计数仅展示，不参与判定: CTRL={r.get('ctrl_errs')} "
              f"TGT={r.get('tgt_errs')})")
        for note in r.get("notes", []):
            print(f"  ! {note}")


def write_markdown_report(cdir, result, raw=None):
    """§6.4.1/§6.4.2 补写 markdown 报告：与 json 同源（items[] 渲染），覆盖写。"""
    base = cdir / f"{REPORT_BASE}.md"
    items = result.get("items", [])
    behavior = [it for it in items if it.get("kind") == "behavior"]
    bpass = sum(1 for it in behavior if it.get("result") == "PASS")
    failed = [it for it in behavior if it.get("result") == "FAIL"]
    obs = [it for it in items if it.get("kind") == "observe"]
    guards = [it for it in items if it.get("kind") == "guard"]
    status = result.get("result") or "FAIL"
    head = f"## {result.get('case', '')}: {status} (行为项 {bpass}/{len(behavior)})"
    extra = [f"{k.upper()}={result[k]}" for k in ("ctrl_execution", "tgt_execution")
             if result.get(k)]
    if extra:
        head += "  " + ", ".join(extra)

    lines = [
        f"# {DRIVER_NAME} 测试报告",
        "",
        f"- 工程: {DRIVER_NAME}",
        f"- case: {result.get('case', '')}",
        f"- 日期: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
        "- 板级: lsqsh_evb@2os",
        "",
        head,
        "",
    ]
    if raw is not None:
        lines += [
            f"- 采集时长: {raw.get('elapsed_s', 0):.1f}s"
            f"（timed_out={raw.get('timed_out')}；**不参与判定**，见脚本文件头）",
            "",
        ]
    for note in result.get("notes", []):
        lines.append(f"> {note}")
    if result.get("notes"):
        lines.append("")

    lines += [
        "| 测试项 | 核 | 类别 | 结果 | 明细 |",
        "|---|---|---|---|---|",
    ]
    label = {"execution": "核判定", "behavior": "行为", "observe": "观察",
             "guard": "守卫"}
    mark = {"PASS": "✅ PASS", "FAIL": "❌ FAIL", "OBS": "👁 OBS",
            "OBS_DIFF": "⚠ OBS_DIFF", "GUARD": "🛡 GUARD",
            "NO_BOOT": "❌ NO_BOOT", "NO_MARKER": "❌ NO_MARKER"}
    for it in items:
        res = it.get("result", "?")
        lines.append(f"| {it.get('name')} | {it.get('core')} | "
                     f"{label.get(it.get('kind'), it.get('kind'))} | "
                     f"{mark.get(res, res)} | {it.get('detail', '')} |")
    lines.append("")

    # ---- 失败摘要（§1.3：此前报告只有 2 项，看不出是哪条失败）----
    lines.append("### 失败摘要")
    if failed:
        for it in failed:
            lines.append(f"- ❌ [{it.get('core')}] {it.get('name')}: {it.get('detail')}")
    elif status == "PASS":
        lines.append("- 无")
    else:
        for it in items:
            if it.get("result") in ("NO_BOOT", "NO_MARKER", "FAIL"):
                lines.append(f"- ❌ [{it.get('core')}] {it.get('name')}: {it.get('detail')}")
    lines.append("")

    lines.append("### 观察项（observe=1，不判负）")
    if obs:
        for it in obs:
            lines.append(f"- {it.get('result')} [{it.get('core')}] {it.get('name')}: "
                         f"{it.get('detail')}")
        lines.append("")
        lines.append("> 观察项把驱动的**当前缺陷**固化成预期值（COVERAGE.md §3.4）。"
                     "`OBS` = 现状与记录一致；`OBS_DIFF` = 现状已变，"
                     "**须人工复核并同步改判**；两者都不参与本 case 的 PASS/FAIL。")
    else:
        lines.append("- 无")
    lines.append("")

    if guards:
        lines.append("### 结构性守卫（guard=1，非行为证据）")
        for it in guards:
            lines.append(f"- [{it.get('core')}] {it.get('name')}: {it.get('detail')}")
        lines.append("")
        lines.append("> 守卫项只证明该核已启动并走完入口，**不构成行为验证**；"
                     "本 case 的行为判据见上表 `类别=行为` 的行。")
        lines.append("")

    lines.append(f"- `<err>` 日志计数（仅展示，不参与判定）："
                 f"CTRL={result.get('ctrl_errs')}, TGT={result.get('tgt_errs')}")
    lines.append("")

    base.write_text("\n".join(lines), encoding="utf-8")
    return base


# ---- L2 处理层规格（common/process.py）：只声明**本工程特有**的接入点 ----
# 判定族特有（标记式，见文件头）：每核 boot 门 + 时序门 + verdict 标记恰一次 +
# 行为项全 PASS；**不**用 timed_out 判 FAIL。归档存的是逐行文本（_split_lines 的
# 产物）⇒ 判定入参按"单块拼回"还原（拼接后再切分与归档行**逐行等价**）。
# 契约：每工程暴露 l2_spec(facts=None)；facts = **未归档的采集事实**
# （此处 timed_out / elapsed_s：只进报告「采集时长」行，明确不参与判定）。
def l2_spec(facts=None):
    def _judge(ci):
        ctrl = [(0.0, "\n".join(ci.lines.get("CTRL", [])))]
        tgt = [(0.0, "\n".join(ci.lines.get("TGT", [])))]
        result = evaluate_case(ci.case, ctrl, tgt)
        # facts 缺失（离线）时归一到"未超时"，与在线路径的布尔值同形（不进判定）
        result["timed_out"] = bool(ci.facts.get("timed_out"))
        result["elapsed_s"] = round(ci.facts.get("elapsed_s") or 0.0, 1)
        return result

    def _render_md(jl, cdir):
        write_markdown_report(cdir, jl[0],
                              {"timed_out": jl[0].get("timed_out"),
                               "elapsed_s": jl[0].get("elapsed_s", 0.0)})
        return cdir / f"{REPORT_BASE}.md"

    return process.ProcessSpec(
        project=DRIVER_NAME, driver=DRIVER_NAME, report_base=REPORT_BASE,
        judge=_judge, render_md=_render_md,
        console=lambda jl: print_report(jl),
        summary=lambda jl: (jl[0].get("result") or "FAIL",
                            jl[0].get("behavior_passed", 0),
                            jl[0].get("behavior_total", 0)),
    )


def main():
    parser = argparse.ArgumentParser(description="MBOX dual-core regression runner")
    parser.add_argument("--case", default="case_mbox_basic",
                        help="Test case directory name")
    parser.add_argument("--ctrl-uart", default=None)
    parser.add_argument("--tgt-uart", default=None)
    parser.add_argument("--session-dir", default=None)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--no-archive", action="store_true")
    parser.add_argument("--case-timeout", type=int, default=120)
    args = parser.parse_args()

    ctrl_uart = args.ctrl_uart or os.environ.get("I3C_CTRL_UART")
    tgt_uart = args.tgt_uart or os.environ.get("I3C_TGT_UART")
    if not ctrl_uart or not tgt_uart:
        log("ERROR: --ctrl-uart and --tgt-uart required for mbox dual-core test")
        return 1

    log(f"Controller UART: {ctrl_uart}")
    log(f"Target UART: {tgt_uart}")

    date_str = datetime.now().strftime("%Y-%m-%d")
    session = Path(args.session_dir) if args.session_dir else allocate_session_dir(REPORTS_ROOT, date_str)

    if not args.skip_build:
        log(f"[BUILD] {args.case} (both cores)")
        test_sh = TESTS_DIR / args.case / "test.sh"
        env = dict(os.environ)
        env["I3C_TEST_ROOT"] = str(SAMPLE_ROOT).replace("\\", "/")
        rc = subprocess.call([Bash, str(test_sh).replace("\\", "/")],
                             cwd=str(SAMPLE_ROOT).replace("\\", "/"), env=env)
        if rc != 0:
            log(f"[ERROR] build failed rc={rc}")
            return 1

    raw = run_case(args.case, ctrl_uart, tgt_uart, args.case_timeout)
    if raw is None:
        return 1

    # L1→L2 边界（L0 归档形状）：本工程判定入参是 (时刻, 字节块)，先用判定同款的
    # `_split_lines` 还原成逐行文本（= 归档内容），再把**同一个对象**交给 L2。
    # timed_out / elapsed_s 是未归档的采集事实（只进报告「采集时长」行）。
    lines_by_core = {"CTRL": _split_lines(raw["ctrl_lines"]),
                     "TGT": _split_lines(raw["tgt_lines"])}
    if not args.no_archive:
        archive_serial(case_dir_for(session, DRIVER_NAME, args.case), args.case,
                       lines_by_core, board="lsqsh_evb@2os",
                       ports={"CTRL": ctrl_uart, "TGT": tgt_uart},
                       timeout=args.case_timeout, script=__file__)
    return process.run(l2_spec(), case=args.case, lines=lines_by_core,
                       session=session, write=not args.no_archive,
                       facts={"timed_out": raw["timed_out"],
                              "elapsed_s": raw["elapsed_s"]}).rc


if __name__ == "__main__":
    sys.exit(main())
