#!/usr/bin/env bash
# eMMC 块层驱动测试 — 单核 case_block_driver（DMA/SDMA 使能）
#
# What is tested:
#   LinkedSemi SDHCI 控制器驱动块层（zephyr/drivers/sdhc/sdhci-of-linkedsemi.c）：
#     - is_app_cpu_running() 早退守卫与实际寄存器态（APP_CPU_SRST）的一致性
#     - get_host_props 能力字 vs DT（f_max/f_min/hs200/hs400/bus8/is_spi）
#     - 块读写通路（disk_access → mmc_read/write_blocks → sdhc_request）：
#       单块 / 4 块、4 字节对齐 vs 非对齐缓冲、异常盘名
#     - 缺陷靶点：data-opcode 白名单 default 分支的 mutex 泄漏（:475-478 vs :422）
#       —— 非挂死判定（有限超时 join 的探针线程）
#     - 缺陷靶点：set_io 时钟越界静默忽略（:228-232）
#   本 case 使能 SDMA（CONFIG_SDHCI_SDMA_ENABLE 默认 y）→ 走 DMA 数据通路。
#
# CPU1 behavior（单核工程，只有 cpu1 侧固件）：
#   上电 → sdhci1 驱动 init → ztest 依次跑
#   emmc_app_cpu_guard / emmc_block_data / zz_emmc_block_edge。
#
# Normal behavior:
#   全部用例 PASS + `PROJECT EXECUTION SUCCESSFUL`。
#   ⚠️ 若 `APP_CPU_SRST > 0`（守卫生效，见测试文件头说明），除
#   `guard_matches_driver_behavior` 外的用例会打印 SKIP 而非 FAIL —— 这是设计行为。
#
# Abnormal behavior:
#   zassert 失败 → `Assertion failed at ...` + `FAIL - <name>` + `PROJECT EXECUTION FAILED`。
#
# Feedback:
#   串口 ztest 原生输出；串口不可用时
#   `python scripts/dump_results.py --case case_block_driver --json` 读内存兜底。
#
# Hardware IO connections:
#   零外部接线（板载 eMMC，sdhci1@40049000，8-bit，盘名 "SD2"）。
#
# ⚠️ 破坏性：本 case 会在块偏移 8 MiB 处写 scratch 数据（用户裁定允许格式化整片）。
#
# Build artifacts:
#   tests/case_block_driver/build/target/zephyr/zephyr.elf
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_block_driver"

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
