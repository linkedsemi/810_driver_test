#!/usr/bin/env bash
# OTBN ECDSA delegation, PLAIN API routed through the proxy (case_alt_via_proxy)
#
# What is tested:
#   The SAME delegation matrix as case_default, but the cpu2 (CLIENT) is built in
#   the ALT_VIA_DELEGATION config (CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_VIA_
#   DELEGATION=y). In that config the client's plain mbedtls_ecdsa_{genkey,sign,
#   verify} names ARE the delegation forwarders (mbedtls_otbn_crypto_mbox.c
#   supplies them instead of ecdsa_alt.c / ecdsa.c). The shared body therefore
#   calls the PLAIN names (see the DELEG_* redirect at the top of
#   tests/src/otbn_delegation_test.c) -- i.e. it exercises exactly the API an
#   application / TLS / pk layer would use, proving the plain mbedtls API is
#   really forwarded to the peer core.
#
#   Surface selection: -DOTBN_DELEG_VIA_PROXY=1 (both cores; on the server the
#   macro only registers the extra item, see below).
#
# CPU2 (controller, uart5): delegation CLIENT -- t01..t88 call the plain
#                           mbedtls_ecdsa_* names; each is a real mbox round-trip
#                           to cpu1. It builds NO local OTBN (that is the point).
# CPU1 (target, uart1):     delegation SERVER -- unchanged: local OTBN ECDSA
#                           (ecdsa_alt.c) and answers the client's requests.
#
# Extra item (vs case_default), the case's independent value:
#   t90_plain_api_mailbox_bound -- proves the plain-name call is really bound to
#   the mailbox, not silently local software. With the client's otbn_crypto
#   channel masked, the plain-name genkey must TIMEOUT (return non-zero); software
#   ECDSA would return a valid key at once and FAIL this item. A correct signature
#   alone cannot tell a real round-trip from a software fallback, hence this
#   observable cross-core side effect (same bounded-probe pattern as t80). Client
#   asserts; server SKIPs.
#
# Normal behavior (prediction, to be confirmed on hardware):
#   Same per-item outcome as case_default (the four explicit-proxy cases and this
#   plain-proxy case reach the SAME mbox stub, just via a different name), plus
#   t90 PASS on the client / SKIP on the server.
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_alt_via_proxy"

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
# Register the extra item on BOTH cores (client asserts, server SKIPs for name
# symmetry); the surface switch itself is the Kconfig in controller.conf.
EXTRA_CMAKE_ARGS="-DOTBN_DELEG_VIA_PROXY=1" build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
EXTRA_CMAKE_ARGS="-DOTBN_DELEG_VIA_PROXY=1" build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"
echo ""
echo -e "${COLOR_GREEN}===== case_alt_via_proxy built (client calls plain mbedtls_ecdsa_*) =====${COLOR_RESET}"
print_launch_json "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
