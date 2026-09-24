#!/usr/bin/env python3
"""report_dir 离线单元测试（无硬件依赖）。

Run: python 810_driver_test/common/test_report_dir.py
"""
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from report_dir import (allocate_session_dir, case_dir_for,  # noqa: E402
                        read_summary, update_summary)


def main():
    root = Path(tempfile.mkdtemp())
    s1 = allocate_session_dir(root, "2026-08-29")
    assert s1.name == "2026-08-29-1"
    s2 = allocate_session_dir(root, "2026-08-29")
    assert s2.name == "2026-08-29-2"
    cd = case_dir_for(s1, "trng_test", "case_default")
    assert cd.name == "trng_test_case_default" and cd.is_dir()
    update_summary(s1, "trng_test_case_default", "PASS", 9, 9,
                   "trng_test_case_default/trng_test_report.md")
    rows = read_summary(s1)
    assert rows["trng_test_case_default"] == {
        "report": "trng_test_case_default/trng_test_report.md",
        "result": "PASS", "passed": 9, "total": 9}
    update_summary(s1, "rtc_test_case_default", "FAIL", 7, 9,
                   "rtc_test_case_default/rtc_test_report.md")
    rows = read_summary(s1)
    assert set(rows) == {"trng_test_case_default", "rtc_test_case_default"}
    assert rows["rtc_test_case_default"]["result"] == "FAIL"

    # --- O-38 回归：结果是**可视标记**，回读要剥掉；且整文件重写不得抹掉末尾注记 ---
    # (a) 标记不污染 result（否则计数/判定会被带偏）
    assert all(v["result"] in ("PASS", "FAIL") for v in rows.values()), \
        "结果列的可视标记没被 read_summary 剥掉"
    # (b) 末尾注记（别的模块写的，如 failures 指针行）在 update_summary 后必须还在 ——
    #     本函数是整文件重写，不显式保留就是静默删除（2026-09-24 实证过一次）
    p = s1 / "summary.md"
    p.write_text(p.read_text(encoding="utf-8") + "\n> 失败归因：见 `failures/`\n",
                 encoding="utf-8")
    update_summary(s1, "trng_test_case_default", "PASS", 9, 9,
                   "trng_test_case_default/trng_test_report.md")
    assert "失败归因" in p.read_text(encoding="utf-8"), \
        "update_summary 抹掉了末尾注记（O-38 复发）"

    print("ALL TESTS PASS")


if __name__ == "__main__":
    main()
