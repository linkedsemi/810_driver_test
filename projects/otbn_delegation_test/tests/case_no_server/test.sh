#!/usr/bin/env bash
# OTBN ECDSA delegation dual-core test (case_no_server)
#
# Scenario: the PEER is absent -- the target/server image is built WITHOUT the
# delegation server (CONFIG_MBEDTLS_LINKEDSEMI_OTBN_DELEGATION_SERVER=n), while
# the client stays normal. This is distinct from case_dead_channel (where the
# client's own send path fails):
#   - here mbox_send_dt() SUCCEEDS (the client can send), but no reply ever
#     arrives, so the client blocks forever on client_op_return_sem;
#   - t16 asserts mbox_send_dt()==0 (send-path evidence) AND that the proxy call
#     returns within the timeout -- the latter FAILS on the client.
#
# The client's functional items are skipped via -DOTBN_DELEG_NO_SERVER (the
# peer-absent gate); the server's own items are local and unaffected.
#
# This case is a DEFECT-CHARACTERIZATION case: OVERALL is expected to be FAIL.
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_no_server"

# Select the "peer absent" test surface for the CLIENT build only (see the gate
# in tests/src/otbn_delegation_test.c). Passed through test_common.sh's
# EXTRA_CMAKE_ARGS passthrough (#70).

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
EXTRA_CMAKE_ARGS="-DOTBN_DELEG_NO_SERVER=1" build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"
echo ""
echo -e "${COLOR_GREEN}===== case_no_server built (expect FAIL: client proxy hang) =====${COLOR_RESET}"
print_launch_json "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
