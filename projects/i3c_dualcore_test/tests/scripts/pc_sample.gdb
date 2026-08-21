python

import gdb
import time

gdb.execute("set confirm off")
gdb.execute("set pagination off")
gdb.execute("set target-async on")
gdb.execute("target remote localhost:1025")
gdb.execute("file case_pure_i3c_12m5/build/target/zephyr/zephyr.elf")

def flash_and_run(run_secs):
    gdb.execute("reset")
    gdb.execute("load")
    gdb.execute("restore case_pure_i3c_12m5/build/controller/zephyr/zephyr.bin binary 0x10000000")
    gdb.execute("set $pc=0x100c0000")
    gdb.execute("continue &")
    time.sleep(run_secs)

# pass 1: prime I-cache with the new image (run past the test)
flash_and_run(3.0)
time.sleep(0.5)

# pass 2: run and sample pc inside the stall window
gdb.execute("reset")
gdb.execute("load")
gdb.execute("restore case_pure_i3c_12m5/build/controller/zephyr/zephyr.bin binary 0x10000000")
gdb.execute("set $pc=0x100c0000")
gdb.execute("continue &")
time.sleep(1.535)
gdb.execute("interrupt")
time.sleep(0.3)
try:
    f = gdb.selected_frame()
    print("SAMPLED PC = 0x%x" % f.pc())
    gdb.execute("x/5i $pc")
    gdb.execute("p/x $mstatus")
    gdb.execute("bt 4")
except gdb.error as e:
    print("SAMPLE FAILED:", e)
gdb.execute("detach")

end
