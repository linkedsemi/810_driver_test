#!/usr/bin/env bash
# OTBN ECDSA delegation, EXPLICIT entry points (case_explicit_api)
#
# What is tested:
#   The SAME delegation matrix as case_default, but the cpu2 (CLIENT) issues each
#   operation through the EXPLICIT API -- mbedtls_ls_otbn_ecdsa_{genkey,sign,
#   verify} -- instead of the mbedtls_ecdsa_* names. On the client the two are
#   related by a thin forward (mbedtls_ecdsa_* -> mbedtls_ls_otbn_ecdsa_*), so
#   this case is about the explicitly-named surface and the public header
#   mbedtls_otbn_ecdsa_mbox.h being usable from an application.
#
#   Surface selection: -DOTBN_DELEG_EXPLICIT_API=1 (both cores; on the server the
#   macro only registers the equivalence item, see below) steers the shared body
#   tests/src/otbn_delegation_test.c via the DELEG_* redirect macros at its top.
#
# CPU2 (controller, uart5): delegation CLIENT -- t01..t88 call the explicit
#                           mbedtls_ls_otbn_ecdsa_* names; each is a real mbox
#                           round-trip to cpu1.
# CPU1 (target, uart1):     delegation SERVER -- keeps calling mbedtls_ecdsa_*
#                           locally (the image has no explicit entry point).
#
# Extra item (vs case_default), the case's independent value:
#   t16d_mbedtls_and_explicit_agree -- same request through mbedtls_ecdsa_genkey
#   (the forwarder) AND mbedtls_ls_otbn_ecdsa_genkey (its target) in the SAME
#   client image; both must succeed. This proves at RUN time that the forwarder
#   is alive, which link-time symbol presence alone cannot show. Client asserts;
#   server SKIPs (no explicit entry point there).
#
# Normal behavior (prediction, to be confirmed on hardware):
#   Same per-item outcome as case_default -- the five product-defect negatives
#   (t17/t88 sign d=0/d>=n, t19/t89 bad Q, t21 wrong keypair) still FAIL and the
#   panics still truncate the suite, because delegation semantics are unchanged.
#   t16d PASSes on the client.
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_explicit_api"

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
# Explicit-entry surface on BOTH cores (client: explicit names; server: the
# equivalence item only, its matrix keeps the mbedtls_* names).
EXTRA_CMAKE_ARGS="-DOTBN_DELEG_EXPLICIT_API=1" build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
EXTRA_CMAKE_ARGS="-DOTBN_DELEG_EXPLICIT_API=1" build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"
echo ""
echo -e "${COLOR_GREEN}===== case_explicit_api built (client calls mbedtls_ls_otbn_ecdsa_*) =====${COLOR_RESET}"
print_launch_json "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi

if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
