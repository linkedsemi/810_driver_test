#!/usr/bin/env bash
# app_iwdg_cpu2_test - case_reset 执行脚本（破坏性：真实复位 cpu2）
#
# ⚠ 未实机校准：按 SPEC_REFERENCE §6.7（XIP 固化与启动测试）+ wdt_test
#   case_destructive_reset 的 noinit 状态机流程编写，**首次实机运行前**
#   须核对以下实机相关项（P6）：
#     1. CPU1 侧是否需改用 @2os_xip 变体构建（§6.7.4：XIP 标准组合 =
#        cpu1 XIP + cpu2 标准 2os RAM bin）；本脚本当前用两侧标准 2os 构建
#        的 flash 段镜像，若 bootrom 不引导则按 §6.7.4 换 CPU1 构建变体。
#     2. CTRL_UART（cpu2 控制台 uart5，MCP2200）实机端口号。
#     3. DebugServerConsole.exe 路径。
#     4. noinit 符号地址在 flash 引导路径下是否与 RAM-load 一致。
#
# 流程：
#   1. 构建两核固件
#   2. 一次会话烧两个 bin：cpu1 @0x08000000、cpu2 @0x08100000（§6.7.3）
#   3. 起 DebugServer，用 gdb 清 test_phase / phase0_passed
#   4. 关 DebugServer，触发复位并采集 uart5
#
# 判定：uart5 出现 APP_IWDG_RESET_PASS 且 PROJECT EXECUTION SUCCESSFUL。
#
# Usage:  bash run_reset.sh

set -e

CDIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${CDIR}/../.." && pwd)"
TEST_ROOT="$(cd "${PROJECT_ROOT}/../.." && pwd)"

FP="${TEST_ROOT}/scripts/flash_program.py"
DS_EXE="${DS_EXE:-C:/uaer_app/debug_server/bin/DebugServerConsole.exe}"
CTRL_UART="${CTRL_UART:-${I3C_CTRL_UART:-COM15}}"

BUILD="${PROJECT_ROOT}/tests/case_reset/build"
CPU1_FLASH_BIN="${BUILD}/target/zephyr/zephyr_flash_bram_xip_lsqsh_evb_2os_lsqsh_cpu1.bin"
CPU2_FLASH_BIN="${BUILD}/controller/zephyr/zephyr_flash_lsqsh_evb_2os_lsqsh_cpu2.bin"
GDB=riscv64-unknown-elf-gdb
NM=riscv64-unknown-elf-nm

echo "=== 1. 构建固件 ==="
cd "$CDIR"
I3C_TEST_ROOT="${PROJECT_ROOT}" bash test.sh 2>&1 | grep -E "Build OK|error|Error" | tail -3

if [ ! -f "$CPU1_FLASH_BIN" ]; then
    echo "ERROR: cpu1 flash 镜像不存在: $CPU1_FLASH_BIN"
    echo "       （§6.7.4：XIP 组合下 cpu1 可能需用 lsqsh_evb@2os_xip 构建）"
    exit 1
fi
if [ ! -f "$CPU2_FLASH_BIN" ]; then
    echo "ERROR: cpu2 flash 镜像不存在: $CPU2_FLASH_BIN"
    ls -1 "${BUILD}/controller/zephyr/" | grep -i "\.bin$" || true
    exit 1
fi

echo ""
echo "=== 2. 烧录（一次会话两个 bin，§6.7.3） ==="
taskkill /F /IM DebugServerConsole.exe 2>/dev/null || true
sleep 2
python "$FP" --bin "${CPU1_FLASH_BIN}@0x08000000" --bin "${CPU2_FLASH_BIN}@0x08100000" --verify 2>&1 | tail -3

echo ""
echo "=== 3. 起 DebugServer 并清 noinit ==="
"$DS_EXE" > /tmp/ds_iwdg.log 2>&1 &
sleep 8

PHASE_ADDR=$(${NM} "${BUILD}/controller/zephyr/zephyr.elf" | grep " test_phase$" | awk '{print $1}')
PHASE0_ADDR=$(${NM} "${BUILD}/controller/zephyr/zephyr.elf" | grep " phase0_passed$" | awk '{print $1}')
echo "test_phase@0x${PHASE_ADDR}, phase0_passed@0x${PHASE0_ADDR}"

"$GDB" -batch \
  -ex "target remote 127.0.0.1:1025" \
  -ex "set {int}0x${PHASE_ADDR} = 0" \
  -ex "set {int}0x${PHASE0_ADDR} = 0" \
  -ex "detach" 2>&1 | tail -2
echo "noinit 清零 (test_phase=0)"

echo ""
echo "=== 4. 关 DebugServer + 复位启动并采集 uart5 ==="
taskkill /F /IM DebugServerConsole.exe 2>/dev/null || true
sleep 2

python - "$CTRL_UART" "$CPU1_FLASH_BIN" "$CPU2_FLASH_BIN" "$FP" <<'PY'
import sys, time, threading, subprocess
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import serial

uart, bin1, bin2, fp = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

ser = serial.Serial(uart, 115200, timeout=0.1)
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

r = subprocess.run(["python", fp,
                    "--bin", f"{bin1}@0x08000000",
                    "--bin", f"{bin2}@0x08100000",
                    "--reset-and-run"], capture_output=True, text=True, timeout=90)
print("reset:", (r.stdout or r.stderr).strip().splitlines()[-1])

# Phase0 2s + app_iwdg 2s 超时 + 复位 + Phase1，留足余量
time.sleep(20)
stop.set()
ser.close()

text = data.decode("utf-8", errors="replace")
import re
text = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", text)
print(f"--- captured {len(data)} bytes from {uart} ---")
for line in text.splitlines():
    if line.strip():
        print(line.strip())

ok = ("APP_IWDG_RESET_PASS" in text) and ("PROJECT EXECUTION SUCCESSFUL" in text)
print("\n=== case_reset:", "PASS" if ok else "FAIL", "===")
sys.exit(0 if ok else 1)
PY
