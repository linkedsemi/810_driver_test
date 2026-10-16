#!/usr/bin/env bash
# I2C filter (PFR/TMP) —— I2CF 状态枚举矩阵一次性探针（单核 cpu1）
#
# What is tested:
#   ★ 一次性探针（非回归）：把「白名单内/外 × 读/写 × 有/无器件 × 滤波器开/关
#   → 反馈状态（ret / 是否上报 / ERR 日志 / SMBF_NONWHITELIST）」的枚举表填满。
#   重点补 #5「白名单内 + 无器件」（区分"按白名单判"与"按总线应答判"）。
#   每格前 cold_reset + 关全部通道（单变量 + 组间复位）；每格成对 LOG_INF marker
#   （EM_BEGIN/EM_END）包住 3 笔事务，逐笔记 ret / ISR 增量 / 读回数据。
#
# CPU1 (target role, TGT token):
#   - Single-core project, no peer core; test_main 进入循环枚举探针（不跑 ztest）。
#
# Normal behavior:
#   不判 PASS/FAIL（无 zassert）；固件持续输出 EM_* 行与驱动 ERR 行。
#
# Abnormal behavior:
#   无（本固件不做判定）；若串口无任何 EM_ 行 ⇒ 固件未跑（查连接/烧录）。
#
# Hardware IO connections:
#   上游 i2c2(PB04/PB01) 跳线到 i2cf4 输入 SCLI/SDAI(PE03/PB02)；
#   i2cf4 输出 SCLO/SDAO(PE02/PB00) 跳线到 i2c3(从机)。从设备 = 片内 target eeprom@33。
#   地址 0x33 = 有器件；0x2A = 下游无器件（实测应答清单 = 33 50）。
#
# Build artifacts:
#   - tests/case_enum_matrix/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_enum_matrix"

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
    "$(realpath "${SCRIPT_DIR}/case.conf")" "${TEST_BUILD_DIR}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 2. Print test summary after build so it is not buried in build logs.
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}（I2CF 状态枚举矩阵，一次性探针）${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}What: 白名单内/外 × 读/写 × 有/无器件 × 滤波器开/关 → ret/isr/数据/ERR 日志/SMBF_NONWHITELIST。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: 固件持续输出 EM_* 行（不判 PASS/FAIL）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM16/17, CH342）；banner 'CPU1 I2CF' -> TGT。${COLOR_RESET}"
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
#    -g：AUTO_GDB=y ⇒ SKIP_LOAD 未置 ⇒ 走到这里。
#    -r：AGENT_RUN=y ⇒ 也走到这里。
if [ "${SKIP_LOAD}" != "y" ] || [ "${AGENT_RUN}" = "y" ]; then
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
