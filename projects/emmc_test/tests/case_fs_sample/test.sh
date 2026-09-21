#!/usr/bin/env bash
# eMMC 文件系统场景补充（件②）—— 复用上游 sample，单核 case_fs_sample
#
# What is tested:
#   上游 zephyr/samples/subsys/fs/fs_sample（控制台 sample，**非 ztest**）：
#   挂载 "/SD2:" → 列目录 → （可选）创建目录/文件 → 读回校验 → 卸载。
#   用于覆盖 ztest 用例之外的"真实使用姿势"（挂载点字符串、目录遍历、
#   文件追加语义）。
#
#   ⚠️ 只读上游：不修改 zephyr/samples/ 下任何文件，只做
#      `west build -d <本地 build 目录>`。
#
# CPU1 behavior（单核工程，只有 cpu1 侧固件）：
#   上电 → main() 直接挂载 "/SD2:" 并跑场景；无 ztest 输出。
#
# Normal behavior:
#   控制台依次打印 mount / lsdir / create / read 的 LOG_INF 行，最后正常结束。
#
# Abnormal behavior:
#   挂载或读写失败时打印 `[error] ...` 行（sample 自身的 LOG_ERR）。
#
# Feedback:
#   ⚠️ 本固件是**控制台 sample，不是 ztest**：
#     · 无 `Running TESTSUITE` / `PASS - <name>` / `PROJECT EXECUTION SUCCESSFUL`；
#     · 无 ZT_BOOT、不链接 810 公共结果模块；
#     · 现有 harness（dbg_run.py / run_test.py 的 parse_ztest 判定）**无法自动判 PASS/FAIL**，
#       需人工看串口或新增 sample 模式判据。详见 tests/COVERAGE.md「框架层障碍」。
#
# Hardware IO connections:
#   零外部接线（板载 eMMC，sdhci1@40049000，8-bit，盘名 "SD2"）。
#
# ⚠️ 破坏性：sample 会在盘上创建/删除文件（用户裁定允许格式化整片）。
#
# Build artifacts:
#   tests/case_fs_sample/build/target/zephyr/zephyr.elf
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB (人工看串口)
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_fs_sample"

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
UPSTREAM_SRC="$(realpath "${WORKSPACE_ROOT}/../zephyr/samples/subsys/fs/fs_sample")"
if [ ! -f "${UPSTREAM_SRC}/CMakeLists.txt" ]; then
    echo "ERROR: upstream source not found: ${UPSTREAM_SRC}" >&2
    exit 1
fi

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

do_build() {
    echo "[${TEST_NAME}] Build upstream fs_sample -> ${TEST_BUILD_DIR}"
    west build -p always \
        -d "${TEST_BUILD_DIR}" \
        -b "${SINGLE_BOARD}" \
        "${UPSTREAM_SRC}"
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
