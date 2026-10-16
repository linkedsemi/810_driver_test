#!/usr/bin/env bash
# I2C filter (PFR/TMP) driver test (single-core cpu1 secure domain)
#
# What is tested:
#   ★ 一次性诊断（非回归用例）：quick command（0 长度写）扫描的单笔耗时。
#   验证客户反馈的 200.2 ms/笔（≈ LS_I2C_BUS_TIMEOUT_MS 默认 200）与
#   i2cf_test 既有 1 字节写 0.14 ms/笔 的落差。三相位：
#     A 被拦 quick command（空白名单 + filter on）
#     B 对照 quick command（filter off）
#     C 基线 1 字节写（filter on + 空白名单，复刻 scan_burst）
#
# CPU1 (target role, TGT token):
#   - Single-core project, no peer core; test_main drives all ZTESTs itself.
#
# Normal behavior:
#   quick_scan PASS（本用例只记账、不置时序断言，结论由 QUICKSCAN_* 日志承载）。
#   ★ 重点产出是 QUICKSCAN_A/B/C_* 三行与逐址 dt 列表，以及串口 "i2c bus timeout" 计数。
#
# Abnormal behavior:
#   Any FAIL prints the zassert reason (file:line (func): msg, with expected vs actual).
#
# Hardware IO connections:
#   外部: **零外部器件** —— 从设备 = 片内 target eeprom@33（挂 i2c3）；上游主机 i2c2 跳线到 i2cf4 输入、i2c3 跳线到 i2cf4 输出。
#
# Build artifacts:
#   - tests/case_quick_scan/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_quick_scan"

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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_i2cf_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
# 1. Build target image first.
#    Board 走 test_common.sh 的 SINGLE_BOARD 变量（单核 = 1os 变体）——
#    单核固件一律 1os 编译测试（SPEC_REFERENCE §6.1.10，禁在此硬编码 board）。
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" \
    "$(realpath "${SCRIPT_DIR}/quick_scan.conf")" "${TEST_BUILD_DIR}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 2. Print test summary after build so it is not buried in build logs.
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}What: quick command（0 长度写）扫描单笔耗时 —— A 被拦 quick command / B 对照 filter off / C 基线 1 字节写。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: quick_scan PASS（只记账；结论看 QUICKSCAN_* 日志与 i2c bus timeout 计数）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: FAIL - <name> + zassert reason (file:line, expected vs actual).${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM16/17, CH342）；banner 'CPU1 I2CF' -> TGT.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: 零外部器件 —— 从设备 = 片内 target eeprom@33（挂 i2c3）；i2c2 跳线到 i2cf4 输入、i2c3 跳线到 i2cf4 输出。${COLOR_RESET}"
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
