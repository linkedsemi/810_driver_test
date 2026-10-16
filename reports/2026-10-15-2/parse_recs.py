import re, sys, collections

path = sys.argv[1] if len(sys.argv) > 1 else r"D:/zephyr4_0/810_driver_test/reports/2026-10-15-2/gdb_read_ram_d.log"
words = []
for line in open(path, encoding="utf-8", errors="replace"):
    m = re.search(r"<wave_recs[^>]*>:\s*(.*)", line)
    if m:
        for w in m.group(1).split():
            words.append(int(w, 16))

# struct (9 words): cyc, ret_w, isr_w, ret_r, isr_r, rd, idle_w, idle_r, period_ms
def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v

W = 9
n = len(words) // W
print("records parsed:", n, " words:", len(words))
rows = []
for k in range(n):
    cyc, ret_w, isr_w, ret_r, isr_r, rd, iw, ir, per = words[W*k:W*k+W]
    rows.append((cyc, s32(ret_w), isr_w, s32(ret_r), isr_r, rd, iw, ir, per))

agg = collections.defaultdict(lambda: collections.defaultdict(set))
cnt = collections.Counter()
for cyc, ret_w, isr_w, ret_r, isr_r, rd, iw, ir, per in rows:
    ph = cyc & 1
    a = agg[ph]
    cnt[ph] += 1
    a["Wret"].add(ret_w); a["Wisr"].add(isr_w)
    a["Rret"].add(ret_r); a["Risr"].add(isr_r)
    a["rd"].add(rd); a["idle_w"].add(iw); a["idle_r"].add(ir); a["per"].add(per)

for ph in sorted(agg):
    a = agg[ph]
    name = "A(0x33 授权)" if ph == 0 else "B(0x33 未授权)"
    print(f"phase {name}: n={cnt[ph]}")
    print(f"   W ret={sorted(a['Wret'])} isr={sorted(a['Wisr'])}")
    print(f"   R ret={sorted(a['Rret'])} isr={sorted(a['Risr'])} "
          f"rd={[hex(x) for x in sorted(a['rd'])]}")
    print(f"   idle_w(scl,sda)={[bin(x) for x in sorted(a['idle_w'])]} "
          f"idle_r={[bin(x) for x in sorted(a['idle_r'])]}")
    print(f"   period={sorted(a['per'])}")

print("\nfirst 8 records (cyc,Wret,Wisr,Rret,Risr,rd,idle_w,idle_r,per):")
for r in rows[:8]:
    print("  ", r[:5], "rd=" + hex(r[5]), "iw=" + bin(r[6]), "ir=" + bin(r[7]), r[8])
