#!/usr/bin/env bash
# case_pmu_iwdg_full_reset 端到端自动跑法（构建 -> flash 固化 -> 清 noinit -> 复位启动 -> 采集）
#
# ⚠ 本 case 触发 pmu_iwdg 整芯片复位，会连带复位另一核与全部外设。
#   必须单独运行，不要在批量回归 / 其它 case 运行时并发执行。
#
# 环境变量（不设则用默认值）：
#   TGT_UART     cpu1 目标串口（默认 COM16；见 810_driver_test/HARDWARE_SETUP.md）
#   DS_EXE       DebugServerConsole.exe 路径
#   CAPTURE_SEC  采集秒数（默认 18：Phase0 2s + 复位 + Phase1）
#
# 说明：本脚本是 case 自带的独立运行入口（同 case_destructive_reset/run_destructive.sh），
#       P1b 阶段只生成不运行；首次实机运行（P6）时按实机校准 UART / DS_EXE。

set -e

CDIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${CDIR}/../.." && pwd)"
WORKSPACE_ROOT="$(cd "${TEST_ROOT}/.." && pwd)"

: "${TGT_UART:=COM16}"
: "${DS_EXE:=C:/uaer_app/debug_server/bin/DebugServerConsole.exe}"
: "${CAPTURE_SEC:=18}"

FP="${WORKSPACE_ROOT}/scripts/flash_program.py"
TARGET_BUILD="${CDIR}/build/target"
FLASH_BIN="${TARGET_BUILD}/zephyr/zephyr_flash_bram_xip_lsqsh_evb_2os_lsqsh_cpu1.bin"
ELF="${TARGET_BUILD}/zephyr/zephyr.elf"

echo "=== 1. 构建固件 ==="
(I3C_TEST_ROOT="$(cd "${TEST_ROOT}/../.." && pwd)" bash "${CDIR}/test.sh" 2>&1 \
    | grep -E "Build OK|error:|Error|FAILED" | tail -5) || true
[ -f "${FLASH_BIN}" ] || { echo "ERROR: flash bin 未生成: ${FLASH_BIN}"; exit 1; }

# noinit 段符号（跨复位状态机）
PHASE_ADDR=$(riscv64-unknown-elf-nm "${ELF}" | grep " pmu_test_phase$" | awk '{print $1}')
PHASE0_ADDR=$(riscv64-unknown-elf-nm "${ELF}" | grep " pmu_phase0_passed$" | awk '{print $1}')
echo "pmu_test_phase@0x${PHASE_ADDR}, pmu_phase0_passed@0x${PHASE0_ADDR}"

echo ""
echo "=== 2. 烧录固件到 flash ==="
taskkill /F /IM DebugServerConsole.exe 2>/dev/null || true
sleep 2
python "${FP}" --bin "${FLASH_BIN}@0x08000000" --verify 2>&1 | tail -2

echo ""
echo "=== 3. 启动 DebugServer 并清 noinit ==="
"${DS_EXE}" > /tmp/ds_pmu.log 2>&1 &
sleep 8
riscv64-unknown-elf-gdb -batch \
  -ex "target remote 127.0.0.1:1025" \
  -ex "set {int}0x${PHASE_ADDR} = 0" \
  -ex "set {int}0x${PHASE0_ADDR} = 0" \
  -ex "detach" 2>&1 | tail -2
echo "noinit 清零 (pmu_test_phase=0)"

echo ""
echo "=== 4. 关 DebugServer + 复位启动并采集 ==="
taskkill /F /IM DebugServerConsole.exe 2>/dev/null || true
sleep 2

python -c "
import sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
import serial, time, subprocess, threading, os, re

ser = serial.Serial('${TGT_UART}', 115200, timeout=0.1)
data = bytearray()
stop = threading.Event()

def reader():
    while not stop.is_set():
        try:
            d = ser.read(4096)
            if d:
                data.extend(d)
        except Exception:
            break

threading.Thread(target=reader, daemon=True).start()

fp = subprocess.run(['python', '${FP}',
    '--bin', '${FLASH_BIN}@0x08000000', '--reset-and-run'],
    capture_output=True, text=True, timeout=60)
print('reset:', (fp.stdout or fp.stderr).strip().split(chr(10))[-1])

time.sleep(${CAPTURE_SEC})
stop.set()
ser.close()

text = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', data.decode('utf-8', errors='replace'))
print(f'--- captured {len(data)} bytes ---')
for line in text.split(chr(10)):
    if line.strip():
        print(line.strip())
"
