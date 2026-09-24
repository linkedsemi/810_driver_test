#!/usr/bin/env python3
"""归档完整性判据 —— 要求归档是「一次、完整、从头到尾」的运行。

判据（每核独立）：
  H1 头部完整：该核日志里，第一条 ztest 信号行必须是 ZT_BOOT（不允许在它之前
     出现 START/PASS/FAIL/SKIP/INFO 等**测试项行** —— 出现即"尾片段"）。
  H2 唯一性：`ZT_BOOT <CORE>`、`Running TESTSUITE`、`PROJECT EXECUTION` 各恰好 1 次
     （>1 说明夹带了上一轮残留 = 不是一次干净的运行）。
  H3 尾部完整：最后一条非空行必须是 `PROJECT EXECUTION SUCCESSFUL`
     （不是则说明窗口到点把收尾截掉了 → 归档被截断）。

**核集合由工程决定**（2026-09-24 修正）：
  `projects/<proj>/` 下 `cpu*` 目录数 1 ⇒ 单核，只有 `serial_TGT.log`（cpu1 = target）；
  2 ⇒ 双核，两口都要有。工程名由 case 目录名 `<project>_<case>` 在首个 `_case_` 处切分。
  ⚠️ 旧版写死 `("CTRL","TGT")`，把"日志不存在"一律计 BAD ⇒ 对**单核工程**
  （24 个工程里 19 个）**恒报 `ARCHIVE: BAD`**，属**口径误报**而非内容退化 ——
  危害是诱使人放弃对单核工程的归档核验（`KNOWN_ISSUES #117` 增补段）。

用法: python scripts/check_archive.py <case_dir>
"""
import re
import sys
from pathlib import Path

# ⚠️ 本仓默认 stdout 编码是 gbk/cp936（Windows 中文环境）：下面的输出含 `⇒`/`⚠️` 等
# 非 GBK 字符，**重定向/管道/被脚本捕获时会抛 UnicodeEncodeError**（交互式控制台走
# WriteConsoleW，不受影响 ⇒ 只在自动化里炸，最难发现）。同族既有条目见 KNOWN_ISSUES #91。
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parent.parent      # scripts/ 的上一级 = 810_driver_test

#: 退出码 —— **内部错误必须与"判到 BAD"可区分**：
#: Python 未捕获异常默认退 1，若与 BAD 同码，只看退出码无法分辨"判坏了"还是"跑挂了"。
RC_OK, RC_BAD, RC_USAGE, RC_INTERNAL = 0, 1, 2, 3

ITEM_RE = re.compile(r"^\s*(START|PASS|FAIL|SKIP)\s*-\s|\bINFO\s+\S+:")
BOOT_RE = re.compile(r"^\s*ZT_BOOT\s+(\S+)\s*$")


def expected_cores(cdir: Path):
    """期望核集合 —— 由**工程**决定。返回 (cores|None, project|None)。

    解析不出工程（case 目录已被拷到 tmp/ 快照等）⇒ 返回 (None, None)，
    由调用方退回"按实际存在的日志枚举"并**显式标注**，不静默放行。
    """
    name = cdir.name
    if "_case_" in name:
        proj = name.split("_case_", 1)[0]
        pdir = ROOT / "projects" / proj
        if pdir.is_dir():
            n = sum(1 for d in pdir.glob("cpu*") if d.is_dir())
            # 单核工程只有 cpu1（= target ⇒ serial_TGT.log）
            return (["CTRL", "TGT"] if n >= 2 else ["TGT"]), proj
    return None, None


def check(log_path: Path, core: str):
    raw = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    lines = [l.rstrip() for l in raw]
    nonempty = [l for l in lines if l.strip()]
    issues = []

    boots = [i for i, l in enumerate(nonempty) if BOOT_RE.match(l)]
    if not boots:
        issues.append("H2: 无 ZT_BOOT")
        return issues, 0
    if len(boots) > 1:
        issues.append(f"H2: ZT_BOOT 出现 {len(boots)} 次（应为 1）")
    bi = boots[0]

    # H1 头部
    pre = [l for l in nonempty[:bi] if ITEM_RE.match(l)]
    if pre:
        issues.append(f"H1: ZT_BOOT 之前已有 {len(pre)} 行测试项 → 尾片段；首行={pre[0][:60]!r}")

    n_suite = sum(1 for l in nonempty if l.strip().startswith("Running TESTSUITE"))
    if n_suite != 1:
        issues.append(f"H2: Running TESTSUITE 出现 {n_suite} 次（应为 1）")
    n_exec = sum(1 for l in nonempty if l.strip().startswith("PROJECT EXECUTION"))
    if n_exec != 1:
        issues.append(f"H2: PROJECT EXECUTION 出现 {n_exec} 次（应为 1）")

    # H3 尾部
    if not nonempty or not nonempty[-1].strip().startswith("PROJECT EXECUTION SUCCESSFUL"):
        issues.append(f"H3: 末行非 PROJECT EXECUTION SUCCESSFUL → 截断；末行={nonempty[-1][:60]!r}"
                      if nonempty else "H3: 空日志")

    return issues, len(nonempty)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return RC_USAGE
    cdir = Path(sys.argv[1])
    if not cdir.is_dir():
        print(f"[ERR] 目录不存在: {cdir}")
        return RC_USAGE
    cores, proj = expected_cores(cdir)
    if cores is None:
        cores = [c for c in ("CTRL", "TGT") if (cdir / f"serial_{c}.log").exists()]
        print(f"[NOTE] 工程解析不出（case 目录名 {cdir.name!r}）⇒ 按实际存在的日志枚举：{cores or '无'}")
    else:
        print(f"[工程] {proj} ⇒ 期望核 {cores}")

    if not cores:
        # ⚠️ 零证据不得判 COMPLETE —— "什么都没核" 与 "核过且通过" 必须可区分。
        # （空集合静默变成"通过"是最危险的形态：看起来像没问题。）
        print("[ERR] 一个 serial_*.log 都没找到 ⇒ 无从核验，不得判 COMPLETE")
        return RC_USAGE

    bad = False
    for core in cores:
        p = cdir / f"serial_{core}.log"
        if not p.exists():
            print(f"[{core}] MISSING {p}")
            bad = True
            continue
        issues, n = check(p, core)
        status = "OK  " if not issues else "BAD "
        print(f"[{core}] {status} lines={n}")
        for it in issues:
            print(f"        - {it}")
            bad = True
    print("ARCHIVE:", "BAD" if bad else "COMPLETE")
    return RC_BAD if bad else RC_OK


if __name__ == "__main__":
    # 内部错误退 RC_INTERNAL(3)，**刻意避开 RC_BAD(1)** —— Python 未捕获异常默认退 1，
    # 若不多加一层，"脚本崩了" 与 "判到 BAD" 只看退出码不可区分（真事：非 UTF-8 输出
    # 通道下 `⇒` 抛 UnicodeEncodeError，退出码与 BAD 同码且无任何判定输出）。
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except BaseException as exc:                      # noqa: BLE001
        print(f"[ERR] 内部错误（非判定结果）: {type(exc).__name__}: {exc}", file=sys.stderr)
        sys.exit(RC_INTERNAL)
