#!/usr/bin/env bash
# IO interconnect probe (single-core cpu1 secure domain)
#
# What is tested:
#   实测 IO 互联矩阵：逐个把候选脚驱动为**低**（其余保持输入 + 内部上拉），
#   采样其它脚来判断哪些脚之间真的导通。候选集 = i2c2/3/4、i2cf2/3/4、
#   spi1/3/4、spif1..4、i3c5/i3c6 的全部信号脚（75 根，见 tests/COVERAGE.md §2）。
#
# CPU1 (target role, TGT token):
#   - Single-core project, no peer core; test_main drives all ZTESTs itself.
#
# Normal behavior:
#   All items PASS + PROJECT EXECUTION SUCCESSFUL.
#   其中 io_probe_positive_control_i3c_link 必须在 PJ10<->PJ08 / PJ11<->PJ09
#   上测出连通 —— 那是已确认接好的跨核跳线，是"方法本身没坏"的判据。
#
# Abnormal behavior:
#   - positive_control_i3c_link FAIL  => 测量方法失效（或该跳线掉了），
#     本次矩阵**不可采信**，不要按矩阵下结论。
#   - pin_controllable FAIL           => 有脚根本没被 GPIO 接管，见 IOPROBE ANOMALY 行。
#   - 用户声称的接线测不出来**不判 FAIL** —— 那正是本工程要交付的信息，
#     结论以串口里的 "IOPROBE CLAIM ..." 与矩阵为准。
#
# Hardware IO connections:
#   外部: 用户手工跳线（i2c2/i2c3/i2c4 互联；SPI 与 SPIF 互联）+ 已确认的
#   I3C5<->I3C6 跨核连线（阳性对照用）。本工程只驱动 LOW，永不驱动 HIGH。
#   ⚠️ 板卡必须离线运行时不要误跑其它工程：本工程会把 spi3/spi4 的 CS 脚
#      短暂拉低（无时钟，被选中的 NOR flash 不会输出），其余脚均为无害拉低。
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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_io_probe_test"
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
echo -e "${COLOR_YELLOW}What: 实测 IO 互联矩阵。逐脚驱动 LOW（只拉低、永不推高），其余脚输入+内部上拉，采样判连通。候选 75 脚：i2c2/3/4、i2cf2/3/4、spi1/3/4、spif1..4、i3c5/i3c6。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: all items PASS + PROJECT EXECUTION SUCCESSFUL；其中 positive_control_i3c_link 必须测出 PJ10<->PJ08 / PJ11<->PJ09 连通。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: positive_control_i3c_link FAIL = 方法失效，矩阵不可采信；pin_controllable FAIL = 有脚未受 GPIO 接管。用户声称的接线测不出**不判 FAIL**。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM16/17, CH342）；banner 'CPU1 IOPROBE' -> TGT。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: 外部: 用户手工跳线（i2c2/i2c3/i2c4 互联、SPI 与 SPIF 互联）+ 已确认的 I3C5<->I3C6 跨核连线（阳性对照）。全程只驱动 LOW。${COLOR_RESET}"
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
