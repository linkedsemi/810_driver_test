#!/usr/bin/env bash
# I3C/I2C module-switch test: no-bus-init case (controller skips boot-time bus init)
#
# What is tested:
#   The I3C controller driver's `no-bus-init` DTS switch (opt-out semantics).
#   With `&i3c5 { no-bus-init; };` the driver's ls_i3c_init() guard skips
#   i3c_bus_init() (RSTDAA / DISEC / SETDASA / DAA / SETAASA), so the bus is
#   left UNENUMERATED; the application must call i3c_bus_init() itself.
#
#   A/B inside the `i3c_baseline` item (both halves required):
#     ① negative control - with no bus init and no app-side init, the target
#        must be unreachable: no dynamic address assigned (desc->dynamic_addr
#        == 0), empty attached list, and i3c_transfer() must fail with the
#        driver's deterministic error (-EINVAL). A best-effort bus-level probe
#        to 0x32 must also return no data (NACK reporting is timing dependent
#        on this HW, so the judged form is "no data", raw ret is reported).
#     ② recovery - after the application calls i3c_bus_init() explicitly, the
#        same target must become reachable again (dynamic_addr == 0x32) and the
#        regular 16-byte pattern write/read must pass.
#   The rest of the sequence is identical to case_switch_core, so this case
#   also proves nothing else regressed under the switch.
#
# CPU0 (controller, I3C5 + i2c5):
#   - Runs the no-bus-init A/B, then the full switch sequence.
#
# CPU1 (target, I3C6 + i2c6):
#   - Unchanged from case_switch_core; during ① the controller cannot reach it
#     (by construction), during ② and afterwards it behaves normally.
#
# Normal behavior:
#   All 11 items PASS on BOTH cores (CTRL+TGT double PASS); both SUMMARY
#   11/11 PASS and DONE:PASS. ①'s raw readings appear in the CTRL INFO of
#   i3c_baseline as `num_i3c=1,nbi_neg_dyn=0,nbi_neg_xfer=-22,...`.
#
# Abnormal behavior:
#   - If the switch is NOT honored (driver still enumerates at boot), ① fails
#     with reason `nbi:neg_dynaddr_<n>` or `nbi:neg_xfer_ret_<n>`.
#   - If the app-side i3c_bus_init() cannot restore the bus, ② fails with
#     `nbi:init_ret_<n>` / `nbi:recover_not_attached`.
#   - The remaining items fail exactly as in case_switch_core.
#
# Feedback:
#   - UART5 (controller): START/PASS/FAIL tokens per item + INFO + SUMMARY + DONE.
#   - UART1 (target): same token stream from the target side.
#
# Hardware IO connections:
#   - I3C5_SCL (PJ10) <-> I3C6_SCL (PJ08)
#   - I3C5_SDA (PJ11) <-> I3C6_SDA (PJ09)
#   - Same wiring as i3c_dualcore_test / case_switch_core.
#
# Build artifacts:
#   - tests/case_no_bus_init/build/controller/
#   - tests/case_no_bus_init/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_no_bus_init"

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
echo -e "${COLOR_YELLOW}What: no-bus-init A/B (① negative control / ② explicit i3c_bus_init recovery)${COLOR_RESET}"
echo -e "${COLOR_YELLOW}      then the full case_switch_core sequence, unchanged.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: All 11 items PASS on both cores; both SUMMARY 11/11 PASS and DONE:PASS.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: switch not honored -> i3c_baseline FAIL with nbi:neg_dynaddr_* /${COLOR_RESET}"
echo -e "${COLOR_YELLOW}          nbi:neg_xfer_ret_*; recovery failed -> nbi:init_ret_* / nbi:recover_not_attached.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: UART5 (controller) and UART1 (target) show token streams + SUMMARY.${COLOR_RESET}"
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
