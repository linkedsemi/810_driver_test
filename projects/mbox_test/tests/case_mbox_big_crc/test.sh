#!/usr/bin/env bash
# MBOX dual-core test - big payload + CRC32
#
# What is tested:
#   LinkedSemi mbox driver 大数据通路：4096 B 按 ch4 的 MTU(64 B) 分片
#   （79 片，末片部分有效）经共享 SRAM 的 MBOX 区传输，逐片 ACK 走 ch5，
#   末端用软件 CRC-32(IEEE 802.3) 双核交叉校验。
#
# CPU1 (target, uart1): 回调重组分片、逐片回 ACK、收到 DONE 后回 STAT 汇总。
# CPU2 (controller, uart5): 产生 4096 B pattern、分片发送、比对 target CRC。
#
# Normal behavior: 两核均打印 PROJECT EXECUTION SUCCESSFUL；
#                  MBOX_TEST:<core>:PASS:big_payload_crc（crc == tgt_crc 且 tgt_st=0）
#                  且 MBOX_TEST:<core>:PASS:big_crc_chunk_ack（chunk_err/ack_timeout/send_fail 全 0）。
# Abnormal behavior: 任一核打印 PROJECT EXECUTION FAILED（分片丢失/CRC 不符）。
#
# Feedback: 串口逐项行 `MBOX_TEST:<core>:(PASS|FAIL):<name>[:<明细>]`（两核各上报
#           big_payload_crc / big_crc_chunk_ack；判据见 tests/COVERAGE.md §3.1/§7）。
# Hardware IO connections: 无外部接线（mbox 走片内共享 SRAM）。
#
# Build artifacts: tests/case_mbox_big_crc/build/{target,controller}/
# Usage:
#   ./test.sh          # build only (both cores)
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_mbox_big_crc"

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
# ★ 双核工程用 build_test（_detect_board 自动识别 2os cpu1/cpu2）
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
