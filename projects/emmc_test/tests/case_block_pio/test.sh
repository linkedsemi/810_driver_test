#!/usr/bin/env bash
# eMMC 块层驱动测试 — 单核 case_block_pio（SDMA 关闭，走 ISR 轮询搬运）
#
# What is tested:
#   与 case_block_driver 完全相同的固件与用例集，差异只在 conf：
#     CONFIG_SDHCI_SDMA_ENABLE=n → linkedsemi_sdhci_transfer_blocking() 里
#     host->use_dma = false → 数据通路走 ISR 轮询搬 FIFO
#     （sdhci-of-linkedsemi.c:142-163 的 polling else 分支）。
#   用于覆盖"传输方式（DMA / PIO）× 缓冲对齐"象限。
#
# CPU1 behavior（单核工程，只有 cpu1 侧固件）：
#   上电 → sdhci1 驱动 init → ztest 三个套件（同 case_block_driver）。
#
# Normal behavior:
#   全部用例 PASS + `PROJECT EXECUTION SUCCESSFUL`。
#   ⚠️ 守卫生效（APP_CPU_SRST > 0）时除 guard 用例外全部 SKIP，属设计行为。
#
# Abnormal behavior:
#   zassert 失败 → `Assertion failed at ...` + `FAIL - <name>` + `PROJECT EXECUTION FAILED`。
#
# Feedback:
#   串口 ztest 原生输出；串口不可用时
#   `python scripts/dump_results.py --case case_block_pio --json` 读内存兜底。
#
# Hardware IO connections:
#   零外部接线（板载 eMMC，sdhci1@40049000，8-bit，盘名 "SD2"）。
#
# ⚠️ 破坏性：同 case_block_driver，会在块偏移 8 MiB 处写 scratch 数据。
#
# Build artifacts:
#   tests/case_block_pio/build/target/zephyr/zephyr.elf
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_block_pio"

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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_emmc_block_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
# conf 增量：build_single_test 检测到 <src>/prj.conf 存在时会拼成
# -DCONF_FILE=<src>/prj.conf;<TARGET_CONF>
TARGET_CONF="$(realpath "${I3C_TEST_ROOT}/tests/conf/case_block_pio.conf")"
TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TEST_BUILD_DIR}"
echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TEST_BUILD_DIR}"
print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
else
# Agent mode: build if ELF missing, then load+run via GDB
if [ ! -f "${TARGET_ELF}" ]; then
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${TARGET_CONF}" "${TEST_BUILD_DIR}"
fi
load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi
