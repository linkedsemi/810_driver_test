#!/usr/bin/env bash
# OTBN delegation project -- ENTROPY-API timing run (case_entropy_iso)
#
# Goal: time the TWO fetch paths of the device chosen as zephyr,entropy (trng0)
# on ONE core, with the peer holding. This decides whether the ~1.9 ms/byte the
# delegation SERVER handler shows (it calls the BLOCKING entropy_get_entropy via
# ecdsa_server_get_random) is an API-selection cost or the raw TRNG rate.
#
#   cpu1 (target,     SERVER): -DOTBN_DELEG_BENCH=1 + _ENTROPY=1  -> bench; times
#                        entropy_get_entropy() and
#                        entropy_get_entropy_isr(..., ENTROPY_BUSYWAIT)
#   cpu2 (controller, CLIENT): -DOTBN_DELEG_BENCH=1 + _HOLD=1 + _ENTROPY=1
#                        -> hold; mbox channel up, sends nothing
#
# Matrix: {entropy_block, entropy_busy} x {1,16,32,48} bytes, BENCH_N=20
# measured iterations after BENCH_WARMUP=3, run TWICE with reversed API order
# (pass 1 block->busy, pass 2 busy->block). Results per cell:
#   INFO bench: op=<api>,pass=<1|2>,bytes=<N>,n=20,min_us=..,med_us=..,max_us=..,mean_us=..
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_entropy_iso"

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
# cpu1 times the entropy device; cpu2 holds (channel up, sends nothing).
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}" "-DOTBN_DELEG_BENCH=1 -DOTBN_DELEG_BENCH_HOLD=1 -DOTBN_DELEG_BENCH_ENTROPY=1"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}" "-DOTBN_DELEG_BENCH=1 -DOTBN_DELEG_BENCH_ENTROPY=1"
echo ""
echo -e "${COLOR_GREEN}===== ${TEST_NAME} built (ENTROPY timing: cpu1 benches trng0, cpu2 holds) =====${COLOR_RESET}"
print_launch_json "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
