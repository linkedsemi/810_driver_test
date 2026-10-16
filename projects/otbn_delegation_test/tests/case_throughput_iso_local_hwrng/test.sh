#!/usr/bin/env bash
# OTBN ECDSA throughput -- ISOLATED LOCAL, HARDWARE-RNG variant
# (case_throughput_iso_local_hwrng)
#
# Same as case_throughput_iso_local (cpu1 benches its LOCAL OTBN, cpu2 holds),
# except the bench feeds genkey/sign from the HARDWARE entropy device
# (-DOTBN_DELEG_BENCH_HWRNG=1) instead of the in-process LCG.
#
# Why: ecdsa_alt.c pulls curve_size bytes from f_rng on every genkey/sign (and
# none on verify). The delegation SERVER handler passes the hardware entropy
# device; case_throughput_iso_local's bench passes a free software LCG. Diffing
# the proxy run against THIS run therefore isolates the bus round trip, while
# diffing it against case_throughput_iso_local shows how much of the raw diff is
# just the entropy source.
#
#   cpu1 (target,     SERVER): -DOTBN_DELEG_BENCH=1 + _HWRNG=1   -> local OTBN,
#                                                                  hw entropy
#   cpu2 (controller, CLIENT): -DOTBN_DELEG_BENCH=1 + _HOLD=1     -> hold
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_throughput_iso_local_hwrng"

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
# Server benches its local OTBN with the hardware entropy device; client holds.
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}" "-DOTBN_DELEG_BENCH=1 -DOTBN_DELEG_BENCH_HOLD=1"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}" "-DOTBN_DELEG_BENCH=1 -DOTBN_DELEG_BENCH_HWRNG=1"
echo ""
echo -e "${COLOR_GREEN}===== ${TEST_NAME} built (ISOLATED local+hw-entropy: cpu1 benches, cpu2 holds) =====${COLOR_RESET}"
print_launch_json "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
