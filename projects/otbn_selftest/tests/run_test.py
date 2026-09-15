#!/usr/bin/env python3
"""OTBN selftest single-core regression runner: build cases, flash, capture,
evaluate, archive reports (CLAUDE.md §12/§13).

单核工程版：从 trng_test 样板 run_test.py 改造，硬件流程保留 otbn 特有的
交互式单遍烧录（reset → load → 等 "Transfer rate" → set $pc → continue，§10.24/
§10.25）+ 写包提速（§10.23）+ DebugServer 自愈重试（§10.22）。

固件为纯 ztest（ztest_glue）：结果判定以 ztest 原生逐项输出为数据源
（common/ztest_report.parse_ztest），完成标记 = "PROJECT EXECUTION
SUCCESSFUL/FAILED"；串口缺项用 dump_results.py --json 内存兜底（§12.3）。

流程（每 case）：
    1. 编译（west build -p auto，CLAUDE.md 构建规范）
    2. 烧录 + 串口采集（交互式单遍，dbg_run.py 逻辑内联）
    3. 解析（parse_ztest：ZT_BOOT / PASS|FAIL - <name> / INFO / PROJECT EXECUTION）
    4. 判定（§13.5 可归因 reason；串口缺项用 dump_results.py --json 内存兜底，§12.3）
    5. 报告归档（§12）：reports/<YYYY-MM-DD>/<session>/otbn_selftest_report.{md,json}
       + summary.md 索引行（common/report_dir.py）

Usage:
    python run_test.py                 # 全流程 + 归档
    python run_test.py --no-archive   # 只跑不改报告
    python run_test.py --skip-build   # 复用现有构建
    python run_test.py --case-only case_default  # 只跑指定 case
"""
import argparse
import json
import os
import tempfile
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

# tests/run_test.py -> parents[1] = otbn_selftest 工程根
SAMPLE_ROOT = Path(__file__).resolve().parents[1]
REPORTS_ROOT = SAMPLE_ROOT.parents[1] / "reports"   # 810_driver_test/reports/
GDB = "riscv64-unknown-elf-gdb"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
# 用户需求#8：单核一律 1os 构建，cpu1 入口 = 0x10000000（readelf 实测）；
# 2os 构建入口 0x100c0000，需显式 TARGET_ROM_START=0x100c0000 环境变量覆盖
# （不得静默改地址，§10 规范）
TARGET_ROM_START = os.environ.get("TARGET_ROM_START", "0x10000000")
BOARD = "lsqsh_evb@1os/lsqsh/cpu1"   # 用户需求#8：单核固件一律 1os 编译测试
DRIVER_NAME = "otbn_selftest"          # §12.4 summary 行 `<driver>_test` 前缀
REPORT_BASE = f"{DRIVER_NAME}_report"   # otbn_selftest_report.{md,json}（避免 _test_test 重复）

sys.path.insert(0, str(REPORTS_ROOT.parent / "common"))
sys.path.insert(0, str(Path(__file__).resolve().parent / "scripts"))
from ztest_report import parse_ztest   # noqa: E402
from report_dir import (allocate_session_dir, case_dir_for,  # noqa: E402
                        update_summary)

TGT_BANNERS = ("CPU1 OTBN_SELFTEST", "otbn_selftest", "OTBN selftest")

DEBUGSERVER_EXE = r"C:\uaer_app\debug_server\bin\DebugServerConsole.exe"
# ztest 原生逐项输出的完成/活动标记（parse_ztest 数据源）
ZT_BOOT_RE = re.compile(r"ZT_BOOT TGT")
EXEC_RE = re.compile(r"PROJECT EXECUTION (SUCCESSFUL|FAILED)")
ZTEST_ACTIVITY_RE = re.compile(r"(ZT_BOOT|START - | PASS - | FAIL - | SKIP - "
                               r"|PROJECT EXECUTION|Assertion failed)")


def _own_port(p):
    """§10.30 串扰免疫：已见本固件 ZT_BOOT 且带本固件 banner 的端口才算我方。

    他方固件（如 cpu2 残留镜像）无 ZT_BOOT/banner，其 PROJECT EXECUTION
    等标记只记录不参与 completion/early-stop 判定。"""
    pl = p["lines"]
    return (any(ZT_BOOT_RE.search(l) for l in pl)
            and any(b in l for b in TGT_BANNERS for l in pl))


