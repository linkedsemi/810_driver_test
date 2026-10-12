#!/usr/bin/env bash
# OTBN RSA key generation single-core test (cpu1 secure domain)
#
# What is tested:
#   otbn_rsa_keygen.c（OTBN 加速 RSA 密钥生成）+ 重构后的 rsa_alt.c keygen hook：
#   确定性（固定 RNG 流两次逐位相同）、2048/3072/4096 生成、正确性
#   （n=p*q / p,q 素数 / 3 mod 4 / e*d≡1 mod lcm）、直调 API
#   （check_prime / key_from_pq）、错误路径（rng 上抛 / 非 4 字节对齐 -EINVAL）、
#   session 不泄漏（连续 10 次 2048）、mbedtls_rsa_gen_key 走 OTBN hook 成功。
#
# CPU1 (target role, TGT token):
#   - 单核工程，无对侧核；main() 自驱动跑完全部套件。
#
# Normal behavior:
#   每项 START → PASS（ztest 原生逐项输出）+ PROJECT EXECUTION SUCCESSFUL。
#
# Abnormal behavior:
#   任一项 FAIL 带 zassert reason（ret 码 / byte[i] 0xXX!=0xYY / 判据描述）。
#
# Feedback:
#   cpu1 uart1（COM16/17，CH342）；banner 'CPU1 OTBN_KEYGEN' → TGT。
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
    # OTBN RSA-4096 keygen 慢（数十秒~分钟级），超时放宽
    RUN_TIMEOUT="${RUN_TIMEOUT:-900}"
fi

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_otbn_keygen_test"
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
echo -e "${COLOR_YELLOW}What: OTBN RSA 密钥生成（确定性 / 三尺寸 / 正确性 / 错误路径 / session / mbedtls hook）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: 全项 PASS + PROJECT EXECUTION SUCCESSFUL（ztest 原生逐项输出）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: FAIL - <name> + zassert reason（ret 码 / byte[i] 0xXX!=0xYY）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM16/17，CH342）；banner 'CPU1 OTBN_KEYGEN' → TGT。${COLOR_RESET}"
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
