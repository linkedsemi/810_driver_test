#!/usr/bin/env bash
# perf_test — MBOX layout-check ON case (dual core).
#
# 与 case_default 相同的 21+11 项性能/功能测量，唯一差异是两核都显式设置
#   CONFIG_MBOX_LINKEDSEMI_LAYOUT_CHECK=y
# （Kconfig 要求两核取值一致；conf 通过 build_test 的第 4 参注入）。
#
# 覆盖点：check on 时 initiator(cpu1) 在 init 里初始化两侧 TX/RX FIFO，
# follower(cpu2) 校验并采用共享布局；perf 的 roundtrip / proxy_* / peer_alive
# 全部经 mbox ch5~ch9 往返，任何一侧初始化不对都会在这里失败。
#
# Usage:
#   ./test.sh          # build only (both cores)
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_layout_check_on"

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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_perf_test"
CONTROLLER_SRC="${I3C_TEST_ROOT}/cpu2_perf_peer"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
CONTROLLER_OVERLAY="$(realpath "${CONTROLLER_SRC}/app.overlay")"
TARGET_CONF="$(realpath "${SCRIPT_DIR}/target.conf")"
CONTROLLER_CONF="$(realpath "${SCRIPT_DIR}/controller.conf")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_BUILD="${TEST_BUILD_DIR}/target"
CONTROLLER_BUILD="${TEST_BUILD_DIR}/controller"
TARGET_ELF="${TARGET_BUILD}/zephyr/zephyr.elf"
CONTROLLER_BIN="${CONTROLLER_BUILD}/zephyr/zephyr.bin"

if [ "${AGENT_RUN}" != "y" ]; then
BOARD_OVERRIDE="${CONTROLLER_BOARD}" build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
BOARD_OVERRIDE="${TARGET_BOARD}" build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"
echo -e "${COLOR_GREEN}Build OK (both cores):${COLOR_RESET} ${TEST_BUILD_DIR}"
else
if [ ! -f "${TARGET_ELF}" ]; then
BOARD_OVERRIDE="${TARGET_BOARD}" build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"
fi
if [ ! -f "${CONTROLLER_BIN}" ]; then
BOARD_OVERRIDE="${CONTROLLER_BOARD}" build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
fi
load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
