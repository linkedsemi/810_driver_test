#!/usr/bin/env bash
# MBOX dual-core test — MBOX layout-check OFF case.
#
# 内容与 case_mbox_basic 相同（ch4/ch5 双向、多通道隔离、MTU 边界），
# 唯一差异是两核都显式设置：
#   CONFIG_MBOX_LINKEDSEMI_LAYOUT_CHECK=n
# Kconfig 明确要求两核取值一致；conf 由 build_test 第 4 参注入。
#
# 覆盖点：check off 时 legacy TX FIFO init 默认打开，双方各自初始化自己的
# TX FIFO；ch4/ch5 双向收发与多通道隔离仍须全部 PASS。
#
# Usage:
#   ./test.sh          # build only (both cores)
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_layout_check_off"

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
TARGET_CONF="$(realpath "${SCRIPT_DIR}/target.conf")"
CONTROLLER_CONF="$(realpath "${SCRIPT_DIR}/controller.conf")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_BUILD="${TEST_BUILD_DIR}/target"
CONTROLLER_BUILD="${TEST_BUILD_DIR}/controller"
TARGET_ELF="${TARGET_BUILD}/zephyr/zephyr.elf"
CONTROLLER_ELF="${CONTROLLER_BUILD}/zephyr/zephyr.elf"
CONTROLLER_BIN="${CONTROLLER_BUILD}/zephyr/zephyr.bin"

if [ "${AGENT_RUN}" != "y" ]; then
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"
echo -e "${COLOR_GREEN}Build OK (both cores):${COLOR_RESET} ${TEST_BUILD_DIR}"
else
if [ ! -f "${TARGET_ELF}" ]; then
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"
fi
if [ ! -f "${CONTROLLER_BIN}" ]; then
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
fi
load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
