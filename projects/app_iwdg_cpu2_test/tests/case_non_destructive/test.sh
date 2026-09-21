#!/usr/bin/env bash
# app_iwdg_cpu2_test - case_non_destructive
#
# What is tested:
#   app_iwdg@0x40099000（cpu2 应用域 IWDG，驱动 zephyr/drivers/watchdog/wdt_iwdg_ls.c）
#   非破坏性通路：install/setup/feed/disable、窗口与 flags 异常参数、
#   callback 注册分支、setup 错误分支、timeout_ms -> IWDT_LOAD 换算边界。
#   不触发任何复位（setup 成功的项在断言后立即 disable）。
#
# CPU1 (target, uart1): 常驻监督核，跑 app_iwdg_domain 结构守卫套件（2 项）。
# CPU2 (controller, uart5): 被测核，跑 app_iwdg 套件（16 项）。
#
# Normal behavior:   两核各自 PROJECT EXECUTION SUCCESSFUL，全部项 PASS。
# Abnormal behavior: 任一核出现 FAIL 项或 PROJECT EXECUTION FAILED。
#                    （喂狗失效会让 cpu2 被 app_iwdg 复位，本 case 表现为重启）
#
# Feedback:          串口 ztest 原生输出 + INFO 约定行（§6.5.4）。
# Hardware IO connections: 无外部接线（IWDG 为片内外设，SPEC_REFERENCE §6.6）。
#
# Build artifacts:   tests/case_non_destructive/build/{target,controller}/
# Usage:
#   ./test.sh          # build only (both cores)
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_non_destructive"

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
CONTROLLER_OVERLAY="$(realpath "${CONTROLLER_SRC}/app.overlay")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_BUILD="${TEST_BUILD_DIR}/target"
CONTROLLER_BUILD="${TEST_BUILD_DIR}/controller"
TARGET_ELF="${TARGET_BUILD}/zephyr/zephyr.elf"
CONTROLLER_BIN="${CONTROLLER_BUILD}/zephyr/zephyr.bin"

if [ "${AGENT_RUN}" != "y" ]; then
# ★ 双核工程用 build_test（_detect_board 按目录名选 2os cpu1/cpu2 board）。
#   case 变体只体现在 controller.overlay/controller.conf（本 case 用工程默认值）。
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "" "${CONTROLLER_BUILD}"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TARGET_BUILD}"
echo -e "${COLOR_GREEN}Build OK (both cores):${COLOR_RESET} ${TEST_BUILD_DIR}"
else
if [ ! -f "${TARGET_ELF}" ]; then
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TARGET_BUILD}"
fi
if [ ! -f "${CONTROLLER_BIN}" ]; then
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "" "${CONTROLLER_BUILD}"
fi
load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
