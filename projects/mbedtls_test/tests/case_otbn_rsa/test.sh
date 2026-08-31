#!/usr/bin/env bash
# mbedtls single-core test (cpu1 secure domain, mbedtls API + LINKEDSEMI Kconfig routing)
#
# What is tested (case_otbn_rsa):
#   RSA OTBN ALT（2048/3072/4096，F4 keygen）。
#   验证 OTBN  RSA 私钥运算与 keygen 在不同 keylen 下的正确性，
#   覆盖 modexp、sign/verify 及 F4 公钥指数生成路径。
#
# CPU1 (single core, TGT):
#   - 单核工程，无对侧核；test_main → zt_init + zt_run_all 纯 ztest。
#
# Normal behavior:
#   每项测试 START → PASS（ztest 原生逐项输出）+ PROJECT EXECUTION SUCCESSFUL。
#   每项带 INFO 约定行（algo/vecs/us/keylen 等，§13.4）。
#
# Abnormal behavior:
#   任一项 FAIL 带 zassert 可归因 reason（byte[i] 0xXX!=0xYY / ret 码，§13.5）。
#
# Feedback:
#   cpu1 uart1（COM16/17，CH342）；banner 'CPU1 MBEDTLS' → TGT。
#
# Hardware IO connections:
#   无外部接线（mbedtls 为片内软件库 + 片内加密引擎/TRNG，CLAUDE.md §14 片内）。
#
# Build artifacts:
#   - tests/case_otbn_rsa/build/（zephyr.elf，dbg_run/dump/run 脚本共用此目录）
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_otbn_rsa"

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
    # 软件 RSA-2048 keygen + 百万 'a' 长报文，超时放宽
    RUN_TIMEOUT="${RUN_TIMEOUT:-600}"
fi

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_mbedtls_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
CASE_CONF="$(realpath "${SCRIPT_DIR}/case.conf")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
# 1. Build target image first（conf 差异经 CONF_FILE=prj.conf;case.conf 注入）.
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${CASE_CONF}" "${TEST_BUILD_DIR}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 2. Print test summary after build so it is not buried in build logs.
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}What: RSA OTBN ALT（2048/3072/4096，F4 keygen）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: 全项 PASS + PROJECT EXECUTION SUCCESSFUL（ztest 原生逐项输出）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: FAIL - <name> + zassert reason（byte[i] 0xXX!=0xYY / ret 码）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM16/17，CH342）；banner 'CPU1 MBEDTLS' → TGT。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: 无外部接线（片内软件库 + 片内加密引擎/TRNG，CLAUDE.md §14 片内标注）。${COLOR_RESET}"
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
