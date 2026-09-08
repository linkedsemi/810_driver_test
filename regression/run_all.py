#!/usr/bin/env python3
"""810_driver_test 一键全量回归总控。

批量全程脚本自驱（构建/烧录/采集/归档/自愈都在各工程 run 脚本内），
agent 只负责启动；单 case FAIL 不阻断后续 case；预检 FAIL 才中止（exit 2）。
一次批量 = 一个会话目录 reports/<YYYY-MM-DD>-<idx>（spec §9.1）。

用法：
  python run_all.py                # 环境预检 + cases.yaml 全部启用 case
  python run_all.py --no-precheck  # 跳过预检
  python run_all.py --dry-run      # 只列启用 case 与映射脚本，不执行
"""
import argparse
import datetime
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent   # 810_driver_test
REPORTS_ROOT = ROOT / "reports"
CASES_FILE = Path(__file__).resolve().parent / "cases.yaml"
PRECHECK = ROOT / "regression" / "precheck" / "run_precheck.py"

# 双核工程 UART：None = 由 precheck 自动发现后从 port_mapping.json 读取；
# 环境变量可强制覆盖（同 precheck PRE_CTRL_UART / PRE_TGT_UART）。
CTRL_UART = os.environ.get("PRE_CTRL_UART") or None
TGT_UART  = os.environ.get("PRE_TGT_UART")  or None

# DebugServer（§0/§10.17）：1025 由 PC 进程 XuanTie DebugServer 提供；
# 重启 = 四步自愈（杀残留 gdb → taskkill /IM → netstat 兜底 → Popen + 日志轮询）。
# ★A3: 日志路径以 run_precheck.py:177 实测定稿为准（%TEMP%/debug_server.log），
# 计划笔误 DEBUGSERVER_EXE.parent / "DebugServer.log" 不用。
DEBUGSERVER_EXE = Path(r"C:\uaer_app\debug_server\bin\DebugServerConsole.exe")
DEBUGSERVER_LOG = Path(tempfile.gettempdir()) / "debug_server.log"
GDB_HOST, GDB_PORT = "127.0.0.1", "1025"
GDB = "riscv64-unknown-elf-gdb"

# 工程 → run 脚本（tests/scripts/ 下，调用前逐一存在性校验）
PROJECT_SCRIPT = {
    "trng_test": "run_test.py",
    "rtc_ls_test": "run_test.py",
    "otbn_test": "run_test.py",
    "otbn_selftest": "run_test.py",
    "i2c_pure_test": "run_test.py",
    "i3c_i2c_switch_test": "run_test.py",
    "i3c_dualcore_test": "run_test.py",
    "mbedtls_test": "run_test.py",
    # 新增 6 工程（2026-09-04，全量回归纳入）
    "adc_temper_test": "run_test.py",
    "dma_test": "run_test.py",
    "flash_test": "run_test.py",
    "mbox_test": "run_test.py",
    "wdt_test": "run_test.py",
    "iopmp_test": "run_test.py",
}
DUAL_CORE = {"i2c_pure_test", "i3c_i2c_switch_test", "i3c_dualcore_test", "mbox_test"}


# ---- DebugServer 环境管理（run_precheck.py 既有实现照搬，§10.17 四步）----

