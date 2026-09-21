#!/usr/bin/env bash
# app_iwdg_cpu2_test - case_reset
#
# What is tested:
#   app_iwdg 超时触发 **cpu2 HART 复位**，复位后由 cpu2 侧
#   reset_reason_init() 的 SYSC_APP_PER 分支回读原因 == APP_IWDT_HART_RESET。
#   这是 wdt_test（单核 cpu1）结构上覆盖不到的那条路径——app_iwdg 的复位
#   原因只有 cpu2 侧读得到（soc_reset_reason.c 的
#   DT_NODE_HAS_STATUS(DT_NODELABEL(cpu1), okay) 分支）。
#
# CPU1 (target, uart1): 常驻监督核，不在 APP_IWDT 复位域内，复位期间保持在线。
# CPU2 (controller, uart5): 被测核，__noinit 状态机跨复位推进
#                           Phase0 -> (HART reset) -> Phase1 -> Phase2
#
# Normal behavior:   Phase1 打印 APP_IWDG_RESET_PASS 且 PROJECT EXECUTION SUCCESSFUL。
# Abnormal behavior: APP_IWDG_RESET_FAIL（复位原因不符）或 PROJECT EXECUTION FAILED。
#
# Feedback:          串口 uart5 的 PHASE 行 + APP_IWDG_RESET_* 标记。
# Hardware IO connections: 无外部接线（片内外设）。
#
# ⚠ 破坏性：本 case 真实复位 cpu2，**不可进批量回归**（run_all.py）。
#    见 tests/COVERAGE.md §「不可进批量回归的 case」。
#    执行必须走 flash 固化流程（复位后要从 flash 重新引导；RAM-load 镜像
#    在 hart 复位后不会自行重启）：./run_reset.sh（未实机校准，P6 确认）。
#
# Build artifacts:   tests/case_reset/build/{target,controller}/
# Usage:
#   ./test.sh          # build only (both cores) —— 本脚本默认动作
#   ./test.sh -r       # 执行破坏性流程（转 run_reset.sh；load_and_run 不适用）
#   ./test.sh -g       # 同 -r

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_reset"

AUTO_GDB=""
while [ $# -gt 0 ]; do
    case "$1" in
        -g) AUTO_GDB="y"; shift ;;
        -r) AGENT_RUN="y"; shift ;;
        *)  echo "Unknown option: $1"; exit 1 ;;
    esac
done

# SKIP_LOAD：本 case 不走 GDB RAM load（load_and_run）——app_iwdg 的 HART
# 复位后固件必须能从 flash 重新引导，故只有"-只编译"与"转 run_reset.sh"
# 两种动作；SKIP_LOAD 恒为 y 语义，保留变量名以便与其余 case 的
# test_common.sh 约定一致。
SKIP_LOAD="y"

if [ "${AGENT_RUN}" = "y" ] || [ "${AUTO_GDB}" = "y" ]; then
    SKIP_LAUNCH_PRINT="y"
    echo "[${TEST_NAME}] destructive case: handing over to run_reset.sh"
    exec bash "${SCRIPT_DIR}/run_reset.sh"
fi

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_target_app_iwdg_test"
CONTROLLER_SRC="${I3C_TEST_ROOT}/cpu2_controller_app_iwdg_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
CONTROLLER_OVERLAY="$(realpath "${CONTROLLER_SRC}/app.overlay")"
CONTROLLER_CONF="$(realpath "${SCRIPT_DIR}/controller.conf")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_BUILD="${TEST_BUILD_DIR}/target"
CONTROLLER_BUILD="${TEST_BUILD_DIR}/controller"

build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TARGET_BUILD}"
echo -e "${COLOR_GREEN}Build OK (both cores):${COLOR_RESET} ${TEST_BUILD_DIR}"
echo ""
echo "本 case 为破坏性（cpu2 HART 复位），执行方式："
echo "  bash ${SCRIPT_DIR}/run_reset.sh"
