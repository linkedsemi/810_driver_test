#!/usr/bin/env bash
# OTBN ECDSA delegation SEQUENCE-WRAP soak (case_seq_soak)
#
# Purpose: cross the client's 8-bit request/response sequence wrap (the product
# skips 0 on wrap so a request seq never collides with the "no reply yet"
# sentinel). One item, run on both cores via -DOTBN_DELEG_SOAK=1:
#   - cpu2 (controller, CLIENT): every call is a seq'd proxy round-trip;
#   - cpu1 (target,     SERVER): the same code runs local OTBN (no seq).
# The CLIENT's proxy-call count is the number that must exceed 256.
#
# SOAK_ITERS=160, 4 calls per iteration = 640 proxy calls, each iteration
# asserting: genkey==0; sign==0 with r,s in (1,n-1); verify(valid)==0;
# verify(tampered r=(r+1) mod n)!=0. A silent stale-reply mix-up must trip a
# later assertion.
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_seq_soak"

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
EXTRA_CMAKE_ARGS="-DOTBN_DELEG_SOAK=1" build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
EXTRA_CMAKE_ARGS="-DOTBN_DELEG_SOAK=1" build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"
echo ""
echo -e "${COLOR_GREEN}===== case_seq_soak built (640 proxy calls, seq-wrap soak) =====${COLOR_RESET}"
print_launch_json "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
