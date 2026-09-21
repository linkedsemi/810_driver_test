#!/usr/bin/env bash
# MBOX dual-core test - invalid args (异常参数)
#
# What is tested:
#   对 mbox 驱动 API 的非法入参逐一断言返回码（判据全部在 controller 侧）：
#     send(NULL msg)              → -ENOTSUP（未开 signalling 模式）
#     send(channel = max_channels)→ -EINVAL
#     send(size = width+1)        → -EMSGSIZE（ch4 与 ch2 各一）
#     send(data=NULL, size>0)     → -EMSGSIZE
#     register_callback(非法通道) → -EINVAL
#     set_enabled(非法通道)       → -EINVAL
#     send(size = 0)              → 0（驱动现状：0 长帧被接受）
#     mbox_mtu_get(dev)           → -ENOSYS（驱动 api.mtu_get 未接线）
#
# CPU1 (target, uart1): 本 case 无行为判据，待机 4 s 后结束（上报
#                       MBOX_TEST:CTRL:INFO:tgt_invalid_idle:pass=1,guard=1 —— 结构性守卫，
#                       脚本不把它计入"全项 PASS"，见 COVERAGE §3.4）。
# CPU2 (controller, uart5): 执行 7 项非法/边界入参断言（判负）+ 2 项观察项
#                       （observe=1，不判负），汇总 fails 计数。
#
# Normal behavior: 两核均打印 PROJECT EXECUTION SUCCESSFUL；
#                  MBOX_TEST:<core>:PASS:inv_send_* 7 项；MBOX_TEST:CTRL:INFO:invalid_args:max_channels=6,fails=0。
#                  观察项 inv_zero_size_accepted / inv_mtu_get_unimplemented 记的是
#                  **驱动现状**（pass=1 = 现状与记录一致），不判负；驱动修复后翻
#                  OBS_DIFF 并提示同步改判。
# Abnormal behavior: 任一核打印 PROJECT EXECUTION FAILED（7 项返回值与契约不符）。
#
# Feedback: 串口逐项行 `MBOX_TEST:<core>:(PASS|FAIL):<name>[:<明细>]`（判定）+ `MBOX_TEST:<core>:INFO:<name>:pass=<0|1>,observe=1|guard=1[,<明细>]` + LOG_ERR 明细。
# Hardware IO connections: 无外部接线（mbox 走片内共享 SRAM）。
#
# Build artifacts: tests/case_mbox_invalid/build/{target,controller}/
# Usage:
#   ./test.sh          # build only (both cores)
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_mbox_invalid"

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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_target_mbox_test"
CONTROLLER_SRC="${I3C_TEST_ROOT}/cpu2_controller_mbox_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
CONTROLLER_OVERLAY="$(realpath "${CONTROLLER_SRC}/app.overlay")"
TARGET_CONF="$(realpath "${SCRIPT_DIR}/target.conf")"
CONTROLLER_CONF="$(realpath "${SCRIPT_DIR}/controller.conf")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_BUILD="${TEST_BUILD_DIR}/target"
CONTROLLER_BUILD="${TEST_BUILD_DIR}/controller"
TARGET_ELF="${TARGET_BUILD}/zephyr/zephyr.elf"
CONTROLLER_ELF="${CONTROLLER_BUILD}/zephyr/zephyr.elf"
CONTROLLER_BIN="${CONTROLLER_BUILD}/zephyr/zephyr.bin"

if [ "${AGENT_RUN}" != "y" ]; then
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"
echo -e "${COLOR_GREEN}Build OK (both cores):${COLOR_RESET} ${TEST_BUILD_DIR}"
else
if [ ! -f "${TARGET_ELF}" ]; then
build_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TARGET_BUILD}"
fi
if [ ! -f "${CONTROLLER_BIN}" ]; then
build_test "${TEST_NAME}" "${CONTROLLER_SRC}" "${CONTROLLER_OVERLAY}" "${CONTROLLER_CONF}" "${CONTROLLER_BUILD}"
fi
load_and_run "${TEST_NAME}" "${TARGET_ELF}" "${CONTROLLER_BIN}"
fi
