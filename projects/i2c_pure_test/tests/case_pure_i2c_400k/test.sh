#!/usr/bin/env bash
# Pure I2C waveform test: 400K continuous write/read loop for scope capture
#
# What is tested:
#   A pure-I2C link (no I3C involved) at 400K bus rate for waveform-quality
#   observation on a scope/logic analyzer:
#     - i2c5 (cpu2 master, I2C5_scl_PJ10 / I2C5_sda_PJ11)
#     - i2c6 (cpu1 slave, I2C6_scl_PJ08 / I2C6_sda_PJ09, slave addr 0x58)
#   Pinmux comes from the i2c5/i2c6 node pinctrl-0 (default state, generated
#   by board macros), applied automatically at driver init.
#   Sequence: 100 rounds of 16-byte write + 16-byte read, 100ms gap between
#   rounds (~10s total; each round is a ~0.3ms bus burst, easy to trigger).
#
# CPU2 (master, i2c5):
#   - Drives 100 rounds; verifies read payload (0xAA..0xB9); sends 'P' command
#     for the slave to print its SUMMARY; prints its own SUMMARY + DONE.
#
# CPU1 (slave, i2c6):
#   - Registers i2c6 slave (address 0x58); parses 'W' <len> <data> writes and
#     verifies the incremental payload; serves reads with 0xAA+i pattern.
#   - Prints SUMMARY + DONE on 'P' command.
#
# Normal behavior:
#   The single test item (i2c_wave) passes on BOTH cores (CTRL+TGT double
#   PASS); both cores print SUMMARY with 1/1 PASS and DONE:PASS.
#
# Abnormal behavior:
#   - Write/read failure in a round is recorded as the failing reason
#     (write:roundN / read:roundN / byte[i]:0xXX!=0xYY).
#   - Slave receiving no write or a payload mismatch fails with
#     no_write_seen / write_data_mismatch.
#
# Feedback:
#   - UART5 (controller, cpu2): START/PASS/FAIL tokens per item + SUMMARY + DONE.
#   - UART1 (target, cpu1): same token stream from the slave side.
#
# Hardware IO connections:
#   - I2C5_SCL (PJ10) <-> I2C6_SCL (PJ08)
#   - I2C5_SDA (PJ11) <-> I2C6_SDA (PJ09)
#   - Same wiring as i3c_dualcore_test; scope probes on either side.
#
# Build artifacts:
#   - tests/case_pure_i2c_400k/build/controller/
#   - tests/case_pure_i2c_400k/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_pure_i2c_400k"

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

CONTROLLER_SRC="${I3C_TEST_ROOT}/cpu0_i2c_master"
TARGET_SRC="${I3C_TEST_ROOT}/cpu1_i2c_slave"

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
echo -e "${COLOR_YELLOW}What: Pure I2C link at 400K for waveform observation.${COLOR_RESET}"
echo -e "${COLOR_YELLOW}      i2c5 (PJ10/PJ11, cpu2 master) <-> i2c6 (PJ08/PJ09, cpu1 slave 0x58).${COLOR_RESET}"
echo -e "${COLOR_YELLOW}      100 rounds x 16B write+read, 100ms gap, ~10s total.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: i2c_wave PASS on both cores (CTRL+TGT double PASS);${COLOR_RESET}"
echo -e "${COLOR_YELLOW}        both SUMMARY 1/1 PASS and DONE:PASS.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: round write/read failure or payload mismatch recorded${COLOR_RESET}"
echo -e "${COLOR_YELLOW}          as item FAIL reasons on both UARTs.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: UART5 (controller) and UART1 (target) show token streams + SUMMARY.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: I2C5_SCL(PJ10)<->I2C6_SCL(PJ08), I2C5_SDA(PJ11)<->I2C6_SDA(PJ09)${COLOR_RESET}"
echo -e "${COLOR_YELLOW}    (same wiring as i3c_dualcore_test; scope on either side)${COLOR_RESET}"
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
