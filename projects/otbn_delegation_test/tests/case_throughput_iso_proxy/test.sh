#!/usr/bin/env bash
# OTBN ECDSA delegation throughput -- ISOLATED PROXY run (case_throughput_iso_proxy)
#
# Goal: measure the PROXY (delegation) path with the shared OTBN uncontended.
# Only ONE core benches; the other holds (does not touch the OTBN) so its
# measurements are free of the peer's queueing that pollutes case_throughput
# (where both cores bench at once -- see tests/COVERAGE.md §1 methodology note).
#
#   cpu2 (controller, CLIENT): -DOTBN_DELEG_BENCH=1                 -> bench, each op
#                                                                     is an mbox
#                                                                     round-trip
#   cpu1 (target,     SERVER): -DOTBN_DELEG_BENCH=1 + _HOLD=1        -> hold; its
#                                                                     boot-time
#                                                                     delegation
#                                                                     thread still
#                                                                     answers cpu2,
#                                                                     but it runs
#                                                                     no bench ops
#
# Matrix: {genkey, sign, verify} x {P256, P384, SM2}, BENCH_N=20 measured
# iterations after BENCH_WARMUP=3. Results per combo:
#   INFO bench: op=<op>,gid=<id>,n=20,min_us=..,med_us=..,max_us=..,mean_us=..
# The peer's matching LOCAL numbers come from case_throughput_iso_local.
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_throughput_iso_proxy"

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
# Client benches the proxy; server holds (answers cpu2 but benches nothing).
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}" "-DOTBN_DELEG_BENCH=1"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}" "-DOTBN_DELEG_BENCH=1 -DOTBN_DELEG_BENCH_HOLD=1"
echo ""
echo -e "${COLOR_GREEN}===== ${TEST_NAME} built (ISOLATED proxy: cpu2 benches, cpu1 holds) =====${COLOR_RESET}"
print_launch_json "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
