#!/usr/bin/env bash
# QSPI flash 测试 — 单核 case_no_calib（时序校准关闭 + 25 MHz 变体）
#
# What is tested:
#   与 case_default 完全相同的固件与用例集，差异只在 overlay：
#     - qspi2 关掉时序校准（timing-calibration-disabled）
#       → qspi_flash_calib/calib_status_expected 期望 QSPI_TIMING_CALIB_UNUSED
#     - flash2 降到 25 MHz
#   用于覆盖 qspi_ls_read_init()→qspi_ls_timing_calibration() 的
#   timing_calibration_disabled 早退分支（qspi_ls.c:952-955）。
#
# CPU1 behavior（单核工程，只有 cpu1 侧固件）：
#   上电 → 驱动 init（读 SFDP；跳过时序校准）→ ztest 四个套件。
#
# Normal behavior:
#   全部用例 PASS + `PROJECT EXECUTION SUCCESSFUL`。
#
# Abnormal behavior:
#   zassert 失败 → `Assertion failed at ...` + `FAIL - <name>` + `PROJECT EXECUTION FAILED`。
#
# Feedback:
#   串口 ztest 原生输出；串口不可用时
#   `python scripts/dump_results.py --case case_no_calib --json` 读内存兜底。
#
# Hardware IO connections:
#   零外部接线（板载 qspi2 NOR，pinmux 板载默认）。
#
# Build artifacts:
#   tests/case_no_calib/build/target/zephyr/zephyr.elf
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_no_calib"

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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_qspi_flash_test"
# 本 case 用自己的完整 overlay（DTC_OVERLAY_FILE 显式指定时不会自动叠加
# 应用目录的 app.overlay，故本文件必须自包含）
TARGET_OVERLAY="$(realpath "${SCRIPT_DIR}/case_no_calib.overlay")"
TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TEST_BUILD_DIR}"
echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TEST_BUILD_DIR}"
print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
else
# Agent mode: build if ELF missing, then load+run via GDB
if [ ! -f "${TARGET_ELF}" ]; then
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TEST_BUILD_DIR}"
fi
load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi
