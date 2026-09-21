#!/usr/bin/env bash
# WDT boundary / error-path test
#
# What is tested:
#   LinkedSemi WDT driver（zephyr/drivers/watchdog/wdt_iwdg_ls.c）的非破坏性
#   边界与错误分支，实例 = sec_iwdg（HART 复位域）+ pmu_iwdg（SOC 复位域）：
#     - install_timeout: window.min != 0 拒 / window.max == 0 拒
#     - install_timeout: HAL_IWDG_IS_MATCH 按实例匹配 flags（RESET_CPU_CORE / RESET_SOC）
#     - install_timeout: callback 注册（!=NULL）与清除（==NULL）分支
#     - setup: IWDT_LOAD==0 -> -ENOTSUP / IWDT_CTRL!=0 -> -EBUSY /
#              WDT_OPT_PAUSE_IN_SLEEP -> -ENOTSUP / 正常使能路径
#     - disable / feed 正常返回
#     - timeout_ms -> IWDT_LOAD ticks 换算，含 uint32 饱和边界
#
# CPU0/CPU1 behavior:
#   单核工程，只有 cpu1 侧固件；构建走 1os/lsqsh/cpu1（入口 0x10000000）。
#   本 case 用 -DWDT_BOUNDARY_TEST 选择 src/wdt_boundary_test.c 源集。
#
# Normal behavior:
#   全部 ztest 项 PASS - <name>，套件末尾 PROJECT EXECUTION SUCCESSFUL。
#   setup 成功的用例在断言后立即 disable，看门狗不会跑到超时。
#
# Abnormal behavior:
#   任一断言失败 -> FAIL - <name>（reason = Assertion failed at ...）。
#   本 case 不触发任何复位（破坏性路径见 case_destructive_reset /
#   case_pmu_iwdg_full_reset）。
#
# Feedback:
#   串口 ztest 原生输出（ZT_BOOT / Running TESTSUITE / START-PASS / PROJECT
#   EXECUTION）+ INFO 约定行（ticks=<n>）；串口不可用时 dump_results.py 读内存兜底。
#
# Hardware IO connections:
#   无（片内看门狗：sec_iwdg@0x400a1800、pmu_iwdg@0x4002f800，均无外部引脚）
#
# Build artifacts:
#   <case>/build/target/zephyr/zephyr.elf
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_boundary"

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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_wdt_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

# -DWDT_BOUNDARY_TEST 在 cpu1_wdt_test/CMakeLists.txt 里选择 wdt_boundary_test.c。
# board 走 SINGLE_BOARD（1os/lsqsh/cpu1），不硬编码板级（SPEC_REFERENCE §6.1.10）。
build_boundary() {
    echo "[${TEST_NAME}] Build $(basename "${TARGET_SRC}") -> ${TEST_BUILD_DIR}"
    west build -p always \
        -d "${TEST_BUILD_DIR}" \
        -b "${SINGLE_BOARD}" \
        "${TARGET_SRC}" \
        -- -DDTC_OVERLAY_FILE="${TARGET_OVERLAY}" -DWDT_BOUNDARY_TEST=y
}

if [ "${AGENT_RUN}" != "y" ]; then
    build_boundary
    echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TEST_BUILD_DIR}"
    print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
else
    # Agent mode: build if ELF missing, then load+run via GDB
    if [ ! -f "${TARGET_ELF}" ]; then
        build_boundary
    fi
    load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi
