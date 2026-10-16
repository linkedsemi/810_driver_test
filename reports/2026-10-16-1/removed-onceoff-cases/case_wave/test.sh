#!/usr/bin/env bash
# I2C filter (PFR/TMP) —— 循环波形发生器（单核 cpu1，示波器抓取用）
#
# What is tested:
#   ★ 一次性波形用例（非回归）：让上游 master(i2c2) 以 ~420ms 周期反复发
#   **一笔写 + 一笔读**（地址 0x33 = i2c3 上的片内 target，位于 i2cf4 输出侧/下游）。
#   两相位都 filter ON，只切「0x33 是否在白名单」：A=授权(ACK) / B=关 ch0(NACK+上报)。
#   目的 = 供示波器/逻辑分析仪对比"授权 vs 未授权"两段（并译码 read 方向 LSB 与 DIR 是否一致）。
#
# CPU1 (target role, TGT token):
#   - Single-core project, no peer core; test_main 进入永不返回的波形循环（不跑 ztest）。
#
# Normal behavior:
#   不判 PASS/FAIL（无 zassert）；固件持续输出 [WAVE] 行（每轮一条，含 ret/ISR/period）。
#
# Abnormal behavior:
#   无（本固件不做判定）；若串口无任何 [WAVE] 行 ⇒ 固件未跑（查连接/烧录）。
#
# Marker GPIO (probe these):
#   TRIG  = PA06   事务包络（事务前建立、事务后撤销）
#   DIR   = PA04   写=低 / 读=高
#   PHASE = PA05   A=低（0x33 已授权）/ B=高（0x33 未授权）
#
# Hardware IO connections:
#   上游 i2c2(PB04/PB01) 跳线到 i2cf4 输入 SCLI/SDAI(PE03/PB02)；
#   i2cf4 输出 SCLO/SDAO(PE02/PB00) 跳线到 i2c3(从机)。从设备 = 片内 target eeprom@33。
#   探 SCL/SDA 优先探**输入侧** PE03(SCL)/PB02(SDA) —— 那才是上游 master 实际发出的帧。
#
# Build artifacts:
#   - tests/case_wave/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_wave"

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
    "$(realpath "${SCRIPT_DIR}/wave.conf")" "${TEST_BUILD_DIR}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 2. Print test summary after build so it is not buried in build logs.
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}（循环波形发生器，非回归）${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}What: i2c2 以 ~420ms 周期发 一笔写+一笔读(0x33=片内target)；两相位都 filter on，只切 0x33 授权(A)/不授权(B)，标记 GPIO 供示波器抓取。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: 固件持续输出 [WAVE] 行（不判 PASS/FAIL）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Marker: TRIG=PA06 DIR=PA04 PHASE=PA05；SCL/SDA 探 i2cf4 输入侧 PE03/PB02。${COLOR_RESET}"
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
#    -r：AGENT_RUN=y（本工程旧式 test.sh 无 AGENT_RUN 加载分支，补上）⇒ 也走到这里。
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
