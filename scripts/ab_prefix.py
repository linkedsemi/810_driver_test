#!/usr/bin/env python3
"""B2 离线自证（§6.4.9 第 2 档）：`ztest_report.py` 前缀白名单化前后，
在**全部真实归档 + 全部合成归档**上对 `parse_ztest` 的输出做逐字段 A/B。

判据：唯一允许的差异 = 原本被硬编码 `I3C_I2C_TEST` 放过的、现在被白名单
（`RTC_TEST` / `MBOX_TEST`）认领的行。任何其它差异 = 回归。

⚠️ 2026-09-24 由 `tmp/b2/ab_prefix.py` 迁来：本脚本被耐久文档当**证据脚本**点名，
   却住在会被清理的 `tmp/` 里 —— 一旦清理就**静默失去该证据脚本**。
   迁移时只改了一处：根目录由 `parents[2]`（`tmp/<子目录>/`）改为 `parents[1]`（`scripts/`）。
   ⚠️ 遗留依赖：语料之一 `tmp/a1/synth/`（合成归档）**未随迁**（属 `tmp/a1` 线束的产物树）。
      该目录若被清理，本脚本仍可运行，但只在 `reports/` 真归档上比对，语料会变少。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]      # scripts/ 的上一级 = 810_driver_test
sys.path.insert(0, str(ROOT / "common"))
import ztest_report as Z  # noqa: E402

NEW_RESULT, NEW_INFO = Z._NATIVE_RESULT_RE, Z._NATIVE_INFO_RE
LEGACY_RESULT = re.compile(
    r"I3C_I2C_TEST:(?:CTRL|TGT):(PASS|FAIL|SKIP):([^\s:]+)(?::(.*))?")
LEGACY_INFO = re.compile(r"I3C_I2C_TEST:(?:CTRL|TGT):INFO:([^\s:]+):(.*)")


def parse_with(result_re, info_re, lines):
    Z._NATIVE_RESULT_RE, Z._NATIVE_INFO_RE = result_re, info_re
    return Z.parse_ztest(lines)


def collect():
    files = []
    for base in (ROOT / "reports", ROOT / "tmp" / "a1" / "synth"):
        if base.is_dir():
            files += sorted(base.glob("**/serial_*.log"))
    return files


def main():
    files = collect()
    print(f"语料: {len(files)} 份 serial_*.log")
    changed = []
    for f in files:
        try:
            lines = f.read_text(encoding="utf-8", errors="replace").split("\n")
        except OSError:
            continue
        old = parse_with(LEGACY_RESULT, LEGACY_INFO, lines)
        new = parse_with(NEW_RESULT, NEW_INFO, lines)
        if old != new:
            only_new_tests = set(new["tests"]) - set(old["tests"])
            only_new_infos = set(new["infos"]) - set(old["infos"])
            lost_tests = set(old["tests"]) - set(new["tests"])
            lost_infos = set(old["infos"]) - set(new["infos"])
            same_rest = (all(old[k] == new[k] for k in
                             ("boot_seen", "role", "execution", "exec_conflict"))
                         and not lost_tests and not lost_infos)
            changed.append({
                "file": str(f.relative_to(ROOT)),
                "new_tests": sorted(only_new_tests),
                "new_infos": sorted(only_new_infos),
                "lost_tests": sorted(lost_tests),
                "lost_infos": sorted(lost_infos),
                "rest_same": same_rest,
            })
    print(f"输出有差异的归档: {len(changed)}")
    bad = 0
    for c in changed:
        ok = (c["rest_same"] and not c["lost_tests"] and not c["lost_infos"])
        bad += 0 if ok else 1
        print(f"  [{'ok ' if ok else 'BAD'}] {c['file']}")
        print(f"        新增 tests={c['new_tests']} infos={c['new_infos']}")
        if c["lost_tests"] or c["lost_infos"]:
            print(f"        !! 丢失 tests={c['lost_tests']} infos={c['lost_infos']}")

    # 定向补充：RTC_TEST / MBOX_TEST 形态必须被新白名单认领（旧版不认）
    extra = ["RTC_TEST:TGT:PASS:set_get_roundtrip",
             "RTC_TEST:TGT:INFO:alarm_fires:fire1:triggered=1",
             "MBOX_TEST:CTRL:FAIL:basic_ch4_frames:frames=0",
             "MBOX_TEST:TGT:PASS:basic_ch4_frames",
             "ZT_BOOT TGT", "PROJECT EXECUTION SUCCESSFUL"]
    o = parse_with(LEGACY_RESULT, LEGACY_INFO, extra)
    n = parse_with(NEW_RESULT, NEW_INFO, extra)
    print("\n定向: 旧 tests=", sorted(o["tests"]), " 新 tests=", sorted(n["tests"]))
    print("定向: 旧 infos=", sorted(o["infos"]), " 新 infos=", sorted(n["infos"]))
    expect = {"basic_ch4_frames", "set_get_roundtrip"}
    if set(n["tests"]) != expect or set(o["tests"]) != set():
        print("BAD: 定向期望不符")
        bad += 1

    # 还原
    Z._NATIVE_RESULT_RE, Z._NATIVE_INFO_RE = NEW_RESULT, NEW_INFO
    print(f"\nRESULT: {'ALL OK' if bad == 0 else str(bad) + ' PROBLEM(S)'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
