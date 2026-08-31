#!/usr/bin/env bash
# 环境预检固件构建脚本（controller 核，board cpu2 = lsqsh_evb@2os/lsqsh/cpu2）。
#
# 仅构建（build-only）：预检固件的加载运行统一走 810_driver_test/regression/precheck/
# run_precheck.py（单遍双核 gdb load + 双串口采集），本脚本不经硬件。
#
# Build artifacts:
#   precheck/fw/cpu2_controller/build/zephyr/zephyr.bin   # cpu2 RAM 固件
#
# Usage:
#   ./test.sh          # west build -p auto（构建到 ./build/）

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BOARD="${CONTROLLER_BOARD:-lsqsh_evb@2os/lsqsh/cpu2}"

echo "[precheck/controller] west build -p auto -b ${BOARD} -> ${SCRIPT_DIR}/build"
west build -p auto -d "${SCRIPT_DIR}/build" -b "${BOARD}" "${SCRIPT_DIR}"
echo "[precheck/controller] build OK: ${SCRIPT_DIR}/build/zephyr/zephyr.bin"
