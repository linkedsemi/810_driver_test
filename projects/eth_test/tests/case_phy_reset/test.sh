#!/usr/bin/env bash
# Ethernet PHY reset-pulse path test - A 组（零对端，同 case_mdio 源集，仅 overlay 不同）
#
# What is tested:
#   把 mdio_dwmac_ethphy_reset() 的**脉冲子块**打开后，系统仍能正常启动且 PHY 可用：
#     - zephyr/drivers/mdio/mdio_dwmac.c:231-244（复位脚拉低 + k_busy_wait(reset_hold_ms)）
#     - zephyr/drivers/mdio/mdio_dwmac.c:252-259（拉高后 k_busy_wait(circuits_set_ms)）
#   默认板级下这两个子块**不发脉冲**（门 `reset_hold_ms && circuits_set_ms` 两属性缺省 0）；
#   本 case 的 target.overlay 给 &mdio1 补 reset-hold-ms=10 / circuits-set-ms=30
#   （取值来源见该 overlay 注释：驱动 :242 / :257 自述的 10 ms 与 30 ms），
#   使该路径**在本板上电时真正执行**。
#   本 case 复用 case_mdio 的源集（套件 eth_mdio，5 项），断言不变 ——
#   判据是「启用该分支后系统仍能起来、MDIO/PHY 仍工作」，属**路径冒烟**。
#
# CPU0/CPU1 behavior:
#   单核工程，只有 cpu1 侧固件；构建走 1os/lsqsh/cpu1（SINGLE_BOARD，
#   入口 0x10000000）。mdio1 init（POST_KERNEL 优先级 60）先跑复位脉冲，
#   PHY 设备 init（优先级 70）随后用自己的 reset-gpios 再复位一次（同一 GPIOG pin1）。
#
# Normal behavior:
#   全部 ztest 项 PASS - <name>，套件末尾 PROJECT EXECUTION SUCCESSFUL；
#   INFO 行含 mdio_reset_hold_ms / mdio_circuits_set_ms，可据此确认本 case 的 overlay 生效
#   （case_mdio 同字段为 0/0）。
#
# Abnormal behavior:
#   开启脉冲后若启动异常 / PHY 不可用 -> FAIL。
#   注意：**脉冲的电平瞬态本身无法在固件内观测**（需示波器/逻辑分析仪量 GPIOG pin1），
#   本 case 只能证明"该路径执行后系统与 PHY 仍正常"。
#
# Feedback:
#   串口 ztest 原生输出（ZT_BOOT / Running TESTSUITE / START-PASS / PROJECT
#   EXECUTION）+ INFO 约定行；串口不可用时 dump_results.py 读内存兜底。
#
# Hardware IO connections:
#   无外部接线 —— 只与板载 RTL8211F PHY 通话（复位脚 GPIOG pin1 为板内网络）。
#
# Build artifacts:
#   case_phy_reset/build/target/zephyr/zephyr.elf
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_phy_reset"

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

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_eth_test"
# 本 case 用**自己的 overlay**（#include 既有 app.overlay + 给 &mdio1 补脉冲属性）
TARGET_OVERLAY="$(realpath "${SCRIPT_DIR}/target.overlay")"
TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

# 源集 = 默认分支（src/eth_mdio_test.c），与 case_mdio 相同；差异只在 overlay（§6.1.8）。
# board 走 SINGLE_BOARD（1os/lsqsh/cpu1），不硬编码板级（SPEC_REFERENCE §6.1.10）。
build_phy_reset() {
    echo "[${TEST_NAME}] Build $(basename "${TARGET_SRC}") -> ${TEST_BUILD_DIR}"
    west build -p auto \
        -d "${TEST_BUILD_DIR}" \
        -b "${SINGLE_BOARD}" \
        "${TARGET_SRC}" \
        -- -DDTC_OVERLAY_FILE="${TARGET_OVERLAY}"
}

if [ "${AGENT_RUN}" != "y" ]; then
    build_phy_reset
    echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TEST_BUILD_DIR}"
    print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
else
    if [ ! -f "${TARGET_ELF}" ]; then
        build_phy_reset
    fi
    load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi
