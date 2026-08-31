#!/usr/bin/env bash
# OTBN selftest single-core test (cpu1 secure domain)
#
# What is tested:
#   mbedtls selftest 全量套件（samples/mbedtls_selftest 移植，逐项 token 化）：
#   calloc/平台 sanity、md5/ripemd160/sha1/sha224/256/384/512/sha3、des/aes/gcm/ccm/
#   nist_kw/cmac/chacha20/poly1305/chachapoly、base64/mpi/rsa/camellia/aria、
#   ctr_drbg/hmac_drbg/ecp/ecjpake/dhm/entropy/pkcs5/memory_buffer_alloc。
#   硬件路径：SHA 走硬件引擎（HARDWARE_ALT），RSA/ECDSA 走 OTBN ALT（与样本一致）。
#
# CPU1 (target role, TGT token):
#   - 单核工程，无对侧核；main() 主动自驱动跑完全部套件。
#
# Normal behavior:
#   各套件 START→PASS（未编译的套件 NOT_RUN），结尾 SUMMARY:END + DONE:PASS。
#
# Abnormal behavior:
#   套件 FAIL 打 FAIL token + reason（suite_ret=N），细节看 verbose 输出的
#   "FAILED" 行。
#
# Feedback:
#   cpu1 uart1（COM16/17，CH342）；banner 'CPU1 OTBN_SELFTEST' → TGT。
#
# Hardware IO connections:
#   无外部接线（片内测试，CLAUDE.md §14 片内标注）。
#
# Build artifacts:
#   - tests/case_default/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_default"

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
    RUN_TIMEOUT="${RUN_TIMEOUT:-180}"
fi

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_otbn_selftest"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
# 1. Build target image first.
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TEST_BUILD_DIR}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 2. Print test summary after build so it is not buried in build logs.
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}What: mbedtls selftest 全量套件（SHA 硬件引擎 + RSA/ECDSA OTBN）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: 全部编译套件 PASS + DONE:PASS。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: FAIL token + suite_ret reason；细节看 verbose FAILED 行。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM16/17，CH342）；banner 'CPU1 OTBN_SELFTEST' → TGT。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: 无外部接线（片内，CLAUDE.md §14 片内标注）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET}"
echo "  ${TEST_BUILD_DIR}"
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""

fi

if [ "${AGENT_RUN}" != "y" ]; then
# 3. Print launch.json snippet for manual copy-paste.
print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
fi

# 4. Optionally load into RAM and run.
if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
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
