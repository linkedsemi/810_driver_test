#!/usr/bin/env bash
# MBOX dual-core test
#
# What is tested:
#   LinkedSemi mbox driver: ch4/ch5 bidirectional, multi-channel isolation, MTU boundary.
#
# CPU1 (target, uart1): 等待接收消息，逐帧校验 pattern/序号/条数，回复确认。
# CPU2 (controller, uart5): 主动发送消息，统计发送失败数。
#
# Normal behavior: 两核均打印 PROJECT EXECUTION SUCCESSFUL；
#                  INFO basic_ch4_frames / basic_ch5_frames / basic_mtu_full 全为 PASS。
# Abnormal behavior: 任一核打印 PROJECT EXECUTION FAILED —— 例如 send 全部失败
#                  （controller 判负）或对端一条都收不到（target 计数 0 ≠ 契约值）。
#                  ★ 修复前该 case 是空壳：两个入口都无条件打 SUCCESSFUL，
#                  `mbox_test_verify()` 定义了却零调用 ⇒ 驱动全坏也判 PASS。
#
# Feedback: 串口逐项行 `MBOX_TEST:<core>:(PASS|FAIL):<name>[:<明细>]`（判据见 tests/COVERAGE.md §2/§7）。
#
# Hardware IO connections:
#   无外部接线（mbox 走共享 SRAM，片内）。
#
# Usage:
#   ./test.sh          # build only (both cores)
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_mbox_basic"

AUTO_GDB=""
while [ $# -gt 0 ]; do
    case "$1" in
        -g) AUTO_GDB="y"; shift ;;
        -r) AGENT_RUN="y"; shift ;;
        *)  echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [ "${AUTO_GDB}" != "y" ]; then SKIP_LOAD="y"; fi
if [ "${AGENT_RUN}" = "y" ]; then SKIP_LAUNCH_PRINT="y"; RUN_TIMEOUT="0"; fi

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_target_mbox_test"
CONTROLLER_SRC="${I3C_TEST_ROOT}/cpu2_controller_mbox_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
CONTROLLER_OVERLAY="$(realpath "${CONTROLLER_SRC}/app.overlay")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_BUILD="${TEST_BUILD_DIR}/target"
CONTROLLER_BUILD="${TEST_BUILD_DIR}/controller"
TARGET_ELF="${TARGET_BUILD}/zephyr/zephyr.elf"
CONTROLLER_ELF="${CONTROLLER_BUILD}/zephyr/zephyr.elf"
CONTROLLER_BIN="${CONTROLLER_BUILD}/zephyr/zephyr.bin"

if [ "${AGENT_RUN}" != "y" ]; then
# ★ 双核工程用 build_test（_detect_board 自动识别 2os cpu1/cpu2），
#   不用 build_single_test（硬编码 1os cpu1 会搞错 board）。
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "" "${CONTROLLER_BUILD}"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TARGET_BUILD}"
echo -e "${COLOR_GREEN}Build OK (both cores):${COLOR_RESET} ${TEST_BUILD_DIR}"
else
# Agent mode: build both cores if ELF missing, then load+run both via GDB
if [ ! -f "${TARGET_ELF}" ]; then
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TARGET_BUILD}"
fi
if [ ! -f "${CONTROLLER_BIN}" ]; then
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "" "${CONTROLLER_BUILD}"
fi
load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
