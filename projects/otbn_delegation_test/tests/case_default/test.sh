#!/usr/bin/env bash
# OTBN ECDSA delegation dual-core test (case_default)
#
# What is tested:
#   OTBN ECDSA delegation -- the cpu2 (CLIENT) mbedtls_ecdsa_{genkey,sign,verify}
#   stubs proxy every operation over the otbn_crypto mbox channel to cpu1
#   (SERVER), which performs the real OTBN ECDSA and returns the result.
#   Matrix: {P256, P384, SM2} x {genkey, sign, verify} + an out-of-whitelist
#   curve that must be rejected.
#
# CPU2 (controller, uart5): delegation CLIENT -- each test item is a real mbox
#                           round-trip to cpu1; verify asserts a tampered
#                           signature is rejected (a no-op proxy would accept it).
# CPU1 (target, uart1):     delegation SERVER -- answers cpu2's requests AND
#                           runs the same matrix against local OTBN ECDSA, so a
#                           double PASS means both the channel and the crypto
#                           are correct.
#
# Normal behavior:
#   All 10 items PASS on BOTH cores (CTRL+TGT double PASS); both cores print
#   PROJECT EXECUTION SUCCESSFUL.
#
# Abnormal behavior:
#   - Any delegated op returning non-zero (channel down / server error) -> FAIL.
#   - t07/t08/t09 (verify) FAIL if a tampered signature is accepted, i.e. if the
#     server status never reaches the client (KNOWN defect lead: the client's
#     recive_params.status is never copied from the reply) -> the proxy would be
#     a silent no-op.
#   - t10 FAIL if an out-of-whitelist curve is accepted.
#
# Feedback: ztest-native per-item lines on both UARTs
#           (START - <name> / " PASS|FAIL - <name> ..."), summary + PROJECT EXECUTION.
#
# Hardware IO connections:
#   无外部接线（OTBN 代理走共享 SRAM 的 mbox 通道，片内）。
#
# Build artifacts:
#   - tests/case_default/build/controller/
#   - tests/case_default/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

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
CONTROLLER_ELF="${CONTROLLER_BUILD}/zephyr/zephyr.elf"
CONTROLLER_BIN="${CONTROLLER_BUILD}/zephyr/zephyr.bin"

if [ "${AGENT_RUN}" != "y" ]; then
# 1. Build controller (cpu2 client) and target (cpu1 server) images first.
#    _detect_board maps *controller* -> @2os/lsqsh/cpu2, *target* -> @2os/lsqsh/cpu1
#    (dir names carry the 810 role keywords, so no BOARD_OVERRIDE is needed).
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"

# 2. Print a short summary after the build so it is not buried in build logs.
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}  (OTBN ECDSA delegation)${COLOR_RESET}"
echo -e "${COLOR_YELLOW}What: cpu2(client,uart5) delegates ec genkey/sign/verify over mbox to"
echo -e "${COLOR_YELLOW}      cpu1(server,uart1); both cores also run the matrix locally.${COLOR_RESET}"
echo -e "${COLOR_YELLOW}Matrix: {P256,P384,SM2} x {genkey,sign,verify} + reject_unsupported_curve.${COLOR_RESET}"
echo -e "${COLOR_YELLOW}Build OK:${COLOR_RESET}"
echo "  ${CONTROLLER_BUILD}"
echo "  ${TARGET_BUILD}"
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""

# 3. Print launch.json snippet for manual copy-paste.
print_launch_json "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

# 4. Optionally load into RAM and run.
if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
if [ "${AUTO_GDB}" != "y" ]; then
    echo ""
    echo -e "${COLOR_YELLOW}Hint:${COLOR_RESET} run with ${COLOR_GREEN}-g${COLOR_RESET} to automatically load and run via GDB."
    echo ""
fi
fi
