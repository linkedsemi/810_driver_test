#!/usr/bin/env bash
# DMA destructive/robustness test
#
# Tests DMA driver behavior with invalid parameters, boundary conditions,
# and edge cases.
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_destructive"

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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_dma_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_BUILD="${TEST_BUILD_DIR}/target"
TARGET_ELF="${TARGET_BUILD}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
    rm -rf "${TARGET_BUILD}"
    west build -p always -b lsqsh_evb@2os/lsqsh/cpu1 \
        "${TARGET_SRC}" \
        -d "${TARGET_BUILD}" \
        -- -DDTC_OVERLAY_FILE="${TARGET_OVERLAY}" \
           -DDMA_DESTRUCTIVE=1
    echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TARGET_BUILD}"
    print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
else
    if [ ! -f "${TARGET_ELF}" ]; then
        rm -rf "${TARGET_BUILD}"
        west build -p always -b lsqsh_evb@2os/lsqsh/cpu1 \
            "${TARGET_SRC}" \
            -d "${TARGET_BUILD}" \
            -- -DDTC_OVERLAY_FILE="${TARGET_OVERLAY}" \
               -DDMA_DESTRUCTIVE=1
    fi
    load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi
