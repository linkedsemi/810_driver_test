#!/usr/bin/env bash
# RTC single-core test (cpu1 secure domain, rtc@4006fc00)
#
# What is tested:
#   rtc_ls 驱动的全部功能路径：set/get 往返、时间前进、非法 set 参数、
#   闹钟 set/get 往返、闹钟中断触发（+2s）、非法闹钟参数、校准往返
#   （+1e8 ppb 精确回读）、负校准值（floor 分支）、get_calibration(NULL)。
#
# CPU1 (target role, TGT token):
#   - 单核工程，无对侧核；test_main 主动自驱动跑完所有 ZTEST。
#
# Normal behavior:
#   9 项测试全部 PASS，结尾 SUMMARY:END 9/9 PASS + DONE:PASS。
#
# Abnormal behavior:
#   任一测试 FAIL 会打 FAIL token + 可归因 reason（§13.5）；
#   驱动/硬件坏时典型：set/get ret 非 0、alarm 不触发、calibration 回读不等。
#
# Hardware IO connections:
#   无外部接线（RTC 为片内 PMU 域寄存器外设 rtc@4006fc00，cpu1 安全域，§14 片内）。
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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_rtc_test"
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
echo -e "${COLOR_YELLOW}What: rtc_ls 全路径：set/get 往返/时间前进/非法参数/闹钟 set-get/闹钟中断触发/非法闹钟参数/校准往返/负校准/NULL 参数。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: 9/9 PASS + DONE:PASS。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: FAIL token + 可归因 reason（如 ret -22 != -EINVAL / alarm 未触发）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM16/17，CH342）；banner 'CPU1 RTC' → TGT。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: 无外部接线（片内 PMU 域外设，CLAUDE.md §14 片内标注）。${COLOR_RESET}"
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