def _netstat_1025():
    """netstat 1025 端口状态：(状态, 本端PID, 对端PID)。"""
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

    ★ Python subprocess 无 MSYS 路径转换层：tasklist/taskkill 参数必须用
    单斜杠（/FI /FO /F /PID），双斜杠 //F 会被 Windows 程序拒绝（§10.16）。
    """
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
    """重启 DebugServer 进程并等就绪（日志轮询 "DebuggerServer$" 提示符）。

    实测：任何"连接→detach→退出"的 gdb（包括探测连接）都会让 DebugServer
    留下连接残留——detach 不能真正释放连接，唯一可靠的方式是重启进程
    （§10.17）。CKLink 初始化耗时随机，不能只固定 sleep → 日志轮询。
    （run_precheck.py _restart_debugserver 原样照搬）
    """
    print("[run_all] restarting DebugServer (residue)", file=sys.stderr)
    try:
        subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=15)
    except Exception:
        pass   # taskkill 超时/失败不阻断自愈流程（netstat 精确杀兜底，§10.17）
    time.sleep(1)
    # taskkill /IM 可能杀不净（无效实例占 1025，新实例绑定失败卡在初始化）
    # → 用 netstat 精确杀 1025 的 LISTENING 进程兜底。
    for state, local_pid, _ in _netstat_1025():
        if state == "LISTENING" and local_pid.isdigit():
            try:
                subprocess.run(["taskkill", "/F", "/PID", local_pid],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=15)
            except Exception:
                pass
    try:
        DEBUGSERVER_LOG.unlink()
    except OSError:
        pass
    try:
        subprocess.Popen(
            [str(DEBUGSERVER_EXE)],
            cwd=str(DEBUGSERVER_EXE.parent),
            stdout=open(DEBUGSERVER_LOG, "w", encoding="utf-8", errors="replace"),
            stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NEW_CONSOLE,
        )
    except Exception:
        pass
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            with open(DEBUGSERVER_LOG, encoding="utf-8", errors="replace") as f:
                if "DebuggerServer$" in f.read() or "DebuggerServer>" in f.read():
                    break
        except Exception:
            pass
        time.sleep(0.5)
    else:
        print("[run_all] WARNING: DebugServer 30s 内未打印就绪提示符", file=sys.stderr)
    time.sleep(1)


def clean_gdb_residue(force_restart: bool = False) -> None:
    """清 DebugServer 连接残留（§10.1 高频坑）。

    ★ 按需重启：先杀残留 gdb，杀完复查 1025——连接残留仍在（ESTABLISHED
    未释放）或 server 已死（无 LISTENING）才重启；健康则不重启（§10.17）。
    （run_precheck.py clean_gdb_residue 原样照搬；A4：precheck 之后、
    erase 重启 DebugServer 之后、进入 case 循环前各调用一次）
    """
    _kill_residual_gdb()
    if force_restart:
        _restart_debugserver()
        return
    need = False
    for state, _, _ in _netstat_1025():
        if state == "ESTABLISHED":
            need = True
            break
    if not need:
        # server 没起来（无 LISTENING）也要重启
        need = not any(st == "LISTENING" for st, _, _ in _netstat_1025())
    if need:
        _restart_debugserver()


def restart_debugserver() -> None:
    """重启 XuanTie DebugServer 并等稳定期（§10.20：新实例立即连跑会
    间歇性 load_fail/断连，30s 稳定期后再连）。"""
    _restart_debugserver()
    time.sleep(30)   # 稳定期：新实例 CKLink 初始化/总线状态稳定后再连跑


# ---- cases.yaml 读取与擦除编排 ----

def read_cases():
    """cases.yaml：YAML list，# 开头或空行忽略（# 屏蔽 = 跳过该 case）。"""
    out = []
    text = CASES_FILE.read_text(encoding="utf-8")
    # 简单 YAML list 解析：兼容无 PyYAML 环境
    # 支持：
    #   cases:
    #     - projects\foo\tests\case_bar
    #     # - projects\foo\tests\case_skip
    in_cases_section = False
    for raw in text.splitlines():
        ln = raw.rstrip()
        if not ln or ln.lstrip().startswith("#"):
            continue
        stripped = ln.strip()
        if stripped == "cases:":
            in_cases_section = True
            continue
        if not in_cases_section:
            continue
        if stripped.startswith("-"):
            item = stripped[1:].strip().strip('"').strip("'")
            item = item.replace("\\", "/")
            if item.lower().startswith("810_driver_test/"):
                item = item[len("810_driver_test/"):]
            p = ROOT / item
            if not (p / "test.sh").exists():
                print(f"[run_all] SKIP {item}: 目录不存在或无 test.sh")
                continue
            out.append(p)
    return out


def _probe_cklink():
    """快速探测 CKLink 是否可用（5s 超时）。

    用 DebugServer 尝试监听 1025 → GDB 连一次 → 看是否进 "Remote debugging"。
    返回 True = CKLink 正常，False = 需要用户拔插 USB。
    """
    # 先确保证 DebugServer 在跑（probe 需要它）
    ds_was_running = any(st == "LISTENING" for st, _, _ in _netstat_1025())
    if not ds_was_running:
        try:
            subprocess.Popen(
                [str(DEBUGSERVER_EXE)],
                cwd=str(DEBUGSERVER_EXE.parent),
                stdout=open(DEBUGSERVER_LOG, "w", encoding="utf-8", errors="replace"),
                stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NEW_CONSOLE,
            )
        except Exception:
            return False
        deadline = time.time() + 15
        while time.time() < deadline:
            try:
                with open(DEBUGSERVER_LOG, encoding="utf-8", errors="replace") as f:
                    if "DebuggerServer$" in f.read() or "DebuggerServer>" in f.read():
                        break
            except Exception:
                pass
            time.sleep(0.5)
        else:
            return False
        time.sleep(1)

    # GDB 探测连接
    try:
        proc = subprocess.run(
            [GDB, "-batch", "-ex", "target remote 127.0.0.1:1025", "-ex", "detach", "-ex", "quit"],
            capture_output=True, text=True, timeout=8,
            encoding="utf-8", errors="replace",
        )
        # -batch 模式不打印 "Remote debugging"，改判 "detached" 或 退出码 0
        combined = proc.stdout + proc.stderr
        ok = proc.returncode == 0 and ("detached" in combined.lower()
                                        or "remote target" in combined.lower())
    except Exception:
        ok = False

    # 探测完杀掉临时 DebugServer（erase 需要无 DebugServer 状态）
    if not ds_was_running:
        try:
            subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=10)
        except Exception:
            pass
        time.sleep(3)  # CKLink USB 释放需要时间，太短会导致后续 erase 卡住
    return ok


