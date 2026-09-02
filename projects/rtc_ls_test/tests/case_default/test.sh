#!/usr/bin/env bash
# RTC (rtc_ls) single-core test (cpu1 secure domain, rtc@4006fc00)
#
# What is tested:
#   rtc_ls 驱动全路径：set/get、时间前进、非法参数、闹钟 set/get/触发、
#   校准往返/负值/NULL、2026 全年日历进位 + 2028 闰年二月。
#
# CPU1 (TGT token): 单核工程，test_main 顺序跑完所有 ZTEST。
#
# Normal: 10/10 PASS + RTC_TEST:DONE:PASS
#
# Build artifacts: tests/case_default/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_rtc_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TEST_BUILD_DIR}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME} (rtc_ls_test)${COLOR_RESET}"
echo -e "${COLOR_YELLOW}What: rtc_ls 全路径 10 项 ZTEST（含 calendar_rollover）。${COLOR_RESET}"
echo -e "${COLOR_YELLOW}Normal: 10/10 PASS + DONE:PASS。${COLOR_RESET}"
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1；banner 'CPU1 RTC'。${COLOR_RESET}"
echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TEST_BUILD_DIR}"
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
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
    echo -e "${COLOR_YELLOW}Hint:${COLOR_RESET} run with ${COLOR_GREEN}-g${COLOR_RESET} to load and run via GDB."
    echo -e "  ${COLOR_CYAN}$0 -g${COLOR_RESET}"
    echo ""
fi
