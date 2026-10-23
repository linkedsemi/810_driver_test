#!/usr/bin/env bash
# I2C filter (PFR/TMP) driver test (single-core cpu1 secure domain)
#
# What is tested:
#   ★ 一次性诊断（非回归用例，复用 case_scan_burst 的 marker 块）：
#   **日志配置换成客户那套**（SHELL=y + SHELL_LOG_BACKEND=y + SLEEP_MS=1000 +
#   QUEUE_SIZE=512 + LOG_BUFFER_SIZE=8192 + LOG_PRINTK=y + LOG_BACKEND_UART=n）
#   后的日志驱逐跨度 —— 复现客户「ERR 日志 20+ 秒才排空」的劣化。
#   固件动作与 case_scan_burst **逐字节相同**（filter 清空 + 上游逐址扫 0x08..0x77），
#   差异**只在 conf** ⇒ 两 case 的排空跨度之差 = 日志配置的代价。
#   ⚠️ 必须配带到达时间戳的采集（见 case_scan_burst/README.md）；跑完可整体删除。
#
# CPU1 (target role, TGT token):
#   - Single-core project, no peer core; test_main drives all ZTESTs itself.
#
# Normal behavior:
#   scan_burst_drain PASS（isr 计数 == 探测地址数）。
#   ★ 本用例的**重点产出不是 PASS/FAIL**，而是 SCAN_BURST_BEGIN / END 两行的
#     到达时间差 —— 那才是「打印慢」的量化值。
#   ⚠️ SHELL=y ⇒ shell 提示符可能与 ztest 输出混流，解析按 token 规则（re.search 不锚点）。
#
# Abnormal behavior:
#   Any FAIL prints the zassert reason (file:line (func): msg, with expected vs
#   actual values); a broken driver/HW typically shows ret != 0 or a register
#   read-back mismatch.
#
# Hardware IO connections:
#   外部: **零外部器件** —— 从设备 = 片内 target eeprom@33（挂 i2c3）；上游主机 i2c2 跳线到 i2cf4 输入、i2c3 跳线到 i2cf4 输出。
#
# Build artifacts:
#   - tests/case_scan_burst_shell/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_scan_burst_shell_d"

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
    "$(realpath "${SCRIPT_DIR}/shell_d.conf")" "${TEST_BUILD_DIR}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 2. Print test summary after build so it is not buried in build logs.
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}What: 客户日志配置（SHELL=y+SHELL_LOG_BACKEND=y, SLEEP_MS=1000, QUEUE=512, BUF=8192, UART backend=n）下的日志驱逐跨度复现。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: scan_burst_drain PASS；重点看 SCAN_BURST_BEGIN->END 到达时间差。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: FAIL - <name> + zassert reason (file:line, expected vs actual).${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM16/17, CH342）；banner 'CPU1 I2CF' -> TGT。${COLOR_RESET}"
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
