#!/usr/bin/env bash
# I3C/I2C module-switch test: core case (pinmux switch, default timings)
#
# What is tested:
#   The same hardware module (I3C5/I3C6) switching between I3C and I2C modes,
#   where the mode is selected by pinmux (pinctrl dynamic states):
#     - "default"     = I3C pinmux (i3c5_scl_pj10/i3c5_sda_pj11 ...)
#     - "pinmux_i2c"  = I2C pinmux (I2C5_scl_PJ10/I2C5_sda_PJ11 ...)
#   Sequence: pinctrl state validation, I3C baseline (DAA+16B rw), switch to I2C,
#   1/16/17/100-byte I2C rw, I2C NACK negative path, switch back to I3C (re-DAA),
#   and 5-round I3C<->I2C switch stress.
#
# CPU0 (controller, I3C5 + i2c5):
#   - Runs the full sequence above; drives the command channel in I3C mode
#     (i3c_transfer private write, target 0x32) or I2C mode (i2c_write, slave 0x50).
#   - pinctrl_apply_state() switches the shared pins between I3C and I2C functions.
#
# CPU1 (target, I3C6 + i2c6):
#   - Registers i3c6 target (dynamic address 0x32) and i2c6 slave (address 0x50)
#     simultaneously; both share one byte-level parse state machine.
#   - Applies the pinmux switch on 'S'/'Y' commands after a delay.
#
# Normal behavior:
#   Each of the 10 test items passes on BOTH cores (CTRL+TGT double PASS);
#   both cores print SUMMARY with 10/10 PASS and DONE:PASS.
#
# Abnormal behavior:
#   - pinctrl_apply_state() failure is recorded as the failing item's reason.
#   - Mode mismatch (e.g. controller still in I3C while bus is switched) fails
#     with "mode:N" reason.
#   - I2C accesses to address 0x70 must NACK (negative path).
#
# Feedback:
#   - UART5 (controller): START/PASS/FAIL tokens per item + SUMMARY + DONE.
#   - UART1 (target): same token stream from the target side.
#
# Hardware IO connections:
#   - I3C5_SCL (PJ10) <-> I3C6_SCL (PJ08)
#   - I3C5_SDA (PJ11) <-> I3C6_SDA (PJ09)
#   - Same wiring as i3c_dualcore_test; the I2C mode reuses the same two wires.
#
# Build artifacts:
#   - tests/case_switch_core/build/controller/
#   - tests/case_switch_core/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_switch_core"

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

if [ "${AUTO_GDB}" != "y" ] && [ "${AGENT_RUN}" != "y" ]; then
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
echo -e "${COLOR_YELLOW}What: I3C<->I2C module switch via pinmux (pinctrl_apply_state).${COLOR_RESET}"
echo -e "${COLOR_YELLOW}      Sequence: pinctrl states, I3C baseline, switch to I2C, I2C rw 1/16/17/100,${COLOR_RESET}"
echo -e "${COLOR_YELLOW}      I2C NACK negative, switch back to I3C (re-DAA), 5-round switch stress.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: All 10 items PASS on both cores (CTRL+TGT double PASS);${COLOR_RESET}"
echo -e "${COLOR_YELLOW}        both SUMMARY 10/10 PASS and DONE:PASS.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: pinctrl_apply_state failure / mode mismatch / missing NACK recorded${COLOR_RESET}"
echo -e "${COLOR_YELLOW}          as item FAIL reasons on both UARTs.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: UART5 (controller) and UART1 (target) show token streams + SUMMARY.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: I3C5_SCL(PJ10)<->I3C6_SCL(PJ08), I3C5_SDA(PJ11)<->I3C6_SDA(PJ09)${COLOR_RESET}"
echo -e "${COLOR_YELLOW}    (same wiring as i3c_dualcore_test; I2C mode reuses these two wires)${COLOR_RESET}"
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
