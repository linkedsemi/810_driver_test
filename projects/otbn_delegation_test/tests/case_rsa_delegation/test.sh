#!/usr/bin/env bash
# OTBN RSA delegation dual-core test (case_rsa_delegation)
#
# What is tested:
#   The RSA family of the OTBN delegation bus. cpu2 (CLIENT) calls the explicit
#   proxy entry points mbedtls_ls_otbn_rsa_modexp_encrypt()/_decrypt(); each call
#   is a real mbox round-trip to cpu1 (SERVER), which runs the modexp on its
#   local OTBN (rsa_alt.c) and returns the result. cpu1 also runs the same
#   round-trip against its local backend, so a double PASS means the proxy and
#   the local reference agree.
#
#   Matrix:
#     r01 -- 2048-bit modexp round-trip: c = m^e mod n, m' = c^d mod n, m' == m
#     r02 -- 4096-bit modexp round-trip (same shape)
#     r03 -- unsupported modulus (1024 / 1536 bits) must be rejected
#     r90 -- mailbox-bound proof: with the client's otbn_crypto channel masked,
#            the explicit RSA call must FAIL (bus timeout) instead of returning a
#            locally computed result; software RSA would return rc=0 at once.
#
# Surface selection: -DOTBN_DELEG_RSA=1 (both cores; the server registers the
# same items -- it runs the local backend for r01/r02/r03 and SKIPs r90).
# The server must build its RSa backend: see target.conf.
#
# CPU2 (controller, uart5): delegation CLIENT -- r01/r02/r03 are real mbox
#                           round-trips to cpu1; r90 asserts the binding.
# CPU1 (target, uart1):     delegation SERVER -- answers cpu2's requests and
#                           runs r01/r02/r03 against local OTBN RSA.
#
# Hardware IO connections:
#   无外部接线（OTBN 代理走共享 SRAM 的 mbox 通道，片内）。
#
# Build artifacts:
#   - tests/case_rsa_delegation/build/controller/
#   - tests/case_rsa_delegation/build/target/
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_rsa_delegation"

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
# Register the RSA item set on BOTH cores (client drives the proxy, server drives
# the local backend); the server's RSA backend itself comes from target.conf.
EXTRA_CMAKE_ARGS="-DOTBN_DELEG_RSA=1" build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
EXTRA_CMAKE_ARGS="-DOTBN_DELEG_RSA=1" build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"

echo -e "${COLOR_GREEN}===== case_rsa_delegation built (RSA modexp proxy + local) =====${COLOR_RESET}"
fi

if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
