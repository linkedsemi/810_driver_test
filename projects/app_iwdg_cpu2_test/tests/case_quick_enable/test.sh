#!/usr/bin/env bash
# app_iwdg_cpu2_test - case_quick_enable
#
# What is tested:
#   驱动 iwdt_init 的 quick_enable 正常分支（wdt_iwdg_ls.c:172-184）：
#   DT 声明 quick-enable + quick-timeout-ms = 60000 时，POST_KERNEL 阶段
#   IWDT_LOAD = 1966080、IWDT_CTRL = RST_EN|EN；feed/disable 通路可用。
#
# CPU1 (target, uart1): 常驻监督核，app_iwdg_domain 结构守卫套件（2 项）。
# CPU2 (controller, uart5): 被测核，app_iwdg_quick 套件（1 项，内部固定顺序）。
#
# Normal behavior:   两核各自 PROJECT EXECUTION SUCCESSFUL，全部项 PASS。
# Abnormal behavior: 启动态 load/ctrl 不符预期（quick-enable 未生效）-> FAIL。
#
# Feedback:          串口 ztest 原生输出 + INFO 约定行。
# Hardware IO connections: 无外部接线（片内外设）。
#
# 注：本 case 通过 controller.overlay 给 app_iwdg 加 quick-enable 属性；
#     该项是本案新开独立工程后新增的覆盖能力（sec_iwdg_cpu1_test 受"只增不改"约束
#     无法引入该 DT 配置）。
#
# Build artifacts:   tests/case_quick_enable/build/{target,controller}/
# Usage:
#   ./test.sh          # build only (both cores)
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_quick_enable"

AUTO_GDB=""
while [ $# -gt 0 ]; do
    case "$1" in
        -g) AUTO_GDB="y"; shift ;;
        -r) AGENT_RUN="y"; shift ;;
        *)  echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [ "${AUTO_GDB}" != "y" ]; then SKIP_LOAD="y"; fi
if [ "${AGENT_RUN}" = "y" ]; then SKIP_LAUNCH_PRINT="y"; RUN_TIMEOUT="0"; fi

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_target_app_iwdg_test"
CONTROLLER_SRC="${I3C_TEST_ROOT}/cpu2_controller_app_iwdg_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
CONTROLLER_OVERLAY="$(realpath "${SCRIPT_DIR}/controller.overlay")"
CONTROLLER_CONF="$(realpath "${SCRIPT_DIR}/controller.conf")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_BUILD="${TEST_BUILD_DIR}/target"
CONTROLLER_BUILD="${TEST_BUILD_DIR}/controller"
TARGET_ELF="${TARGET_BUILD}/zephyr/zephyr.elf"
CONTROLLER_BIN="${CONTROLLER_BUILD}/zephyr/zephyr.bin"

if [ "${AGENT_RUN}" != "y" ]; then
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TARGET_BUILD}"
echo -e "${COLOR_GREEN}Build OK (both cores):${COLOR_RESET} ${TEST_BUILD_DIR}"
else
if [ ! -f "${TARGET_ELF}" ]; then
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TARGET_BUILD}"
fi
if [ ! -f "${CONTROLLER_BIN}" ]; then
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
fi
load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
