#!/usr/bin/env bash
# I3C dual-core test: pure I3C bus @ 6.25 MHz SDR
#
# What is tested:
#   Same controller/target functional coverage as test_default, but with the
#   controller I3C bus configured for pure I3C bus at 6.25 MHz SDR. Verifies that this timing
#   configuration does not break DAA, transfers, IBI or Hot-Join.
#
# CPU0 (controller, I3C5):
#   - Runs the same scenarios as test_default (write-read, 100-byte write, IBI, Hot-Join).
#   - Uses pure I3C bus at 6.25 MHz SDR.
#
# CPU1 (target, I3C6):
#   - Registers as I3C target at address 0x32.
#   - Responds to 0xAA reads and raises IBI/HJ on 0xBB/0xCC, identical to test_default.
#
# Normal behavior:
#   All transactions complete under the configured timing exactly like test_default:
#   target 0x32 discovered, 32-byte read matches 0xAA..0xC9, 100B write OK,
#   IBI payload received, HJ re-discovered, post-HJ read OK.
#
# Abnormal behavior:
#   Same failure modes as test_default. Additionally, if the selected timing is not
#   supported by the board/wiring, transactions may NACK or the controller may
#   report bus errors / timeout in the UART log.
#
# Feedback:
#   - UART4 (controller): per-phase OK/fail messages and final "I3C master test done."
#   - UART1 (target): register/IBI/HJ event logs.
#
# Hardware IO connections:
#   - I3C5_SCL (PJ10) <-> I3C6_SCL (PJ08)
#   - I3C5_SDA (PJ11) <-> I3C6_SDA (PJ09)
#   - Common ground between the two I3C buses.
#
# Build artifacts:
#   - tests/case_pure_i3c_6m25/build/controller/
#   - tests/case_pure_i3c_6m25/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/../test_common.sh"

TEST_NAME="case_pure_i3c_6m25"

# Parse options: -g enables automatic GDB load/run.
AUTO_GDB=""
while [ $# -gt 0 ]; do
    case "$1" in
        -g)
            AUTO_GDB="y"
            shift
            ;;
        -r)
            AGENT_RUN="y"
            shift
            ;;
        *)
            echo -e "${COLOR_YELLOW}Unknown option: $1${COLOR_RESET}"
            echo "Usage: $0 [-g] [-r]"
            exit 1
            ;;
    esac
done

if [ "${AUTO_GDB}" != "y" ]; then
    SKIP_LOAD="y"
fi

if [ "${AGENT_RUN}" = "y" ]; then
    SKIP_LAUNCH_PRINT="y"
    RUN_TIMEOUT="0"
fi

CONTROLLER_SRC="${I3C_TEST_ROOT}/cpu0_i3c_controller"
TARGET_SRC="${I3C_TEST_ROOT}/cpu1_i3c_target"

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
# 1. Build controller and target images first.
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 2. Print test summary after build so it is not buried in build logs.
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}What: Full I3C controller/target test sequence at this timing: DAA, 1/16/17/32-byte write-read, 100-byte write, direct read, direct write, IBI and Hot-Join.25 MHz SDR.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: All transactions pass under the configured timing; 1/16/17/32-byte reads return the expected byte counts; 100B write OK;${COLOR_RESET}"
echo -e "${COLOR_YELLOW}        100B write OK; IBI payload received; HJ re-discovered and post-HJ read OK.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: Same as test_default; additionally wiring/timing issues may cause${COLOR_RESET}"
echo -e "${COLOR_YELLOW}          NACK or bus-error/timeout messages on UART4.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: UART4 shows per-phase OK/fail and 'I3C master test done.';${COLOR_RESET}"
echo -e "${COLOR_YELLOW}          UART1 shows target register / IBI / HJ event logs.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: I3C5_SCL(PJ10)<->I3C6_SCL(PJ08), I3C5_SDA(PJ11)<->I3C6_SDA(PJ09)${COLOR_RESET}"
echo ""
echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET}"
echo "  ${CONTROLLER_BUILD}"
echo "  ${TARGET_BUILD}"
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""

fi

if [ "${AGENT_RUN}" != "y" ]; then
# 3. Print launch.json snippet for manual copy-paste.
print_launch_json "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

# 4. Optionally load into RAM and run.
if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 5. Hint when GDB load is skipped.
if [ "${AUTO_GDB}" != "y" ]; then
    echo ""
    echo -e "${COLOR_YELLOW}Hint:${COLOR_RESET} run with ${COLOR_GREEN}-g${COLOR_RESET} to automatically load and run via GDB."
    echo -e "  ${COLOR_CYAN}$0 -g${COLOR_RESET}"
    echo ""
fi
fi
