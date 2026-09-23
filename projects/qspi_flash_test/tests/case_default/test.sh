#!/usr/bin/env bash
# QSPI flash（qspi2 + 板载 32 MiB Puya NOR）功能测试 — 单核 case_default
#
# What is tested:
#   LinkedSemi QSPI 控制器驱动（zephyr/drivers/spi/qspi_ls.c）+ jedec,spi-nor
#   上层（zephyr/drivers/flash/spi_nor_multi_dev.c）：
#     - JEDEC ID 断言（85 23 19）、SFDP 头、器件容量 32 MiB、flash 参数
#     - flash API 擦 / 写 / 读（1 / 255 / 256 / 4096 / 8192 B）+ 参数校验
#     - STIG 单次数据相 4096 B 上限（raw spi_nor_ops 路径，4096 / 4097）
#     - wire mode 分支（1-1-1 / 1-1-4 / 1-4-4 / 未实现的 2-2-2）
#     - qspi_ls_transceive 恒 -ENOTSUP（标准 SPI API 不可用）
#     - 时序校准状态 + 总线频率边界（200 MHz 上界 / 6.25 MHz 下界 / freq==0 静默）
#
# CPU1 behavior（单核工程，只有 cpu1 侧固件）：
#   上电 → qspi2/flash2 驱动 init（SFDP 读取 + 时序校准）→ ztest 依次跑
#   qspi_flash_calib / qspi_flash_id / qspi_flash_rw / qspi_flash_stg 四个套件。
#
# Normal behavior:
#   全部用例 PASS，串口打印 ztest 原生 `PASS - <name>` 逐项 + `PROJECT EXECUTION SUCCESSFUL`。
#
# Abnormal behavior:
#   某项 zassert 失败 → 打印 `Assertion failed at <file>:<line> (<func>): <msg>`
#   + `FAIL - <name>`，套件以 `PROJECT EXECUTION FAILED` 收尾。
#
# Feedback:
#   串口 ztest 原生输出（harness parse_ztest 解析）；串口不可用时
#   `python scripts/dump_results.py --case case_default --json` 走 GDB 读内存兜底。
#
# Hardware IO connections:
#   零外部接线（板载 qspi2 NOR；pinmux 板载默认 PC05=CSN / PC06-09=DAT0-3 / PC10=SCK，FUNC4）。
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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_qspi_flash_test"
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
