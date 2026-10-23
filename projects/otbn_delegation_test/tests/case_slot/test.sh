#!/usr/bin/env bash
# OTBN ECDSA delegation -- private-key slot signing (case_slot)
#
# What is tested:
#   The new slot-based signing path. The private key never crosses the mbox:
#   cpu1 (SERVER) registers a key in slot 0 and the request carries only the
#   slot id + digest.
#
#   Shared item set (selected by -DOTBN_DELEG_SLOT=1 on BOTH cores,
#   tests/src/otbn_delegation_slot.c):
#     s01_sign_verify      sign via slot 0, verify with the known public key
#                          (client: public API + mbox; server: direct handler)
#     s02_unregistered     slot 1 is empty -> -ENOENT on both cores
#     s03_curve_mismatch   slot 0 is P256, request P384 -> -EINVAL
#     s04_register_validation
#                          server exercises the registry (invalid scalar,
#                          out-of-range slot, NULL key, unregister semantics);
#                          client SKIPs (registration is server-local)
#
# Normal behavior:
#   s01/s02/s03 PASS on BOTH cores; s04 PASS on cpu1 and SKIP on cpu2; both
#   cores print PROJECT EXECUTION SUCCESSFUL.
#
# Abnormal behavior:
#   - s01 FAIL if the mbox path or the local handler returns non-zero, or if
#     the resulting signature does not verify.
#   - s02 FAIL if an unregistered slot still signs (lookup is not enforced).
#   - s03 FAIL if the curve of the registered key is not checked.
#
# Hardware IO connections:
#   无外部接线（片内 mbox 通道）。
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_slot"

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

# controller = cpu2 = delegation CLIENT ; target = cpu1 = delegation SERVER
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
# Slot surface on BOTH cores: client calls the explicit API, server calls the
# handler directly (same item names, so the dual-core matrix is symmetric).
EXTRA_CMAKE_ARGS="-DOTBN_DELEG_SLOT=1" build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
EXTRA_CMAKE_ARGS="-DOTBN_DELEG_SLOT=1" build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"

echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}  (OTBN ECDSA private-key slot signing)${COLOR_RESET}"
echo -e "${COLOR_YELLOW}What: cpu2(client,uart5) signs with slot 0 over mbox; cpu1(server,uart1)"
echo -e "${COLOR_YELLOW}      registers the key and runs the handler directly. Key never crosses mbox.${COLOR_RESET}"
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
print_launch_json "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

if [ "${AGENT_RUN}" != "y" ] && [ "${AUTO_GDB}" != "y" ]; then
    echo ""
    echo -e "${COLOR_YELLOW}Hint:${COLOR_RESET} run with ${COLOR_GREEN}-g${COLOR_RESET} to automatically load and run via GDB."
    echo ""
fi
