#!/usr/bin/env bash
# MBOX dual-core test - stress (统计性质)
#
# What is tested:
#   连续 1000 条消息经 ch4 往返（严格逐条 ping-pong），统计丢包/序号错/
#   payload 错/往返延迟；末尾由 target 回 STAT 汇总，两核交叉比对计数与
#   payload 链式 CRC-32。
#
# CPU1 (target, uart1): 逐条校验序号与 payload pattern，回 ACK（携带累计 CRC），
#                       收 DONE 后回 STAT（recv/seq_err/payload_err/isr 计数）。
# CPU2 (controller, uart5): 发送 1000 条 ECHO 并逐条等 ACK，统计 RTT min/avg/max。
#
# Normal behavior: 两核均打印 PROJECT EXECUTION SUCCESSFUL；
#                  MBOX_TEST:<core>:PASS:stress_n_messages（sent==tgt_recv、crc==tgt_crc、各 err=0，
#                  两核各上报）；MBOX_TEST:<core>:PASS:stress_rtt_stats（仅上报 min/avg/max，无硬门限）。
# Abnormal behavior: 任一核打印 PROJECT EXECUTION FAILED（丢包/序号错/CRC 不符）。
#
# Feedback: 串口逐项行 `MBOX_TEST:<core>:(PASS|FAIL):<name>[:<明细>]`（stress_n_messages /
#           stress_rtt_stats；判据见 tests/COVERAGE.md §3.2/§7）。
# Hardware IO connections: 无外部接线（mbox 走片内共享 SRAM）。
#
# Build artifacts: tests/case_mbox_stress/build/{target,controller}/
# Usage:
#   ./test.sh          # build only (both cores)
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_mbox_stress"

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
