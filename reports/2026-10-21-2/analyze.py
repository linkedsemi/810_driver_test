#!/usr/bin/env python3
"""Analyze dbg_run.py --raw logs: arrival gap distribution of ERR lines.

Usage: python -I analyze.py <raw.log> [...]
Only the TGT raw section is parsed. Timestamps are PC-side arrival times
recorded by dbg_run.py reader() when a complete line is split off, so they
are quantized by serial read chunks (typ. ~10-25 ms).
"""
import re
import sys
import statistics

TS_RE = re.compile(r"^\+\s*([0-9]+\.[0-9]+)\s{2}")
ERR_RE = re.compile(r"beyond whitelist")
BEGIN_RE = re.compile(r"SCAN_BURST_BEGIN")
END_RE = re.compile(r"SCAN_BURST_END")
DROP_RE = re.compile(r"---\s*(\d+)\s*messages dropped\s*---")


def parse(path):
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    # locate TGT RAW section
    start = None
    end = len(lines)
    for i, l in enumerate(lines):
        m = re.match(r"===== RAW (\S+) \((\w+|\?)\) =====", l)
        if m:
            if m.group(2) == "TGT" and start is None:
                start = i + 1
            elif start is not None:
                end = i
                break
    if start is None:
        start = 0  # allow section-less input
    rows = []
    for l in lines[start:end]:
        m = TS_RE.match(l)
        if not m:
            continue
        rows.append((float(m.group(1)), l))
    return rows


def report(path):
    rows = parse(path)
    err = [(t, l) for t, l in rows if ERR_RE.search(l)]
    beg = [(t, l) for t, l in rows if BEGIN_RE.search(l)]
    end = [(t, l) for t, l in rows if END_RE.search(l)]
    drops = [(t, int(m.group(1))) for t, l in rows if (m := DROP_RE.search(l))]
    gaps = []
    for i in range(1, len(err)):
        gaps.append((err[i][0] - err[i - 1][0]) * 1000.0)  # ms
    print(f"== {path}")
    print(f"  err_lines={len(err)} begin={len(beg)} end={len(end)}")
    for t, l in beg:
        print(f"    BEGIN @+{t:.3f}  {l.split('uart:~$ ')[-1][:90]}")
    for t, l in end:
        print(f"    END   @+{t:.3f}  {l.split('uart:~$ ')[-1][:90]}")
    if beg and end:
        print(f"  span(first BEGIN arrival -> last END arrival)={end[-1][0]-beg[0][0]:.3f}s")
        print(f"  mean_gap_over_span={1000*(end[-1][0]-beg[0][0])/max(1,len(err)-1):.2f}ms")
    if gaps:
        s = sorted(gaps)
        p50 = statistics.median(gaps)
        p90 = s[int(0.9 * (len(s) - 1))]
        print(f"  ALL-GAPS(include inter-segment): min={min(gaps):.1f} p50={p50:.1f} "
              f"p90={p90:.1f} max={max(gaps):.1f} mean={statistics.mean(gaps):.1f}")
        for th in (50, 80, 150):
            print(f"    gap>{th}ms: {sum(1 for g in gaps if g > th)}")
        print(f"  first20={[round(g,1) for g in gaps[:20]]}")
        print(f"  last20={[round(g,1) for g in gaps[-20:]]}")

    # ---- per-segment (between BEGIN_i and END_i) ----
    segs = []
    for i, (tb, lb) in enumerate(beg):
        te = end[i][0] if i < len(end) else None
        if te is None:
            continue
        e = [t for t, _ in err if tb <= t <= te]
        seg_gaps = [(e[k] - e[k - 1]) * 1000.0 for k in range(1, len(e))]
        segs.append((lb, tb, te, e, seg_gaps))
    pooled = []
    for lb, tb, te, e, sg in segs:
        pooled.extend(sg)
        mode = re.search(r"mode=(\w+)", lb)
        mode = mode.group(1) if mode else "?"
        s = sorted(sg) if sg else [0]
        print(f"  [seg {mode}] n={len(e)} span={te-tb:.3f}s "
              f"mean_gap={statistics.mean(sg) if sg else 0:.1f}ms "
              f"p50={statistics.median(sg) if sg else 0:.1f} "
              f"p90={s[int(0.9*(len(s)-1))]:.1f} max={max(sg) if sg else 0:.1f}")
        for th in (50, 80, 150):
            print(f"     gap>{th}ms: {sum(1 for g in sg if g > th)}")
        if mode in ("write1",):
            print(f"     first20={[round(g,1) for g in sg[:20]]}")
            print(f"     last20={[round(g,1) for g in sg[-20:]]}")
    if pooled:
        s = sorted(pooled)
        p90 = s[int(0.9 * (len(s) - 1))]
        print(f"  INTRA-SEGMENT POOLED: n={len(pooled)} min={min(pooled):.1f} "
              f"p50={statistics.median(pooled):.1f} p90={p90:.1f} max={max(pooled):.1f} "
              f"mean={statistics.mean(pooled):.1f}")
        for th in (50, 80, 150):
            print(f"    gap>{th}ms: {sum(1 for g in pooled if g > th)}")
    if drops:
        print(f"  dropped notices: {[(round(t,3), n) for t, n in drops]} "
              f"total={sum(n for _, n in drops)}")
    # position of drop notice relative to err lines
    if drops:
        tdrop = drops[0][0]
        before = sum(1 for t, _ in err if t < tdrop)
        print(f"  first drop notice after {before} err lines")
    print()


if __name__ == "__main__":
    for p in sys.argv[1:]:
        report(p)
