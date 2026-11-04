#!/usr/bin/env bash
# 自动执行 wdt 破坏性复位测试（全程软件，无人为干预）
set -e

CDIR="d:/zephyr4_0/810_driver_test/projects/sec_iwdg_cpu1_test/tests/case_destructive_reset"
FP="d:/zephyr4_0/810_driver_test/scripts/flash_program.py"

echo "=== 1. 构建固件 ==="
cd "$CDIR"
rm -rf build
I3C_TEST_ROOT="$(cd ../.. && pwd)" bash test.sh 2>&1 | grep -E "Build OK|error|Error" | tail -3

# 找 test_phase 地址
PHASE_ADDR=$(riscv64-unknown-elf-nm build/target/zephyr/zephyr.elf | grep " test_phase$" | awk '{print $1}')
PHASE0_ADDR=$(riscv64-unknown-elf-nm build/target/zephyr/zephyr.elf | grep " phase0_passed$" | awk '{print $1}')
echo "test_phase@0x$PHASE_ADDR, phase0_passed@0x$PHASE0_ADDR"

echo ""
echo "=== 2. 烧录固件 ==="
# 杀 DebugServer 独占 CKLink
taskkill /F /IM DebugServerConsole.exe 2>/dev/null || true
sleep 2
python "$FP" --bin "build/target/zephyr/zephyr_flash_bram_xip_lsqsh_evb_2os_lsqsh_cpu1.bin@0x08000000" --verify 2>&1 | tail -2

echo ""
echo "=== 3. 启动 DebugServer 并清 noinit ==="
"C:/uaer_app/debug_server/bin/DebugServerConsole.exe" > /tmp/ds.log 2>&1 &
DS_PID=$!
sleep 8
riscv64-unknown-elf-gdb -batch \
  -ex "target remote 127.0.0.1:1025" \
  -ex "set {int}0x${PHASE_ADDR} = 0" \
  -ex "set {int}0x${PHASE0_ADDR} = 0" \
  -ex "detach" 2>&1 | tail -2
echo "noinit 清零 (test_phase=0)"

echo ""
echo "=== 4. 关 DebugServer + 触发复位并捕获 ==="
taskkill /F /IM DebugServerConsole.exe 2>/dev/null || true
sleep 2

python -c "
import sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
import serial, time, subprocess, threading, os

ser = serial.Serial('COM10', 115200, timeout=0.1)
data = bytearray()
stop = threading.Event()
def reader():
    while not stop.is_set():
        try:
            d = ser.read(4096)
            if d: data.extend(d)
        except Exception: break
threading.Thread(target=reader, daemon=True).start()

os.chdir('$CDIR')
fp = subprocess.run(['python', '$FP',
    '--bin', 'build/target/zephyr/zephyr_flash_bram_xip_lsqsh_evb_2os_lsqsh_cpu1.bin@0x08000000',
    '--reset-and-run'], capture_output=True, text=True, timeout=60)
print('reset:', (fp.stdout or fp.stderr).strip().split(chr(10))[-1])

# 捕获 18s（PHASE0 2s + WDT 复位 + PHASE1）
time.sleep(18)
stop.set()
ser.close()
text = data.decode('utf-8', errors='replace')
import re
text = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text)
print(f'--- captured {len(data)} bytes ---')
for l in text.split(chr(10)):
    ls = l.strip()
    if ls:
        print(ls)
"
