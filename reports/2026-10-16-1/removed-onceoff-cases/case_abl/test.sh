#!/usr/bin/env bash
# case_abl —— 日志配置消融（一次性诊断，非回归用例）
#
# 目的：在 810 上对「客户 .config 中上次照抄漏掉的项」逐项补做消融 ——
#   基线 = 上次的"客户配置"（SHELL=y + SHELL_LOG_BACKEND=y + SLEEP_MS=1000 +
#   QUEUE=512 + BUF=8192 + LOG_PRINTK=y + LOG_BACKEND_UART=n），实测 ~12 ms/行。
#   每次只加一组项，量 **SCAN_BURST_BEGIN → SCAN_BURST_END 的到达时间差**（排空跨度）。
#
# 固件动作与 case_scan_burst **逐字节相同**（filter 清空 + 上游逐址扫 0x08..0x77），
# 差异只在 conf（B5 另需 shell 触发，见 abl_b5.conf）。
#
# 用法：
#   ./test.sh                    # 默认 abl_00_baseline.conf
#   ./test.sh abl_b1.conf        # 指定 conf
#   ABL_CONF=abl_b2.conf ./test.sh
#
# 只构建（不加载/不采集）；采集走 tests/scripts/dbg_run.py --case case_abl --raw。

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_abl"
CONF_NAME="${1:-${ABL_CONF:-abl_00_baseline.conf}}"
ABL_CONF_PATH="${SCRIPT_DIR}/${CONF_NAME}"
if [ ! -f "${ABL_CONF_PATH}" ]; then
    echo "ERROR: missing conf: ${ABL_CONF_PATH}" >&2
    exit 1
fi

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_i2cf_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"

PRJ_WIN="$(win_path "$(realpath "${TARGET_SRC}/prj.conf")")"
OVERLAY_WIN="$(win_path "${TARGET_OVERLAY}")"
CONF_WIN="$(win_path "$(realpath "${ABL_CONF_PATH}")")"

# 仅 CONF_FILE/内容变化 ⇒ -p auto 重跑 cmake 并只重编受影响对象（比 -p always 快）。
# 若发现配置漂移（.config 未随 conf 更新）再回退 -p always。
echo "[case_abl] conf=${CONF_NAME} -> ${TEST_BUILD_DIR}"
west build -p auto \
    -d "${TEST_BUILD_DIR}" \
    -b "${SINGLE_BOARD}" \
    "${TARGET_SRC}" \
    -DDTC_OVERLAY_FILE="${OVERLAY_WIN}" \
    -DCONF_FILE="${PRJ_WIN};${CONF_WIN}"

echo "[case_abl] build done: ${TEST_BUILD_DIR}/zephyr/zephyr.elf"
