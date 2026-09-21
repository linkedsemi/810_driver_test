#!/usr/bin/env bash
# MBOX dual-core test - FIFO full / ENOSPC
#
# What is tested:
#   ch4/ch5 的 fifo 深度 = 4（板级 dts）。target 先屏蔽本核 mbox IRQ，
#   使其 RX fifo 在窗口内不被排空；controller 随即连发 depth+2 = 6 帧，
#   断言前 4 帧返回 0、后 2 帧命中驱动的 -ENOSPC 分支；target 到点重新
#   使能 IRQ，驱动在 set_enabled(true) 内补排已入队消息（回调被唤起 4 次）。
#
# CPU1 (target, uart1): 屏蔽 IRQ → 800 ms 后重新使能，断言补排条数 == fifo 深度。
# CPU2 (controller, uart5): 500 ms 后连发 6 帧，分别统计 ok / enospc / other。
#
# Normal behavior: 两核均打印 PROJECT EXECUTION SUCCESSFUL；
#                  MBOX_TEST:<core>:PASS:fifo_full_enospc:ok=4,enospc=2；
#                  MBOX_TEST:<core>:PASS:fifo_full_drain_on_enable:drained=4,drop=0。
# Abnormal behavior: 任一核打印 PROJECT EXECUTION FAILED（边界返回值/补排条数不符）。
#
# Feedback: 串口逐项行 `MBOX_TEST:<core>:(PASS|FAIL):<name>[:<明细>]`（fifo_full_enospc /
#           fifo_full_drain_on_enable；判据见 tests/COVERAGE.md §3.5/§7）。
# Hardware IO connections: 无外部接线（mbox 走片内共享 SRAM）。
#
# 注：本 case 依赖两核启动顺序（target 先屏蔽、controller 后发送），
#     时间窗按 500 ms / 800 ms 留足余量。
#
# Build artifacts: tests/case_mbox_fifo_full/build/{target,controller}/
# Usage:
#   ./test.sh          # build only (both cores)
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_mbox_fifo_full"

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
