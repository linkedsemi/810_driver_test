import serial, sys, time, datetime

port = sys.argv[1] if len(sys.argv) > 1 else "COM16"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 45.0
outdir = r"D:/zephyr4_0/810_driver_test/reports/2026-10-15-2"
raw = open(outdir + "/serial_%s_raw.log" % port, "wb")
stamp = open(outdir + "/serial_%s_timestamped.log" % port, "w", encoding="utf-8")

s = serial.Serial(port, 115200, timeout=0.2)
t0 = time.time()
buf = b""
total = 0
while time.time() - t0 < secs:
    d = s.read(4096)
    if not d:
        continue
    raw.write(d); raw.flush(); total += len(d)
    buf += d
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        stamp.write("[%s] %s\n" % (ts, line.decode("utf-8", "replace").rstrip("\r")))
        stamp.flush()
if buf:
    ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    stamp.write("[%s] %s\n" % (ts, buf.decode("utf-8", "replace").rstrip("\r")))
print("captured bytes:", total)
s.close(); raw.close(); stamp.close()
