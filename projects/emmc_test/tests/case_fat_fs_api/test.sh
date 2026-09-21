#!/usr/bin/env bash
# eMMC FatFS 主回归（件①）—— 复用上游 ztest，单核 case_fat_fs_api
#
# What is tested:
#   上游 zephyr/tests/subsys/fs/fat_fs_api（12 个测试文件）在**板载 eMMC** 上的
#   完整 FAT 文件系统通路：mount/unmount/挂载标志、mkfs（fs_mkfs）、
#   文件读写/追加/截断、目录操作、rename、只读挂载、open flags。
#   覆盖层次 = **文件系统层**（块层驱动缺陷见 case_block_driver）。
#
#   ⚠️ 只读上游：本 case 不修改 zephyr/tests/ 下任何文件，只在 810 侧用
#      `west build -d <本地 build 目录>` 构建上游源码 + 810 侧增量 conf。
#
# CPU1 behavior（单核工程，只有 cpu1 侧固件）：
#   上电 → ztest 自动跑 fat_fs_basic / fat_fs_mkfs 两个套件；盘名 "SD2"。
#
# Normal behavior:
#   全部用例 PASS + `PROJECT EXECUTION SUCCESSFUL`。
#
# Abnormal behavior:
#   zassert 失败 → `Assertion failed at ...` + `FAIL - <name>` + `PROJECT EXECUTION FAILED`。
#
# Feedback:
#   串口 ztest 原生输出。
#   ⚠️ 本固件**不链接 810 公共结果模块**（test_results.c / ztest_glue.c），
#      故 `scripts/dump_results.py` 的内存兜底通道**不适用**；也不打印 ZT_BOOT。
#      详见 tests/COVERAGE.md「框架层障碍」一节。
#
# Hardware IO connections:
#   零外部接线（板载 eMMC，sdhci1@40049000，8-bit，盘名 "SD2"）。
#
# ⚠️ 破坏性：本 case 会 wipe + mkfs 整片 eMMC（用户裁定允许格式化整片）。
#
# Build artifacts:
#   tests/case_fat_fs_api/build/target/zephyr/zephyr.elf
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_fat_fs_api"

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

# 上游源码（只读；WORKSPACE_ROOT = 810_driver_test，其父目录 = west topdir）
UPSTREAM_SRC="$(realpath "${WORKSPACE_ROOT}/../zephyr/tests/subsys/fs/fat_fs_api")"
if [ ! -f "${UPSTREAM_SRC}/CMakeLists.txt" ]; then
    echo "ERROR: upstream source not found: ${UPSTREAM_SRC}" >&2
    exit 1
fi

# CONF_FILE 必须显式给成 "<上游 prj_mmc.conf>;<810 侧增量 conf>"：
# 不可走 build_single_test 的 "APP prj.conf 在前" 拼接——上游 prj.conf 里的
# CONFIG_DISK_DRIVER_FLASH=y 会让 test_fat.h 选中 FLASH 分型（DISK_NAME 取
# DT test_disk 节点），本板无该节点 → 直接编译失败。上游 testcase.yaml 的
# mmc 用例本来就是只用 prj_mmc.conf（整体替换 prj.conf）。
UPSTREAM_CONF="$(realpath "${UPSTREAM_SRC}/prj_mmc.conf")"
EXTRA_CONF="$(realpath "${I3C_TEST_ROOT}/tests/conf/case_fat_fs_api.conf")"
TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

do_build() {
    echo "[${TEST_NAME}] Build upstream fat_fs_api (prj_mmc.conf + 810 增量 conf) -> ${TEST_BUILD_DIR}"
    west build -p always \
        -d "${TEST_BUILD_DIR}" \
        -b "${SINGLE_BOARD}" \
        "${UPSTREAM_SRC}" \
        -DCONF_FILE="$(win_path "${UPSTREAM_CONF}");$(win_path "${EXTRA_CONF}")"
}

if [ "${AGENT_RUN}" != "y" ]; then
do_build
echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TEST_BUILD_DIR}"
print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
else
if [ ! -f "${TARGET_ELF}" ]; then
do_build
fi
load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi
