#!/usr/bin/env bash
# perf_test — single case, dual engine:
#   cpu1 runs the hardware SHA/SM3/SHA512 + AES/SM4 engines and the system
#        suites (DMA / flash / mbox / trng).
#   cpu2 runs the OTBN SHA/SM3 interface (wrapped by mbedtls) and the local
#        OTBN RSA modexp, plus the mbox echo peer and the flash proxy client.
# All test data is relayed to cpu1 over perf_ch9; cpu1's UART is the single
# outlet.  Neither engine uses the OTBN delegation path.
#
# Normal behavior:
#   cpu1: 21/21 PASS + PROJECT EXECUTION SUCCESSFUL
#   cpu2: 11/11 PASS + PROJECT EXECUTION SUCCESSFUL（经 perf_ch9 上报）
#
# Hardware IO connections:
#   None (all engines/peripherals on-chip; mbox uses shared SRAM).
#
# Usage:
#   ./test.sh          # build only (both cores)
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_default"

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

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_BUILD="${TEST_BUILD_DIR}/target"
CONTROLLER_BUILD="${TEST_BUILD_DIR}/controller"
TARGET_ELF="${TARGET_BUILD}/zephyr/zephyr.elf"
CONTROLLER_BIN="${CONTROLLER_BUILD}/zephyr/zephyr.bin"

# Directory names use cpu1_/cpu2_ rather than the I3C-era target/controller
# vocabulary, so _detect_board cannot infer the role from the name; pass the
# board explicitly (BOARD_OVERRIDE is the documented escape hatch, KNOWN_ISSUES #70).
if [ "${AGENT_RUN}" != "y" ]; then
BOARD_OVERRIDE="${CONTROLLER_BOARD}" build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "" "${CONTROLLER_BUILD}"
BOARD_OVERRIDE="${TARGET_BOARD}" build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TARGET_BUILD}"
echo -e "${COLOR_GREEN}Build OK (both cores):${COLOR_RESET} ${TEST_BUILD_DIR}"
else
if [ ! -f "${TARGET_ELF}" ]; then
BOARD_OVERRIDE="${TARGET_BOARD}" build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TARGET_BUILD}"
fi
if [ ! -f "${CONTROLLER_BIN}" ]; then
BOARD_OVERRIDE="${CONTROLLER_BOARD}" build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "" "${CONTROLLER_BUILD}"
fi
load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
