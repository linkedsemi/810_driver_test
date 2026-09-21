#!/usr/bin/env bash
# SPI filter (PFR/TMP) driver test (single-core cpu1 secure domain)
#
# What is tested:
#   pfr_tpm spi_filter 全 API：cold_reset 终态(CFG/TARGET_ADDR/BCMD/INTR_MASK/固定命令表) / 命令表 add-remove 与 40 槽容量边界 / 固定表 idx 存取 / 地址白名单显式与掩码填充 / 地址权限守卫与位回读 / SCK 阈值与中断掩码 / 3B-4B 模式位 / flash_size 整除边界 / IP_LOCK 语义 / pinctrl filter-master 切换 / master 模式读 flash JEDEC ID。
#
# CPU1 (target role, TGT token):
#   - Single-core project, no peer core; test_main drives all ZTESTs itself.
#
# Normal behavior:
#   All items PASS + PROJECT EXECUTION SUCCESSFUL.
#
# Abnormal behavior:
#   Any FAIL prints the zassert reason (file:line (func): msg, with expected vs
#   actual values); a broken driver/HW typically shows ret != 0 or a register
#   read-back mismatch.
#
# Hardware IO connections:
#   外部: SPIF 的 in/out 管脚在板级连到 spi4(CS0=GPIOI 0) 上挂的 NOR flash（master 模式用例经该路径直接驱动 flash）。其余寄存器级用例零接线。
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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_spif_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
# 1. Build target image first.
#    Board 走 test_common.sh 的 SINGLE_BOARD 变量（单核 = 1os 变体）——
#    单核固件一律 1os 编译测试（SPEC_REFERENCE §6.1.10，禁在此硬编码 board）。
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TEST_BUILD_DIR}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 2. Print test summary after build so it is not buried in build logs.
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}What: pfr_tpm spi_filter 全 API：cold_reset 终态(CFG/TARGET_ADDR/BCMD/INTR_MASK/固定命令表) / 命令表 add-remove 与 40 槽容量边界 / 固定表 idx 存取 / 地址白名单显式与掩码填充 / 地址权限守卫与位回读 / SCK 阈值与中断掩码 / 3B-4B 模式位 / flash_size 整除边界 / IP_LOCK 语义 / pinctrl filter-master 切换 / master 模式读 flash JEDEC ID。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: all items PASS + PROJECT EXECUTION SUCCESSFUL.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: FAIL - <name> + zassert reason (file:line, expected vs actual).${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM16/17, CH342）；banner 'CPU1 SPIF' -> TGT.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: 外部: SPIF 的 in/out 管脚在板级连到 spi4(CS0=GPIOI 0) 上挂的 NOR flash（master 模式用例经该路径直接驱动 flash）。其余寄存器级用例零接线。${COLOR_RESET}"
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
