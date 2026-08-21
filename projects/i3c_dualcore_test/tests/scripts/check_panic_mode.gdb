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

# pass 1: prime I-cache
flash_and_run(3.0)
time.sleep(0.5)

# pass 2: run, interrupt, read panic_mode
gdb.execute("reset")
gdb.execute("load")
gdb.execute("restore case_pure_i3c_12m5/build/controller/zephyr/zephyr.bin binary 0x10000000")
gdb.execute("set $pc=0x100c0000")
gdb.execute("continue &")
time.sleep(1.6)   # ~IBI 附近 / 死窗内
gdb.execute("interrupt")
time.sleep(0.3)
try:
    print("=== PANIC MODE CHECK ===")
    print("panic_mode =", gdb.parse_and_eval("panic_mode"))
    print("buffered_cnt =", gdb.parse_and_eval("buffered_cnt"))
    print("proc_tid =", gdb.parse_and_eval("proc_tid"))
    f = gdb.selected_frame()
    print("PC = 0x%x" % f.pc())
    gdb.execute("x/3i $pc")
except gdb.error as e:
    print("READ FAILED:", e)
gdb.execute("continue &")
time.sleep(1.0)
gdb.execute("detach")
end