def erase_flash_once():
    """flash 全片擦除：DebugServer 必须先退出（CKLink 互斥，§15）。

    预清理残留进程 → 直接擦除 → 重启 DebugServer。
    CKLink 健康性由 cleanup_cklink()（回归结束时）保证，不在此探测
    （实测：探测的启停 DebugServer 会干扰 CKLink 状态，反而导致擦除卡死）。
    """
    print("[run_all] === flash 全片擦除 ===")

    # 预清理残留进程
    _kill_residual_gdb()
    try:
        subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=10)
    except Exception:
        pass
    time.sleep(1)
    for state, local_pid, _ in _netstat_1025():
        if state == "LISTENING" and local_pid.isdigit():
            try:
                subprocess.run(["taskkill", "/F", "/PID", local_pid],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=10)
            except Exception:
                pass

    # 执行擦除
    rc = subprocess.call([sys.executable, str(ROOT / "scripts" / "flash_program.py"),
                          "--erase-only", "--timeout", "20"])
    if rc != 0:
        print("[run_all] flash 擦除失败，批量中止")
        return rc
    restart_debugserver()
    return 0


def cleanup_cklink():
    """回归结束后释放 CKLink（§10.17 / §15 互斥）。

    回归全程 DebugServer 常驻（case 循环中按需重启），跑完后必须杀掉，
    否则下次 erase_flash_once 时 CKLink 仍被占 → flash 卡死 180s。
    """
    print("[run_all] === 释放 CKLink（杀 DebugServer + 残留 gdb）===")
    _kill_residual_gdb()
    try:
        subprocess.run(["taskkill", "/F", "/IM", "DebugServerConsole.exe"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=10)
    except Exception:
        pass
    # 精确杀 1025 上的进程（/IM 偶发杀不净）
    for state, local_pid, _ in _netstat_1025():
        if local_pid.isdigit():
            try:
                subprocess.run(["taskkill", "/F", "/PID", local_pid],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=10)
            except Exception:
                pass
    # 确认 1025 已释放
    time.sleep(1)
    remaining = [(s, p) for s, p, _ in _netstat_1025() if s == "LISTENING"]
    if remaining:
        print(f"[run_all] WARNING: 1025 仍有 LISTENING: {remaining}", file=sys.stderr)
    else:
        print("[run_all] CKLink 已释放")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-precheck", action="store_true")
    ap.add_argument("--no-erase", action="store_true",
                    help="跳过 flash 全片擦除（默认全量前擦一次，用户 #6）")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    cases = read_cases()
    if not cases:
        print("[run_all] ERROR: cases.yaml 无启用 case")
        return 1
    if args.dry_run:
        print("[run_all] DRY-RUN，启用 case 与映射：")
        for c in cases:
            proj = c.parents[1].name
            print(f"  {proj:<22} {c.name:<26} -> {PROJECT_SCRIPT.get(proj, '??')}")
        return 0

    # ★ try/finally：任何退出路径都释放 CKLink（§15 互斥）
    try:
        return _run_all(args, cases)
    finally:
        cleanup_cklink()


def _run_all(args, cases):
    """run_all 主逻辑（由 main 的 try/finally 保护退出时释放 CKLink）。"""

    # Step 3b：flash 全片擦除（A5：--dry-run 返回后、分配会话目录之前；
    # --no-precheck 不动擦除——擦除恒做，--no-erase 逃生门）
    if not args.no_erase:
        rc = erase_flash_once()
        if rc != 0:
            return 2
        # A4-②: erase_flash_once 重启 DebugServer 之后、进入 case 循环前
        # （erase 先于 precheck，此残留若被 precheck 探测重新制造，
        # 由下方 A4-① 的 clean 再清一次）
        clean_gdb_residue()

    sys.path.insert(0, str(ROOT / "common"))
    from report_dir import allocate_session_dir, update_summary
    session = allocate_session_dir(REPORTS_ROOT, datetime.date.today().isoformat())
    print(f"[run_all] session: {session}")

    if not args.no_precheck:
        print("[run_all] === 环境预检 ===")
        rc = subprocess.call([sys.executable, str(PRECHECK),
                              "--session-dir", str(session)])
        if rc != 0:
            print("[run_all] 预检 FAIL，批量中止；请交 agent 排查（qsh-debug）后重跑")
            return 2
        # A4-①: precheck 的探测连接/load 会话必留残留（T1，§10.17）→
        # case 循环前必须清一次（若 --no-precheck 则上面的 A4-② 覆盖）
        clean_gdb_residue()

    # ---- 读取端口映射（precheck 自动发现 或 环境变量指定） ----
    import json as _json
    global CTRL_UART, TGT_UART
    _mapping_file = session / "port_mapping.json"
    if not CTRL_UART or not TGT_UART:
        if _mapping_file.exists():
            _m = _json.loads(_mapping_file.read_text(encoding="utf-8"))
            CTRL_UART = CTRL_UART or _m.get("ctrl")
            TGT_UART  = TGT_UART  or _m.get("tgt")
            print(f"[run_all] port mapping from precheck: CTRL={CTRL_UART} TGT={TGT_UART}")
        else:
            # --no-precheck 且无环境变量：做一次最小发现（不 load，只看在线口）
            _ports = [p.device for p in __import__("serial.tools.list_ports", fromlist=["comports"]).comports()
                      if "32bf" not in (p.hwid or "").lower() and "cklink" not in (p.description or "").lower()]
            if len(_ports) >= 2:
                # 无法在无固件情况下识别角色，按顺序猜（用户应通过 precheck 或环境变量指定）
                CTRL_UART, TGT_UART = _ports[0], _ports[1]
                print(f"[run_all] --no-precheck, 无映射文件，按枚举顺序猜: CTRL={CTRL_UART} TGT={TGT_UART}（建议跑一次 precheck）")
            else:
                print("[run_all] ERROR: 无足够串口且无 port_mapping.json，双核工程无法运行", file=sys.stderr)
                return 2

    logs = session / "logs"
    logs.mkdir(exist_ok=True)
    results = []
    built_projects = set()
    for c in cases:
        proj = c.parents[1].name
        script = PROJECT_SCRIPT.get(proj)
        script_path = c.parent / script if script else None
        if not script_path or not script_path.exists():
            print(f"[run_all] SKIP {c}: 无 run 脚本映射（{script}）")
            results.append((proj, c.name, "SKIP"))
            continue
        cmd = [sys.executable, str(script_path), "--case", c.name,
               "--session-dir", str(session)]
        if proj in DUAL_CORE:
            cmd += ["--ctrl-uart", CTRL_UART, "--tgt-uart", TGT_UART]
        else:
            cmd += ["--tgt-uart", TGT_UART]  # 单核工程也传 TGT 串口
        if proj == "i3c_dualcore_test" and proj in built_projects:
            cmd.append("--skip-build")   # 同会话首个 i3c case 已全量构建，无混旧镜像
        built_projects.add(proj)
        logf = logs / f"{proj}_{c.name}.log"
        print(f"[run_all] === {proj} {c.name} START（log: {logf.name}）===")
        with open(logf, "wb") as fh:
            rc = subprocess.call(cmd, stdout=fh, stderr=subprocess.STDOUT)
        status = "PASS" if rc == 0 else "FAIL"
        results.append((proj, c.name, status))
        print(f"[run_all] === {proj} {c.name} DONE rc={rc} ===")
        # ★ 失败 case 的 run_test.py 在归档前就 return 1，不会调 update_summary；
        #   由 run_all 兜底写入 summary.md（report 指向日志，因为无报告目录）。
        if status == "FAIL":
            update_summary(session, f"{proj}_{c.name}", "FAIL", 0, 0,
                           f"logs/{proj}_{c.name}.log")

    print("\n[run_all] 汇总：")
    n_fail = 0
    for proj, case, st in results:
        print(f"  {st:<5} {proj:<22} {case}")
        n_fail += (st == "FAIL")
    n_pass = len(results) - n_fail
    print(f"\n[run_all] 共 {len(results)} 个测试，通过 {n_pass} 个，失败 {n_fail} 个")
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
