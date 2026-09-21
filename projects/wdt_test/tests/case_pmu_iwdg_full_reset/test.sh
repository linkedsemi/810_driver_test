#!/usr/bin/env bash
# WDT pmu_iwdg full-reset test - 触发 pmu_iwdg 整芯片复位
#
# What is tested:
#   LinkedSemi WDT driver（zephyr/drivers/watchdog/wdt_iwdg_ls.c）的 SOC 复位分支：
#   pmu_iwdg + WDT_FLAG_RESET_SOC -> 整芯片复位，复位原因 = SYS_IWDT_FULL_RESET。
#   与 case_destructive_reset（sec_iwdg -> SEC_IWDT_HART_RESET）互补，覆盖另一条
#   复位路径与 soc_reset_reason.c 的 SEC_PMU 分支。
#
# CPU0/CPU1 behavior:
#   单核工程（只有 cpu1 侧固件），但本 case 用 2os 板级构建并经 flash 固化启动
#   （bootrom 引导，地址 0x08000000），复位后由 bootrom 重新运行同一镜像。
#   本 case 用 -DWDT_PMU_RESET_TEST 选择 src/wdt_pmu_reset_test.c 源集。
#
# Normal behavior:
#   Phase 0 安装 2s 超时后不喂狗 -> 整芯片复位 -> Phase 1 读到
#   reset_reason == SYS_IWDT_FULL_RESET -> PROJECT EXECUTION: SUCCESSFUL。
#
# Abnormal behavior:
#   未复位（Phase 0 后无输出）或复位原因不符 -> PROJECT EXECUTION: FAILED。
#
# Feedback:
#   串口 printk 直出：PHASE 0/1/2 标记 + PASS/FAIL + PROJECT EXECUTION 行。
#   跨复位状态在 noinit 段（pmu_test_phase / pmu_phase0_passed）。
#
# Hardware IO connections:
#   无（片内 pmu_iwdg@0x4002f800）。
#   ⚠ 本 case 触发整芯片复位，会连带复位另一核与全部外设 ——
#     必须单独运行，不进 regression/cases.yaml 批量回归
#     （与 case_destructive_reset 同一处置，见 tests/COVERAGE.md）。
#
# Build artifacts:
#   <case>/build/target/zephyr/zephyr_flash_bram_xip_lsqsh_evb_2os_lsqsh_cpu1.bin
#
# Usage:
#   ./test.sh          # build firmware
#   ./test.sh -f       # build + flash to board（需先杀 DebugServer，独占 CKLink）
#
# 本 case 走 flash 固化启动 + 跨复位状态机，不是 ztest RAM 加载流程，
# 故不提供三模式里的 -g / -r（GDB RAM load 无意义）；与 case_destructive_reset 同。
# 端到端自动跑法见同目录 run_pmu_reset.sh。

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_pmu_iwdg_full_reset"

FLASH_MODE=""
while [ $# -gt 0 ]; do
    case "$1" in
        -f) FLASH_MODE="y"; shift ;;
        *)  echo "Unknown option: $1"; exit 1 ;;
    esac
done

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_wdt_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_BUILD="${TEST_BUILD_DIR}/target"
TARGET_FLASH_BIN="${TARGET_BUILD}/zephyr/zephyr_flash_bram_xip_lsqsh_evb_2os_lsqsh_cpu1.bin"

echo "[${TEST_NAME}] Build $(basename "${TARGET_SRC}") -> ${TARGET_BUILD}"
# board 走 TARGET_BOARD（2os/lsqsh/cpu1），不硬编码板级（SPEC_REFERENCE §6.1.10）；
# -p always 全量重建（改 overlay/源集后必须重建，SPEC_REFERENCE §4.2）。
west build -p always \
    -d "${TARGET_BUILD}" \
    -b "${TARGET_BOARD}" \
    "${TARGET_SRC}" \
    -- -DDTC_OVERLAY_FILE="${TARGET_OVERLAY}" -DWDT_PMU_RESET_TEST=y

echo "Build OK: ${TARGET_FLASH_BIN}"
echo ""
echo "烧录到 flash（bootrom 会自动引导启动）:"
echo "  python ${TEST_ROOT}/../scripts/flash_program.py \\"
echo "    --bin ${TARGET_FLASH_BIN}@${CPU1_XIP_ADDR} --verify"
echo ""

if [ "${FLASH_MODE}" = "y" ]; then
    taskkill /F /IM DebugServerConsole.exe 2>/dev/null || true
    sleep 2
    python "${TEST_ROOT}/../scripts/flash_program.py" \
        --bin "${TARGET_FLASH_BIN}@${CPU1_XIP_ADDR}" --verify
    echo "烧录完成；复位板卡或运行 run_pmu_reset.sh 采集 Phase 0/1 输出。"
fi
