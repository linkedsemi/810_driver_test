#!/usr/bin/env bash
# MBOX dual-core test - RTC time sync
#
# What is tested:
#   RTC 双核时间同步（清单 TC-CPU1-RTC-02）：cpu1 读 RTC → 经 mbox ch4 发
#   时间戳（payload 同时带本地 RTC 走时推进量）→ cpu2 读自身 RTC 并比对。
#   另含 target 侧"两次读数间隔 300 ms 应前进 0..3 s"的走时推进项，以及
#   cpu2 侧两次同步的时间戳单调性项。
#
# CPU1 (target, uart1): 应答两轮 TS 请求，回本地 epoch 秒；轮间校验 RTC 推进。
# CPU2 (controller, uart5): 发两轮 TS 请求，读自身 RTC 比对差值（容忍 ±2 s）。
#
# Normal behavior: 两核均打印 PROJECT EXECUTION SUCCESSFUL；
#                  MBOX_TEST:<core>:PASS:rtc_sync_delta（|delta1|,|delta2| <= 2）、
#                  MBOX_TEST:<core>:PASS:rtc_sync_monotonic、MBOX_TEST:<core>:PASS:rtc_read_advances
#                  （tgt_advance_s ∈ [0,3]，两核各上报一次）。
# Abnormal behavior: 任一核打印 PROJECT EXECUTION FAILED（RTC 未就绪/差值超窗/非单调）。
#
# Feedback: 串口逐项行 `MBOX_TEST:<core>:(PASS|FAIL):<name>[:<明细>]`（判据见 tests/COVERAGE.md §3.6/§7）。
# Hardware IO connections: 无外部接线（mbox 走片内共享 SRAM；RTC 为片内外设）。
#
# 注：本 case 用独立 overlay 使能 &rtc（#include 复用工程既有 app.overlay）。
#
# Build artifacts: tests/case_mbox_rtc_sync/build/{target,controller}/
# Usage:
#   ./test.sh          # build only (both cores)
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_mbox_rtc_sync"

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
TARGET_OVERLAY="$(realpath "${SCRIPT_DIR}/target.overlay")"
CONTROLLER_OVERLAY="$(realpath "${SCRIPT_DIR}/controller.overlay")"
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
