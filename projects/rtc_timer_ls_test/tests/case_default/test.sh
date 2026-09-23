#!/usr/bin/env bash
# rtc_timer_ls single-core test (cpu1, BSTIM+PIS+GPTIMA software RTC)
#
# What is tested:
#   rtc_timer_ls set/get + calendar_valid 全边界（年/月/日/闰年/HMS/wday/yday/
#   isdst/nsec）、未初始化 get→-ENODATA、时间前进、年尾滚动抽样。
#
# Normal: 15/15 PASS + PROJECT EXECUTION SUCCESSFUL
# IO: 无外部接线（片内）
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + GDB load/run
#   ./test.sh -r       # load/run only

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_default"

AUTO_GDB=""
while [ $# -gt 0 ]; do
    case "$1" in
        -g) AUTO_GDB="y"; shift ;;
        -r) AGENT_RUN="y"; shift ;;
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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_rtc_timer_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TEST_BUILD_DIR}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}${COLOR_RESET}"
echo -e "${COLOR_YELLOW}What: rtc_timer_ls set/get + calendar_valid 全边界（15 项）。${COLOR_RESET}"
echo -e "${COLOR_YELLOW}Normal: 15/15 PASS。${COLOR_RESET}"
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1；banner 'CPU1 RTC_TIMER'。${COLOR_RESET}"
echo -e "${COLOR_YELLOW}IO: 无外部接线（片内）。${COLOR_RESET}"
echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TEST_BUILD_DIR}"
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
fi

if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi

if [ "${AGENT_RUN}" != "y" ] && [ "${AUTO_GDB}" != "y" ]; then
    echo ""
    echo -e "${COLOR_YELLOW}Hint:${COLOR_RESET} run with ${COLOR_GREEN}-g${COLOR_RESET} to load via GDB."
fi
