#!/usr/bin/env bash
# Ethernet link test - B 组（需网线对端）
#
# What is tested:
#   zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c（板载 RTL8211F PHY）+ eth_dwmac
#     - eth_link_up   10000 ms 轮询窗口内 PHY 报 link up（强判据，需对端已激活）
#
#   注：同族弱判据项 eth_phy_link_state（只读 PHY 状态、不断言 up、无需对端）
#   已按父会话裁定挪入 A 组 case_mdio（套件 eth_mdio），本 case 只留 eth_link_up。
#
# CPU0/CPU1 behavior:
#   单核工程，只有 cpu1 侧固件；构建走 1os/lsqsh/cpu1（SINGLE_BOARD，
#   入口 0x10000000）。PHY 设备在 POST_KERNEL 由 phy_rt_rtl8211f_init 复位并
#   配置 RGMII Tx/Rx delay；本 case 只读链路状态，不改 PHY 配置。
#
# Normal behavior:
#   全部 ztest 项 PASS - <name>，套件末尾 PROJECT EXECUTION SUCCESSFUL。
#
# Abnormal behavior:
#   eth_link_up 在 10000 ms 窗口内始终 link down -> FAIL（reason = Assertion
#   failed at ...）。这是真实链路问题（对端网口未上电 / 网线未插好 /
#   自协商失败），不是时序抖动。
#
# Feedback:
#   串口 ztest 原生输出 + INFO 约定行（link / speed_mbps / duplex）；
#   串口不可用时 dump_results.py 读内存兜底。
#
# Hardware IO connections:
#   外部: 板载 RJ45 <-> PC 网口（网线）。对端网口须上电并开启自协商；
#   eth_link_up 的判定依赖对端状态（SPEC_REFERENCE §6.6）。
#
# Build artifacts:
#   case_link/build/target/zephyr/zephyr.elf
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_link"

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
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

# -DETH_LINK_TEST=y 在 cpu1_eth_test/CMakeLists.txt 里选择 src/eth_link_test.c。
# board 走 SINGLE_BOARD（1os/lsqsh/cpu1），不硬编码板级（SPEC_REFERENCE §6.1.10）。
build_link() {
    echo "[${TEST_NAME}] Build $(basename "${TARGET_SRC}") -> ${TEST_BUILD_DIR}"
    west build -p auto \
        -d "${TEST_BUILD_DIR}" \
        -b "${SINGLE_BOARD}" \
        "${TARGET_SRC}" \
        -- -DDTC_OVERLAY_FILE="${TARGET_OVERLAY}" -DETH_LINK_TEST=y
}

if [ "${AGENT_RUN}" != "y" ]; then
    build_link
    echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TEST_BUILD_DIR}"
    print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
else
    if [ ! -f "${TARGET_ELF}" ]; then
        build_link
    fi
    load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi
