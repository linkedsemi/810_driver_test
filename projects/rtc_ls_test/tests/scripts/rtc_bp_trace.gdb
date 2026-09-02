# RTC set/get 断点追踪：观测固件运行态每次调用时 CURTIME/CTRL 寄存器
# 用法: riscv64-unknown-elf-gdb -batch -x rtc_bp_trace.gdb（需先启动 DebugServer）
set confirm off
set pagination off
file d:/zephyr4_0/810_driver_test/projects/rtc_test/tests/case_default/build/target/zephyr/zephyr.elf
target remote 127.0.0.1:1025

break rtc_ls_set_time
commands 1
silent
printf "HIT set_time @%08x: CTRL=%08x CUR=%08x CURCAL=%08x\n", $pc, *(unsigned int*)0x4006fc00, *(unsigned int*)0x4006fc10, *(unsigned int*)0x4006fc14
continue
end

break rtc_ls_get_time
commands 2
silent
printf "HIT get_time @%08x: CTRL=%08x CUR=%08x CURCAL=%08x\n", $pc, *(unsigned int*)0x4006fc00, *(unsigned int*)0x4006fc10, *(unsigned int*)0x4006fc14
continue
end

break rtc_ls_alarm_set_time
commands 3
silent
printf "HIT alarm_set @%08x: CTRL=%08x TIME=%08x CUR=%08x\n", $pc, *(unsigned int*)0x4006fc00, *(unsigned int*)0x4006fc08, *(unsigned int*)0x4006fc10
continue
end

reset
load
set $pc=0x100c0000
continue
