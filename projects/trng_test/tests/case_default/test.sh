#!/usr/bin/env bash
# TRNG single-core test (cpu1 secure domain, DesignWare + APB TRNG)
#
# What is tested:
#   (A) trng1（entropy_ls_trng / DesignWare）全部功能路径：基本获取、非全 0/全 F、
#       ISR busywait、大请求（>128B pool）、无效参数、monobit 统计、多块唯一性、
#       64KB 吞吐（8x8192 分片，单次恰 2^19 bits 触发 ADVANCE_STATE 隐藏分支）。
#   (B) trng0（entropy_ls_apb_trng / APB ring-oscillator，独立模块）：基本获取、
#       非全 0/全 F、ISR busywait、大请求、无效参数、monobit、4096B 吞吐（MB/s）、
#       32×64B 唯一性。
#
# CPU1 (target role, TGT token):
#   - 单核工程，无对侧核；test_main 主动自驱动跑完所有 ZTEST。
#
# Normal behavior:
#   24 项测试全部 PASS（trng1 15 项 + trng0 9 项），结尾 SUMMARY:END 24/24 PASS + DONE:PASS。
#
# Abnormal behavior:
#   任一测试 FAIL 会打 FAIL token + 可归因 reason（§13.5）；
#   驱动/硬件坏时典型：entropy_get_entropy ret 非 0、全 0 输出、monobit 越界。
#
# Hardware IO connections:
#   无外部接线（片内模拟熵源：trng1 dwtrng@40004000 + trng0 trng@400a2000，
#   cpu1 安全域，§14 片内）。
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
    RUN_TIMEOUT="0"
fi

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_trng_test"
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
echo -e "${COLOR_YELLOW}What: trng1(DW) 15 项 + trng0(APB) 9 项：basic/非全0/非全F/ISR busywait/大请求/无效参数/monobit 统计/多块唯一性/吞吐（trng1 64KB、trng0 4096B）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: 9/9 PASS + DONE:PASS。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: FAIL token + 可归因 reason（如 ret -22 != -EINVAL / ones 越界）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM16/17，CH342）；banner 'CPU1 TRNG' → TGT。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: 无外部接线（片内熵源，CLAUDE.md §14 片内标注）。${COLOR_RESET}"
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
