#!/usr/bin/env bash
# Ethernet / MDIO test - A 组（零对端）
#
# What is tested:
#   LinkedSemi 以太网子系统（A 组：零对端，不依赖网线状态）：
#     - zephyr/drivers/mdio/mdio_dwmac.c（mdio1@0x40064000，snps,dwmac-mdio）
#         mdio_phy_id       读板载 RTL8211F PHY 的 PHYID1R/PHYID2R（C22 读通路）
#         mdio_reg_rw       ANAR 写-回读-恢复（C22 写通路 + 读通路可重复性）
#         mdio_invalid_args phy addr / reg 越界按 5 位字段静默回绕（驱动无范围校验）
#     - zephyr/drivers/ethernet/eth_dwmac_linkedsemi.c + eth_dwmac.c（eth1）
#         eth_dev_ready     设备就绪 / net_if 绑定 / L2 / MTU / MAC 地址契约
#     - zephyr/drivers/ethernet/phy/phy_realtek_rtl8211f.c
#         eth_phy_link_state PHYSR(0x1A) 解链路状态（**弱判据**：不断言 link 必须 up，
#                            无对端也能确定判定 ⇒ 归 A 组；原在 case_link 内）
#
# CPU0/CPU1 behavior:
#   单核工程，只有 cpu1 侧固件；构建走 1os/lsqsh/cpu1（SINGLE_BOARD，
#   入口 0x10000000）。网络栈（CONFIG_NETWORKING）必须打开 —— mdio_dwmac 的
#   init 挂在 CONFIG_NETWORKING_AUTO_INIT 下，否则 mdio1 不做时钟/pinctrl/
#   复位初始化，任何 MDIO 事务都会超时。
#
# Normal behavior:
#   全部 ztest 项 PASS - <name>，套件末尾 PROJECT EXECUTION SUCCESSFUL。
#
# Abnormal behavior:
#   任一断言失败 -> FAIL - <name>（reason = Assertion failed at ...）。
#   本组不写 PHY 配置（ANAR 写完立即恢复原值），不影响链路状态。
#
# Feedback:
#   串口 ztest 原生输出（ZT_BOOT / Running TESTSUITE / START-PASS / PROJECT
#   EXECUTION）+ INFO 约定行（oui / anar_* / mac / mtu）；
#   串口不可用时 dump_results.py 读内存兜底。
#
# Hardware IO connections:
#   无外部接线 —— 只与板载 RTL8211F PHY 通话，网线插拔不影响本 case 判定。
#
# Build artifacts:
#   case_mdio/build/target/zephyr/zephyr.elf
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_mdio"

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

# 默认源集 = src/eth_mdio_test.c（cpu1_eth_test/CMakeLists.txt 的 else 分支）。
# board 走 SINGLE_BOARD（1os/lsqsh/cpu1），不硬编码板级（SPEC_REFERENCE §6.1.10）。
build_mdio() {
    echo "[${TEST_NAME}] Build $(basename "${TARGET_SRC}") -> ${TEST_BUILD_DIR}"
    west build -p auto \
        -d "${TEST_BUILD_DIR}" \
        -b "${SINGLE_BOARD}" \
        "${TARGET_SRC}" \
        -- -DDTC_OVERLAY_FILE="${TARGET_OVERLAY}"
}

if [ "${AGENT_RUN}" != "y" ]; then
    build_mdio
    echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TEST_BUILD_DIR}"
    print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
else
    if [ ! -f "${TARGET_ELF}" ]; then
        build_mdio
    fi
    load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi
