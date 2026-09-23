#!/usr/bin/env bash
# i2cf 一次性判别实验（非回归用例）—— 无违规前置下 filter off 是否放行（H1 vs H2）
#
# 设计：reports/2026-09-23-1/i2c_failure_triage.md §3.4 试验 1
#   reset_all → switch_to_filter → filter_off → 上游读 0x33（前置无任何违规）
#   读成功 ⇒ H2（残留态，需显式清中断）；读失败 ⇒ H1（该模式不转发）
#
# 用法：
#   ./test.sh            # 只构建
#   ./test.sh -g         # 构建 + GDB 加载运行
#   ./test.sh -r         # 仅加载运行（需先构建）
# 采集（带串口）：见 tests/run_test.py / scripts/dbg_run.py
#
# ⚠️ 一次性诊断，不登记进 regression/cases.yaml，用完可整体删除。

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_filteroff_probe"

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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_i2cf_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" \
    "$(realpath "${SCRIPT_DIR}/filteroff_probe.conf")" "${TEST_BUILD_DIR}"
echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TEST_BUILD_DIR}"
else
if [ ! -f "${TARGET_ELF}" ]; then
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" \
    "$(realpath "${SCRIPT_DIR}/filteroff_probe.conf")" "${TEST_BUILD_DIR}"
fi
load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi
