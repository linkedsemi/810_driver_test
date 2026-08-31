#!/usr/bin/env bash
# GPIO toggle test: PJ10/PJ11 1s-period square wave for scope-based clock check
#
# What is tested:
#   GPIO 基础输出功能 + 系统时钟链准确性：PJ10/PJ11（I3C5 的 SCL/SDA
#   引脚）以 1s 周期翻转（k_msleep(500) 半周期），示波器实测周期与
#   软件设定对比。用于排查 I2C 400K 实测 333K 的时钟源疑点。
#
# CPU1 (target role, TGT token):
#   - 单核工程；test_main 配置 PJ10/PJ11 为 GPIO 推挽输出后无限翻转，
#     按设计不返回（无 DONE）。
#
# Normal behavior:
#   串口输出 BOOT + START:io_toggle_1s + INFO(pins/period/ticks_per_sec)，
#   随后 PJ10/PJ11 上持续 1s 周期方波。
#
# Abnormal behavior:
#   - 串口无 BOOT → 烧录链路/环境问题；
#   - 方波周期偏离 1s → 时钟源/分频不准（按比例推算实际时钟）。
#
# Feedback:
#   - UART1 (cpu1, CH342 COM16/17)：BOOT/START/INFO token。
#   - 示波器探头：PJ10（I3C5_SCL 焊点）、PJ11（I3C5_SDA 焊点）。
#
# Hardware IO connections:
#   - PJ10/PJ11 即 I3C5/I2C5 总线焊点，示波器直接挂总线处即可。
#
# Build artifacts:
#   - tests/case_toggle_1s/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_toggle_1s"

# 单核单系统（1os）：PJ10/PJ11 归 cpu1 独占。
# 2os 下 cpu2 可能从 SRAM 里的旧镜像启动（reset 不清 RAM），旧 i2c5 master
# 会驱动同一对引脚干扰波形；1os 不启动 cpu2，无此风险。
# 1os 的 cpu1 镜像入口/链接基址为 0x10000000（区别于 2os 的 0x100c0000）。
export TARGET_BOARD="lsqsh_evb@1os/lsqsh/cpu1"
export TARGET_ROM_START="0x10000000"

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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_gpio_toggle"
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
echo -e "${COLOR_YELLOW}What: PJ10/PJ11 (I3C5 pins) toggle at 1s period for scope clock check.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: BOOT + START:io_toggle_1s + INFO on UART1; 1s square wave on pins.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: no BOOT (flash/env issue); period off 1s (clock chain off).${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: UART1 (cpu1) tokens; scope probes on PJ10/PJ11.${COLOR_RESET}"
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
