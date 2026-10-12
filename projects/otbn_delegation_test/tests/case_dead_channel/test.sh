#!/usr/bin/env bash
# OTBN ECDSA delegation dual-core test (case_dead_channel)
#
# Scenario: the client's delegation channel is unusable (the otbn_crypto mbox
# consumer node is disabled by controller.overlay). Every client-side proxy call
# (`mbedtls_ecdsa_{genkey,sign,verify}`) therefore has no responder.
#
# What this case demonstrates:
#   - t16 (boundary probe, run on a separate thread with a timeout) FAILS on the
#     client: the proxy call does NOT return -- the client blocks forever on
#     k_sem_take(&client_op_return_sem, K_FOREVER) while mbox_send_dt()'s error
#     return is ignored. This is the "no responder => permanent hang" defect.
#   - The functional items t01-t15 are SKIPPED on the client (the same gate that
#     detects the dead channel), so the suite still produces a decisive per-item
#     result instead of wedging at t01.
#   - The server core is unaffected (its own ops are local) => its items PASS.
#
# This case is a DEFECT-CHARACTERIZATION case: OVERALL is expected to be FAIL.
# It is not part of the green functional baseline (case_default).
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_dead_channel"

# Select the "channel disabled at run time" test surface for the CLIENT build
# only (t16 calls mbox_set_enabled(false) before probing; see the gate in
# tests/src/otbn_delegation_test.c). Passed through test_common.sh's
# EXTRA_CMAKE_ARGS passthrough (#70). The server core stays normal.

AUTO_GDB=""
while [ $# -gt 0 ]; do
    case "$1" in
        -g) AUTO_GDB="y"; shift ;;
        -r) AGENT_RUN="y"; shift ;;
        *)  echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [ "${AUTO_GDB}" != "y" ] && [ "${AGENT_RUN}" != "y" ]; then SKIP_LOAD="y"; fi
if [ "${AGENT_RUN}" = "y" ]; then SKIP_LAUNCH_PRINT="y"; RUN_TIMEOUT="0"; fi

CONTROLLER_SRC="${I3C_TEST_ROOT}/cpu2_controller_otbn_delegation_test"
TARGET_SRC="${I3C_TEST_ROOT}/cpu1_target_otbn_delegation_test"
CONTROLLER_OVERLAY="$(realpath "${SCRIPT_DIR}/controller.overlay")"
TARGET_OVERLAY="$(realpath "${SCRIPT_DIR}/target.overlay")"
CONTROLLER_CONF="$(realpath "${SCRIPT_DIR}/controller.conf")"
TARGET_CONF="$(realpath "${SCRIPT_DIR}/target.conf")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
CONTROLLER_BUILD="${TEST_BUILD_DIR}/controller"
TARGET_BUILD="${TEST_BUILD_DIR}/target"
TARGET_ELF="${TARGET_BUILD}/zephyr/zephyr.elf"
CONTROLLER_BIN="${CONTROLLER_BUILD}/zephyr/zephyr.bin"

if [ "${AGENT_RUN}" != "y" ]; then
EXTRA_CMAKE_ARGS="-DOTBN_DELEG_DEAD_CHANNEL=1" build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"
echo ""
echo -e "${COLOR_GREEN}===== case_dead_channel built (expect FAIL: client proxy hang) =====${COLOR_RESET}"
print_launch_json "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
