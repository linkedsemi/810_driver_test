#!/usr/bin/env python3
"""从 case_abl 采集日志里算消融指标。

指标（口径 = 任务书）：
  - 日志行数   = SCAN_BURST_BEGIN 与 SCAN_BURST_END 之间的 `address beyond whitelist` 行数
  - 排空跨度   = END 到达时刻 - BEGIN 到达时刻（+秒数前缀）
  - 每行耗时   = 排空跨度 / 日志行数
  - 另计 "i2c bus timeout" / "try recovery" 次数（复用既有统计口径）

用法: python parse_abl.py <log> [<log2> ...]
"""
import re
import sys

MARK_BEGIN = re.compile(r"SCAN_BURST_BEGIN")
MARK_END = re.compile(r"SCAN_BURST_END")
ERR = re.compile(r"address beyond whitelist")
TS = re.compile(r"^\+\s*([0-9]+\.[0-9]+)")


def parse(path):
    begin_t = end_t = None
    err_between = 0
    bus_to = 0
    try_rec = 0
    n_lines = 0
    in_span = False
    for ln in open(path, encoding="utf-8", errors="replace"):
        m = TS.match(ln)
        t = float(m.group(1)) if m else None
        if MARK_BEGIN.search(ln):
            begin_t = t
            in_span = True
            continue
        if MARK_END.search(ln):
            end_t = t
            in_span = False
            continue
        if in_span and ERR.search(ln):
            err_between += 1
        if "i2c bus timeout" in ln:
            bus_to += 1
        if "try recovery" in ln:
            try_rec += 1
        n_lines += 1
    span = (end_t - begin_t) if (begin_t is not None and end_t is not None) else None
    per = (span / err_between) if (span is not None and err_between) else None
    return dict(file=path, begin=begin_t, end=end_t, span=span,
                err_lines=err_between, per_line_ms=(per * 1000.0) if per else None,
                bus_timeout=bus_to, try_recovery=try_rec, total_lines=n_lines)


def main():
    for path in sys.argv[1:]:
        r = parse(path)
        name = path.replace("\\", "/").split("/")[-1]
        span = f"{r['span']:.3f}s" if r["span"] is not None else "n/a"
        per = f"{r['per_line_ms']:.2f}ms" if r["per_line_ms"] is not None else "n/a"
        print(f"{name:34s} lines={r['err_lines']:4d} span={span:>9s} per_line={per:>9s} "
              f"bus_timeout={r['bus_timeout']} try_recovery={r['try_recovery']}")


if __name__ == "__main__":
    main()
