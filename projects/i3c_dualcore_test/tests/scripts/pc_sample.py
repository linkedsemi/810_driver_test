#!/usr/bin/env python3
"""Sample CPU1 $pc mid-stall: 2-pass flash, run, interrupt inside the 8ms window.

Stall: IBI EVENT (uptime ~1.531s) + ~20us .. +8.07ms. Interrupt at ~1.535s
=> ~4ms into the stall. Repeat 3x.
"""
import subprocess
import time

GDB = "riscv64-unknown-elf-gdb"
ELF = "case_pure_i3c_12m5/build/target/zephyr/zephyr.elf"
CTRL_BIN = "case_pure_i3c_12m5/build/controller/zephyr/zephyr.bin"

PY = r"""
import gdb
import time

gdb.execute("set confirm off")
gdb.execute("set pagination off")
gdb.execute("set target-async on")
gdb.execute("target remote localhost:1025")
gdb.execute("file %s")

def flash_and_run(run_secs):
    gdb.execute("reset")
    gdb.execute("load")
    gdb.execute("restore %s binary 0x10000000")
    gdb.execute("set $pc=0x100c0000")
    gdb.execute("continue &")
    time.sleep(run_secs)

# pass 1: prime I-cache with the new image (run past the test)
flash_and_run(3.0)
time.sleep(0.5)

# pass 2: run and sample pc inside the stall window
gdb.execute("reset")
gdb.execute("load")
gdb.execute("restore %s binary 0x10000000")
gdb.execute("set $pc=0x100c0000")
gdb.execute("continue &")
time.sleep(1.535)
gdb.execute("interrupt")
time.sleep(0.3)
try:
    f = gdb.selected_frame()
    print("SAMPLED PC = 0x%%x" %% f.pc())
    gdb.execute("x/5i $pc")
    gdb.execute("p/x $mstatus")
    gdb.execute("bt 4")
except gdb.error as e:
    print("SAMPLE FAILED:", e)
gdb.execute("detach")
""" % (ELF, CTRL_BIN, CTRL_BIN)


def main():
    with open("pc_sample.gdb", "w") as fh:
        fh.write("python\n" + PY + "\nend\n")
    for i in range(3):
        print(f"=== sample {i} ===", flush=True)
        r = subprocess.run([GDB, "-batch", "-x", "pc_sample.gdb"],
                           capture_output=True, text=True, timeout=180)
        for line in (r.stdout + r.stderr).splitlines():
            if any(k in line for k in ("SAMPLED", "=>", "mstatus", "#0", "#1",
                                       "#2", "FAILED", "Remote", "error")):
                print(line)
        time.sleep(1)


if __name__ == "__main__":
    main()
