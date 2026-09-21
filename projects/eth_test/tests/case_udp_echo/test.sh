#!/usr/bin/env bash
# Ethernet data-plane test - B 组：UDP 报文收发 + 载荷校验
#
# What is tested:
#   静态 IPv4 配置 -> UDP socket 发固定载荷（magic+序号+滚动字节）到 PC 的
#   回声端口 -> 收回包并**逐字节比对**（长度 + 内容）-> 统计丢包/RTT。
#   覆盖 zephyr/drivers/ethernet/eth_dwmac.c 的真实收发路径（dwmac_send /
#   dwmac_receive / RX refill 线程 / 描述符环）与 zsock UDP 数据面。
#
# CPU0/CPU1 behavior:
#   单核工程，只有 cpu1 侧固件；构建走 1os/lsqsh/cpu1（SINGLE_BOARD，
#   入口 0x10000000）。IP/对端地址与端口取 prj.conf 与源码内的约定值。
#
# Normal behavior:
#   全部 ztest 项 PASS - <name>，套件末尾 PROJECT EXECUTION SUCCESSFUL；
#   INFO 行上报 sent/send_fail/rx_total/rx_bad/verified/rtt_us 等统计值。
#
# Abnormal behavior:
#   5 次尝试（每次 2000 ms）内无相符回包 -> FAIL（reason = Assertion failed at ...）。
#   UDP 无连接，首包 sendto 因 ARP 未解析返回 EHOSTUNREACH 属正常，只计数不硬判；
#   失败即真实对端问题（回声程序未运行 / 端口不一致 / 不同网段），不是时序抖动。
#
# Feedback:
#   串口 ztest 原生输出 + INFO 约定行；串口不可用时 dump_results.py 读内存兜底。
#
# Hardware IO connections:
#   外部: 板载 RJ45 <-> PC 网口（网线）。PC 侧须：网口上电激活、同网段静态
#   IPv4（约定 192.168.1.1/24）、**运行 UDP 回声程序**（约定端口 5001）
#   （SPEC_REFERENCE §6.6）。
#
# Build artifacts:
#   case_udp_echo/build/target/zephyr/zephyr.elf
#
# Usage:
#   ./test.sh          # build only
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_udp_echo"

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

# -DETH_UDP_ECHO_TEST=y 在 cpu1_eth_test/CMakeLists.txt 里选择 src/eth_udp_echo_test.c。
# board 走 SINGLE_BOARD（1os/lsqsh/cpu1），不硬编码板级（SPEC_REFERENCE §6.1.10）。
build_udp_echo() {
    echo "[${TEST_NAME}] Build $(basename "${TARGET_SRC}") -> ${TEST_BUILD_DIR}"
    west build -p auto \
        -d "${TEST_BUILD_DIR}" \
        -b "${SINGLE_BOARD}" \
        "${TARGET_SRC}" \
        -- -DDTC_OVERLAY_FILE="${TARGET_OVERLAY}" -DETH_UDP_ECHO_TEST=y
}

if [ "${AGENT_RUN}" != "y" ]; then
    build_udp_echo
    echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET} ${TEST_BUILD_DIR}"
    print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
else
    if [ ! -f "${TARGET_ELF}" ]; then
        build_udp_echo
    fi
    load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi
