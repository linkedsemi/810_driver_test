#!/usr/bin/env bash
# PWM interface-availability test (single-core, lsqsh_evb@1os/lsqsh/cpu1)
#
# What is tested:
#   LinkedSemi PWM 驱动 zephyr/drivers/pwm/pwm_ls.c 的 API 层契约：
#   init 通路（clock/reset/pinctrl + PWM_PRE_DIV 写入）、set_cycles 周期/脉宽回读、
#   极性标志位、get_cycles_per_sec 频率契约、ns→cycles 换算路径、pwm_dt_spec 路径、
#   非法通道 / 非法周期脉宽 / 16 位量程边界、多通道隔离、并发写入一致性、
#   以及"驱动无 disable 通路"的契约边界。
#
# CPU1 behavior:
#   单核工程：只有 cpu1 侧固件，测试线程直接以 DEVICE_DT_GET(DT_NODELABEL(pwm))
#   调用 PWM API，并回读 PWM_PARAM/PWM_CTRL 寄存器做断言。
#
# Normal behavior:
#   全部 12 项 PASS - <name>，末尾 PROJECT EXECUTION SUCCESSFUL。
#
# Abnormal behavior:
#   非法入参返回 -EINVAL；越界调用不得改动已配置寄存器（断言寄存器保持原值）。
#
# Feedback:
#   ztest 原生输出（ZT_BOOT / START / PASS|FAIL / TESTSUITE / PROJECT EXECUTION）；
#   实测数值经 INFO <name>: k=v 行上报（解析见 run_test.py）。
#
# Hardware IO connections:
#   无外部接线（片内 PWM 控制器，仅接口可用性）。
#   **波形正确性未验证**：本工程无示波器/逻辑分析仪观测，只验 API 层契约，
#   波形是否正确需用户用示波器/逻辑分析仪确认（见 tests/COVERAGE.md 顶部备注）。
#
# Build artifacts:
#   tests/case_default/build/target/zephyr/zephyr.elf
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_default"

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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_pwm_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
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