def _netstat_1025():
    """netstat 1025 端口状态：(状态, 本端PID, 对端PID)。§10.17"""
    rows = []
    try:
        out = subprocess.run(["netstat", "-ano"], capture_output=True,
                             text=True, timeout=15).stdout
        for line in out.splitlines():
            if ":1025" not in line:
                continue
            parts = line.split()
            if len(parts) >= 5 and parts[0] == "TCP":
                rows.append((parts[3], parts[4], parts[1]))
    except Exception:
        pass
    return rows


def _kill_residual_gdb():
    """精准杀 1025 ESTABLISHED 连接对端（是 gdb 就 taskkill）。
    subprocess 里一律单斜杠参数（§10.16）。"""
    try:
        pids = set()
        for state, local_pid, _ in _netstat_1025():
            if state == "ESTABLISHED" and local_pid.isdigit():
                pids.add(local_pid)
        for pid in pids:
            ti = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/FO", "CSV"],
                                capture_output=True, text=True, timeout=15).stdout
            if "gdb" in ti.lower():
                subprocess.run(["taskkill", "/F", "/PID", pid],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=15)
    except Exception:
        pass


def _restart_debugserver():
    """重启 DebugServer 并轮询日志 "DebuggerServer$" 提示符判就绪（≤30s）。"""
    log("[ENV] restarting DebugServer (residue)")
    subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
    time.sleep(1)
    for state, local_pid, _ in _netstat_1025():
        if state == "LISTENING" and local_pid.isdigit():
            subprocess.run(["taskkill", "/F", "/PID", local_pid],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=15)
    log_path = os.path.join(tempfile.gettempdir(), "debug_server.log")
    try:
        os.remove(log_path)
    except OSError:
        pass
    try:
        subprocess.Popen(
            [DEBUGSERVER_EXE],
            cwd=os.path.dirname(DEBUGSERVER_EXE),
            stdout=open(log_path, "w", encoding="utf-8", errors="replace"),
            stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NEW_CONSOLE,
        )
    except Exception:
        pass
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            with open(log_path, encoding="utf-8", errors="replace") as f:
                if "DebuggerServer$" in f.read() or "DebuggerServer>" in f.read():
                    break
        except Exception:
            pass
        time.sleep(0.5)
    else:
        log("[ENV] WARNING: DebugServer 30s 内未打印就绪提示符")
    time.sleep(1)


def clean_gdb_residue(force_restart=False):
    """按需清 DebugServer 连接残留：先杀残留 gdb，复查仍残留/server 已死才重启。"""
    _kill_residual_gdb()
    if force_restart:
        _restart_debugserver()
        return
    need = any(st == "ESTABLISHED" for st, _, _ in _netstat_1025())
    if not need:
        need = not any(st == "LISTENING" for st, _, _ in _netstat_1025())
    if need:
        _restart_debugserver()


def restart_debugserver():
    """兜底重启 + 5s 稳定期（§10.20：新实例立即连跑会间歇性环境异常）。"""
    _restart_debugserver()
    time.sleep(5)


ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

# 判定状态：src 表示来源（token / mem / missing）
PASS, FAIL, MISSING, NOT_RUN = "PASS", "FAIL", "MISSING", "NOT_RUN"


def log(msg):
    print(msg, flush=True)


def build_case(case):
    """复刻 test_common.sh build_single_test：位置参数 src + -DDTC_OVERLAY_FILE=。"""
    build = SAMPLE_ROOT / "tests" / case / "build"
    src = SAMPLE_ROOT / "cpu1_otbn_selftest"
    overlay = src / "app.overlay"
    try:
        proc = subprocess.run(
            ["west", "build", "-p", "always", "-b", BOARD, "-d", build.as_posix(),
             src.as_posix(), f"-DDTC_OVERLAY_FILE={overlay.as_posix()}"],
            capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        return False, "build timeout"
    if proc.returncode != 0:
        return False, proc.stdout[-2000:] + proc.stderr[-2000:]
    return True, ""


def reader(ser, lines, stop):
    buf = b""
    while not stop.is_set():
        try:
            chunk = ser.read(256)
        except serial.SerialException:
            break
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = ANSI_RE.sub("", line.decode("utf-8", "replace").rstrip("\r"))
            lines.append(line)


def discover_ports(explicit=None):
    ports = list(explicit) if explicit else []
    if ports:
        return ports
    for p in list_ports.comports():
        hwid = (p.hwid or "").lower()
        desc = (p.description or "").lower()
        if "32bf" in hwid or "cklink" in desc:
            continue
        ports.append(p.device)
    if not ports:
        sys.exit("ERROR: no candidate COM port found (all ports are CKLink?)")
    log(f"[run] candidate UART ports: {', '.join(ports)}")
    return ports


def gdb_flash_once(elf, run_secs, watch=None):
    """烧录（单遍）并运行，run_secs 为采集窗口硬上限。返回 (out, no_boot)。

    watch=ports（capture 轮）时启用完成标记提前收尾（§6.2-3）：
    PROJECT EXECUTION 出现后再静默 5s 即提前杀 gdb；无完成标记且连续 60s
    无 ztest 逐项输出判定套件卡死提前结束；90s 无 ZT_BOOT 判环境问题
    （no_boot=True，交给主流程 [RETRY] 自愈）。正常跑完 = 实际套件耗时，
    不跑满窗口。

    ★ 用户需求#7：continue 前按住 cpu2 复位（APP_CPU_SRST=0x4002200c
    是 W1S/W1C 寄存器：写 2=保持复位（SDK app_cpu_reset 语义）、写 1=
    释放（app_cpu_dereset 语义）、写 0 无效果），防他方 RAM 残留镜像串扰；
    跑完不释放（双核流程自会释放）。
    ★ §10.30 串扰防线二：完成/卡死/BOOT 判定只认 _own_port（已见本固件
    ZT_BOOT+banner）的端口，外来端口只记录回显不判定。

    ★ 交互式流程（与 dbg_run.py 同款）：reset+load 后必须等 gdb 打出
    "Transfer rate"（load 完成）再 set $pc+continue。一次性 -ex 链在
    load 刚结束的瞬间就发 continue，DebugServer 状态不佳时 continue
    会被静默丢弃——CPU 停在 halt 无 BOOT，detach 时才跑起来（实测
    3/3 回归采集复现，dbg_run 交互式流程 0 失败）。

    超时后必须干净退出（interrupt + detach + quit），不能 proc.kill():
    kill 会让 DebugServer 认为连接仍存在（连接残留），下次 target
    remote 能连上但 reset/load 实际无效（§10.1）。
    """
    cmd = [GDB, "-ex", f"target remote {GDB_HOST}:{GDB_PORT}",
           "-ex", "set confirm off", "-ex", "set pagination off",
           # CKLink load 实测 1KB/s → 41KB/s（41x），写包参数默认开启
           "-ex", "set memory-write-packet-size 16384",
           "-ex", "set remotewritesize 16384",
           "-ex", f"file {elf}"]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    gdb_out = []
    gdb_out_stop = threading.Event()

    def gdb_reader():
        while not gdb_out_stop.is_set():
            line = proc.stdout.readline()
            if not line:
                break
            gdb_out.append(line.rstrip("\n"))

    gt = threading.Thread(target=gdb_reader, daemon=True)
    gt.start()

    def gdb_send(*cmds):
        for c in cmds:
            proc.stdin.write(c + "\n")
        proc.stdin.flush()

    def wait_load_done(timeout_s=420):
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if any("Transfer rate" in l for l in gdb_out):
                return True
            if any(("Remote failure" in l) or ("E22" in l) for l in gdb_out):
                return False   # 协议错误（如写包过大被 stub 拒绝），立即失败
            time.sleep(0.5)
        return False

    def quit_clean():
        try:
            gdb_send("\x03")
            time.sleep(1.0)
            gdb_send("detach", "quit")
            proc.wait(timeout=10)
        except Exception:
            proc.kill()
            proc.wait()
        gdb_out_stop.set()
        gt.join(timeout=2)

    no_boot = False

    gdb_send("reset", "load")
    if not wait_load_done():
        log("[run] !!! load timeout/error (Remote failure/E22/420s) -> abort")
        quit_clean()
        return "\n".join(gdb_out), False

    if watch is None:
        # 无采集轮：continue 后干等 run_secs 收尾
        gdb_send("set {int}0x4002200c = 2",
                 f"set $pc={TARGET_ROM_START}", "continue")
        try:
            proc.wait(timeout=run_secs)
        except subprocess.TimeoutExpired:
            pass
        quit_clean()
        return "\n".join(gdb_out), False

    # capture 轮：continue 后按提前收尾判据采集
    gdb_send("set {int}0x4002200c = 2",
             f"set $pc={TARGET_ROM_START}", "continue")
    deadline = time.time() + run_secs
    start_at = time.time()
    done_at = None
    last_byte_at = time.time()
    seen = 0
    seen_all = 0
    while time.time() < deadline and proc.poll() is None:
        # 新 ztest 逐项输出行实时回显（采集不再盲等；外来端口也回显备查）
        all_tokens = [l for p in watch.values() for l in p["lines"]
                      if ZTEST_ACTIVITY_RE.search(l)]
        n_all = len(all_tokens)
        if n_all != seen_all:
            for l in all_tokens[seen_all:]:
                log(f"  [{int(time.time()-start_at):4d}s] {l}")
            seen_all = n_all
        # §10.30 防线二：判定只认我方端口（已见本固件 ZT_BOOT+banner）
        own_lines = [l for p in watch.values() if _own_port(p) for l in p["lines"]]
        own_tokens = [l for l in own_lines if ZTEST_ACTIVITY_RE.search(l)]
        n = len(own_tokens)
        if n != seen:
            seen = n
            last_byte_at = time.time()
        txt = "\n".join(own_lines)
        if EXEC_RE.search(txt):
            if done_at is None:
                done_at = time.time()
            elif time.time() - done_at >= 5:
                log(f"[run] PROJECT EXECUTION + 5s idle -> early stop "
                    f"(elapsed {int(time.time()-start_at)}s)")
                break
        elif done_at is None and seen > 0 and time.time() - last_byte_at >= 60:
            log(f"[run] WARNING: 60s no ztest output (t={int(time.time()-start_at)}s), "
                "suite stalled? -> early stop")
            break
        elif seen == 0 and time.time() - start_at >= 90:
            # load 后 90s 仍无 ZT_BOOT = 环境问题（烧录链路/
            # 连接残留/不稳定期 §10.20）。no_boot=True 强制主流程 [RETRY]
            # 自愈（重启 DebugServer 重试），防止 teardown 窗口内才跑起来
            # 的零散输出骗过 BOOT 检查、产出半截报告。
            log(f"[run] ERROR: 90s no ZT_BOOT (t={int(time.time()-start_at)}s), "
                "环境问题 -> early stop for auto-retry")
            no_boot = True
            break
        time.sleep(0.5)
    quit_clean()
    return "\n".join(gdb_out), no_boot


def capture_case(case, tgt_uart=None, timeout=30):
    """烧录（单遍）+ 串口采集，返回 (lines_by_port, gdb_ok)。"""
    build = SAMPLE_ROOT / "tests" / case / "build"
    elf = (build / "zephyr" / "zephyr.elf").as_posix()
    if not Path(elf).exists():
        return None, False, f"not built: {elf}"

    names = discover_ports([p for p in (tgt_uart,) if p])
    ports = {n: {"lines": [], "stop": threading.Event()} for n in names}

    def start_capture():
        sers = {}
        for n in names:
            try:
                sers[n] = serial.Serial(n, 115200, timeout=0.1)
            except serial.SerialException as e:
                log(f"[run] WARNING: cannot open {n}: {e}")
        threads = []
        for n, ser in sers.items():
            t = threading.Thread(target=reader, args=(ser, ports[n]["lines"], ports[n]["stop"]), daemon=True)
            t.start(); threads.append(t)
        return sers, threads

    # 单遍烧录 + 首轮采集（单核工程）。两遍烧录法的 capture 轮 = 会话内
    # interrupt 后二次 load，实测静默挂死（§10.24）；且 reset 已冲刷
    # I-cache（§10.4 慢一拍场景被覆盖），load 后状态天然干净，直接采首轮。
    # dbg_run.py 同款单遍流程已在 otbn_test 三轮验证（18/25 结果逐项一致）。
    sers, threads = start_capture()
    out, no_boot = gdb_flash_once(elf, timeout, watch=ports)
    time.sleep(0.3)
    for n in ports:
        ports[n]["stop"].set()
    for t in threads:
        t.join(timeout=1)
    for ser in sers.values():
        ser.close()

    if no_boot:
        # 90s 无 ZT_BOOT = 环境问题（烧录链路/连接残留/不稳定期 §10.20），
        # 强制主流程 [RETRY]（重启 DebugServer 后重试），不放行部分采集
        # （teardown 窗口才跑起来的零散输出会骗过 BOOT 检查产半截报告）
        return ports, False, "no ZT_BOOT within 90s (env issue, auto-retry)"
    ok = "Loading section" in out
    if not ok:
        return ports, False, "capture flash failed"
    return ports, True, ""


def parse_expected_from_coverage():
    """从 COVERAGE.md 提取每项的预期数据（表格第 4 列）。"""
    cov = SAMPLE_ROOT / "tests" / "COVERAGE.md"
    if not cov.exists():
        return {}
    expected = {}
    for line in cov.read_text(encoding="utf-8").splitlines():
        m = re.match(r"\|\s*(\w+)\s*\|\s*([^|]*)\s*\|\s*([^|]*)\s*\|\s*([^|]*)\s*\|", line)
        if m:
            expected[m.group(1).strip()] = m.group(4).strip()
    return expected


def memory_backfill(case, test_names):
    """dump_results.py --json 读内存兜底（§12.3）。失败返回 None → mem-unavailable。"""
    script = SAMPLE_ROOT / "tests" / "scripts" / "dump_results.py"
    try:
        proc = subprocess.run([sys.executable, str(script), "--case", case, "--json"],
                              capture_output=True, text=True, timeout=60)
    except (subprocess.TimeoutExpired, OSError):
        return None
    if proc.returncode != 0:
        return None
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError:
        return None


def evaluate_case(case, lines, test_names, mem_data):
    """单核判定：ztest 原生输出优先 → 内存兜底 → NOT_RUN（§12.3）。

    返回 (results, infos, reasons, notes, execution)。"""
    p = parse_ztest(lines, tgt_banners=TGT_BANNERS)
    results, notes, reasons = {}, {}, {}
    for name in test_names:
        t = p["tests"].get(name)
        st = t["result"] if t else MISSING
        note = "src=token"
        if t and t["reason"]:
            reasons[name] = t["reason"]
        if st == MISSING and mem_data is not None:
            mem = mem_data.get("TGT", {}).get(name)
            if mem == "PASS":
                st, note = PASS, "src=mem"
                reasons.setdefault(name, mem_data.get("reasons", {}).get(name, ""))
            elif mem == "FAIL":
                st, note = FAIL, "src=mem"
                reasons.setdefault(name, mem_data.get("reasons", {}).get(name, ""))
            elif mem == "NOT_RUN":
                st, note = NOT_RUN, "src=mem(NOT_RUN)"
            else:
                note = "mem-unavailable"
        elif st == MISSING and mem_data is None:
            st, note = FAIL, "mem-unavailable"
        results[name] = st
        notes[name] = note
    return results, p["infos"], reasons, notes, p["execution"]


def write_markdown_report(case, results, infos, reasons, notes, expected, execution,
                          date_dir, env_line):
    """§12.1 覆盖写：同一天多次回归只保留 latest（对比历史用其他日期）。"""
    base = date_dir / f"{REPORT_BASE}.md"
    lines = [
        "# OTBN 测试报告",
        "",
        f"- 工程: {DRIVER_NAME}",
        f"- 日期: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
        f"- 板级: {BOARD}",
        f"- 环境: {env_line}",
        f"- PROJECT EXECUTION: {execution}",
        "",
        "## 测试项结果",
        "",
        "| 测试项 | 结果 | 预期(COVERAGE) | 实际(INFO) | 失败原因 | 来源 |",
        "|---|---|---|---|---|---|",
    ]
    for name in results:
        st = results[name]
        if st == PASS:
            st_txt, reason = "✅ PASS", ""
        elif st == FAIL:
            st_txt, reason = "❌ FAIL", reasons.get(name, "")
        elif st == NOT_RUN:
            st_txt, reason = "⬜ NOT_RUN", ""
        else:
            st_txt, reason = "⚠️ " + st, reasons.get(name, "")
        info = ", ".join(f"{k}={v}" for k, v in infos.get(name, {}).items())
        exp = expected.get(name, "")
        lines.append(f"| {name} | {st_txt} | {exp} | {info} | {reason} | {notes.get(name, '')} |")
    lines.append("")
    base.parent.mkdir(parents=True, exist_ok=True)
    with open(base, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def write_json_report(case, results, infos, reasons, notes, execution,
                      date_dir, env_line):
    """§12.1 覆盖写：同一天多次回归只保留 latest run。"""
    base = date_dir / f"{REPORT_BASE}.json"
    data = {
        "driver": DRIVER_NAME, "case": case, "board": BOARD,
        "env": env_line, "execution": execution,
        "timestamp": datetime.now().isoformat(timespec="seconds"),
        "results": {name: st for name, st in results.items()},
        "infos": infos, "reasons": reasons, "notes": notes,
    }
    base.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-archive", action="store_true", help="只跑不改报告")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--case-only", default=None)
    ap.add_argument("--case", default="case_default")
    ap.add_argument("--session-dir", default=None)
    ap.add_argument("--tgt-uart", default=None)
    ap.add_argument("--timeout", type=float, default=420, help="单 case 采集窗口（勿小于 300）")
    args = ap.parse_args()

    cases = [args.case_only] if args.case_only else [args.case]
    # 与 dump_results.py TEST_NAMES 同源（链接段 SORT_BY_NAME 升序，见其注释）
    from dump_results import TEST_NAMES as test_names
    expected = parse_expected_from_coverage()
    env_line = f"tgt_uart={args.tgt_uart or 'auto'}, timeout={args.timeout}s"
    date_str = datetime.now().strftime("%Y-%m-%d")

    clean_gdb_residue()   # §6.2-6：回归前按需清连接残留
    n_fail = 0   # rc 语义：0=全部 case PASS / 1=有 FAIL（run_all 批量判定用）

    for case in cases:
        log(f"===== CASE {case} =====")
        if not args.skip_build:
            ok, err = build_case(case)
            if not ok:
                log(f"[run] BUILD FAILED: {err}")
                n_fail += 1
                continue
        # BOOT 环境判定 + §10.22 自愈重试：load 后无 ZT_BOOT =
        # 测试环境问题（烧录链路/连接残留/镜像陈旧）→ 重启 DebugServer 重试 1 次
        ports, ok, err = capture_case(case, args.tgt_uart, args.timeout)
        for attempt in range(2):
            if ok:
                # §10.30：BOOT 判定只认我方端口（已见本固件 ZT_BOOT+banner，
                # _own_port，与 capture_case 内完成/卡死判定同款门控）；
                # 他方残留镜像的 ZT_BOOT 不算本固件已启动
                own_lines = [l for p in ports.values() if _own_port(p)
                             for l in p["lines"]]
                if any(ZT_BOOT_RE.search(l) for l in own_lines):
                    break
                ok, err = False, "no ZT_BOOT after flash"
            if attempt == 0:
                log(f"[RETRY] {case}: {err}（环境问题）→ 重启 DebugServer 后重试")
                restart_debugserver()
                ports, ok, err = capture_case(case, args.tgt_uart, args.timeout)
        if not ok:
            log(f"[run] CAPTURE FAILED（环境问题，已自愈重试）: {err}")
            n_fail += 1
            continue
        lines = []
        for n, p in ports.items():
            text = "\n".join(p["lines"])
            if any(b in text for b in TGT_BANNERS):
                lines = p["lines"]
        if not lines:
            log(f"[run] WARNING: no TGT banner lines captured ({case})")

        mem = memory_backfill(case, test_names)
        if mem is None:
            log("[run] memory backfill: mem-unavailable (串口 token 缺失时按 §12.3 判 FAIL)")
        results, infos, reasons, notes, execution = evaluate_case(case, lines, test_names, mem)

        log(f"--- {case} results ---")
        passed = ran = 0
        for name in test_names:
            st = results[name]
            if st == PASS:
                passed += 1
            if st != NOT_RUN:
                ran += 1
            tag = f"  {name:<20} {st}"
            if reasons.get(name):
                tag += f"  ({reasons[name]})"
            log(tag)
        log(f"-> {passed}/{ran} PASS, execution={execution}")

        case_ok = (passed == ran and ran > 0 and execution == "SUCCESSFUL")
        if not case_ok:
            n_fail += 1
        if not args.no_archive:
            session_dir = Path(args.session_dir) if args.session_dir else \
                allocate_session_dir(REPORTS_ROOT, date_str)
            case_dir = case_dir_for(session_dir, DRIVER_NAME, args.case)
            write_markdown_report(case, results, infos, reasons, notes, expected,
                                  execution, case_dir, env_line)
            write_json_report(case, results, infos, reasons, notes, execution,
                              case_dir, env_line)
            update_summary(session_dir, f"{DRIVER_NAME}_{args.case}",
                           "PASS" if case_ok else "FAIL", passed, ran,
                           f"{case_dir.name}/{REPORT_BASE}.md")
            log(f"[run] archived -> {case_dir / (REPORT_BASE + '.md')}")
        else:
            log("[run] --no-archive: 未写报告")

    log("===== DONE =====")
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
