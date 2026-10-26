#!/usr/bin/env bash
# cpu1_iwdg destructive reset test - 触发 sec_iwdg 硬件复位
#
# 流程：
#   1. 构建普通 2os 固件（RAM 地址 0x100c0000）
#   2. 烧录到 flash（bootrom 会自动引导）
#   3. 触发硬件复位，bootrom 加载固件运行
#   4. 固件在 500ms 超时后复位，复位后检查 reset_reason
#
# 注意：
#   - 本 case 会触发真实的 sec_iwdg 硬件复位（系统完全重启）
#   - 需要 noinit 段记录 phase 状态机（跨复位）
#   - 需要单独运行，不进 run_all.py 批量回归
#
# Usage:
#   ./test.sh          # build firmware
#   ./test.sh -f       # flash to board (需要 DebugServer 先杀)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_destructive_reset"

AUTO_GDB=""
FLASH_MODE=""
while [ $# -gt 0 ]; do
    case "$1" in
        -f) FLASH_MODE="y"; shift ;;
        *)  echo "Unknown option: $1"; exit 1 ;;
    esac
done

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_wdt_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
TARGET_CONF="$(realpath "${TARGET_SRC}/prj.conf")"
TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_BUILD="${TEST_BUILD_DIR}/target"
TARGET_BIN="${TARGET_BUILD}/zephyr/zephyr.bin"

echo "Building destructive reset firmware (2os, flash boot via bootrom)..."
rm -rf "${TEST_BUILD_DIR}"

# 构建普通 2os 固件（地址在 RAM，但会被 bootrom 加载到 flash 执行）
west build -p always -b lsqsh_evb@2os/lsqsh/cpu1 \
    "${TARGET_SRC}" \
    -d "${TARGET_BUILD}" \
    -- -DDTC_OVERLAY_FILE="${TARGET_OVERLAY}" \
       -DWDT_DESTRUCTIVE_RESET=y

echo "Build OK: ${TARGET_BIN}"
echo ""
echo "烧录到 flash（bootrom 会自动引导启动）:"
echo "  python ${TEST_ROOT}/../scripts/flash_program.py \\"
echo "    --bin ${TARGET_BIN}@0x08000000 --verify"
echo ""
echo "烧录后手动复位或断电重启，固件会自动执行破坏性测试"

