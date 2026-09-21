#!/usr/bin/env bash
# Ethernet data-plane test - B 组：ARP 解析 + ICMP echo 收发校验
#
# What is tested:
#   静态 IPv4 配置（net_config）-> ICMPv4 Echo-Request 发往 PC ->
#   收 Echo-Reply 并校验回包源地址 == 对端地址（即 ARP 解析到的就是 PC）。
#   覆盖 zephyr/subsys/net/{ip/icmp.c, l2/ethernet/arp.c, lib/config/init.c}
#   与 zephyr/drivers/ethernet/eth_dwmac.c 的真实收发路径。
#
# CPU0/CPU1 behavior:
#   单核工程，只有 cpu1 侧固件；构建走 1os/lsqsh/cpu1（SINGLE_BOARD，
#   入口 0x10000000）。IP/对端地址取 prj.conf 的 CONFIG_NET_CONFIG_*（约定值）。
#
# Normal behavior:
#   全部 ztest 项 PASS - <name>，套件末尾 PROJECT EXECUTION SUCCESSFUL；
#   INFO 行上报 sent/recv/loss_pct/rtt_us 等统计值。
#
# Abnormal behavior:
#   5 次尝试（每次 2000 ms）内无回包 -> FAIL（reason = Assertion failed at ...）。
#   这是真实对端/链路问题（PC 网口未激活 / 不同网段 / 防火墙拦 ICMP），
#   不是时序抖动 —— 判据本身带重试窗口。
#
# Feedback:
#   串口 ztest 原生输出 + INFO 约定行；串口不可用时 dump_results.py 读内存兜底。
#
# Hardware IO connections:
#   外部: 板载 RJ45 <-> PC 网口（网线）。PC 侧须：网口上电激活、同网段静态
#   IPv4（约定 192.168.1.1/24）、放行 ICMPv4 回显请求（SPEC_REFERENCE §6.6）。
#
# Build artifacts:
#   case_arp_icmp/build/target/zephyr/zephyr.elf
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_arp_icmp"

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

# -DETH_ARP_ICMP_TEST=y 在 cpu1_eth_test/CMakeLists.txt 里选择 src/eth_arp_icmp_test.c。
# board 走 SINGLE_BOARD（1os/lsqsh/cpu1），不硬编码板级（SPEC_REFERENCE §6.1.10）。
build_arp_icmp() {
    echo "[${TEST_NAME}] Build $(basename "${TARGET_SRC}") -> ${TEST_BUILD_DIR}"
    west build -p auto \
        -d "${TEST_BUILD_DIR}" \
        -b "${SINGLE_BOARD}" \
        "${TARGET_SRC}" \
        -- -DDTC_OVERLAY_FILE="${TARGET_OVERLAY}" -DETH_ARP_ICMP_TEST=y
}

if [ "${AGENT_RUN}" != "y" ]; then
    build_arp_icmp
    echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TEST_BUILD_DIR}"
    print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
else
    if [ ! -f "${TARGET_ELF}" ]; then
        build_arp_icmp
    fi
    load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi
