#!/usr/bin/env bash
# MBOX dual-core test - all consumers
#
# What is tested:
#   补全 mbox consumer 通道通路覆盖：ch1(otbn_crypto, 32B) / ch2(espi1, 8B) /
#   ch3(vuart1, 8B) / ch4(mbox_test_ch4, 64B) 逐通道往返，且应答走原通道
#   （验证同一通道双向可用）；另在 ch5 发 8 B 短帧，固化"驱动 rx msg.size
#   恒报通道 width"的契约。ch0(qspi1) 由 flash delegation server 占用，不测。
#
# CPU1 (target, uart1): 逐通道收探针帧 → 校验 magic/op/seq/到达通道/rx size →
#                       在原通道回 ACK。
# CPU2 (controller, uart5): 逐通道发探针帧并等 ACK；末条为 ch5 短帧。
#
# Normal behavior: 两核均打印 PROJECT EXECUTION SUCCESSFUL；
#                  INFO consumer_ch1..ch4_roundtrip / consumer_same_channel_bidir /
#                  consumer_ch5_short_frame 全为 PASS（short_frame 的 rx len == 64）。
# Abnormal behavior: 任一核打印 PROJECT EXECUTION FAILED（通道不通/长度契约不符）。
#
# Feedback: 串口逐项行 `MBOX_TEST:<core>:(PASS|FAIL):<name>[:<明细>]`（两核各上报上述 6 项；
#           判据见 tests/COVERAGE.md §3.3/§7）。
# Hardware IO connections: 无外部接线（mbox 走片内共享 SRAM）。
#
# Build artifacts: tests/case_mbox_consumers/build/{target,controller}/
# Usage:
#   ./test.sh          # build only (both cores)
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_mbox_consumers"

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
