#!/usr/bin/env bash
# SPI master (spi_ls / linkedsemi,ls-spi) driver test (single-core cpu1 secure domain)
#
# What is tested:
#   (A) 套件 `spi`（18 项，外部: 无）
#       zephyr/drivers/spi/spi_ls.c 全 API：device_ready/DT 契约 / configure 位回读(MSTR-CPOL-CPHA-LSBFIRST-DS-SSOE-TXFTH) / 分频表七档与纯位宽 / 频率上限±1 / NULL 入参短路 / 长度扫描 1..256 / 全双工与单工通路 / 半双工与 TI 模式异常返回 / release 契约 / 同指针重配的分频漂移(固化) / HOLD_ON_CS 的 SPE 保持 / LOCK_ON 持锁释放 / 双线程并发 / 复位终态与恢复。
#   (B) 套件 `spi_flash`（11 项，外部: 无 —— 板载 spif3 master -> flash4_0）
#       spif3 master -> spi4 -> flash4_0（jedec,spi-nor / spi_nor_multi_dev）：
#       flash ready/JEDEC ID、擦除全 0xFF、对齐/非对齐读 1..4096 B、
#       对齐/非对齐写 1..256 B（擦除后写读逐字节比对）。
#
# CPU1 (target role, TGT token):
#   - Single-core project, no peer core; test_main drives all ZTESTs itself.
#
# Normal behavior:
#   All items PASS + PROJECT EXECUTION SUCCESSFUL.
#
# Abnormal behavior:
#   Any FAIL prints the zassert reason (file:line (func): msg, with expected vs
#   actual values); -ETIMEDOUT on a transceive means the driver's TXE ISR chain
#   did not advance; a load access fault (serial goes silent) means the known
#   NULL-deref defect on the configure error path fired.
#   若板载 flash4_0 不在位，spi_flash 套件会在 suite setup 失败（设备/JEDEC 契约）。
#
# Hardware IO connections:
#   (A) 套件 `spi`：无外部接线（片内）。spi1 = PA00/PA01/PA02 + CS GPIOA 3；
#       spi2 = PJ12/PJ13/PJ14 + CS GPIOJ 15（板级 pinctrl 指定）。总线上无从设备。
#   (B) 套件 `spi_flash`：**外部: 无** —— 板载 spif3 master -> spi4 -> flash4_0。
#       测试区从 1 MiB 起（0x100000/0x110000/0x120000），不碰前 1 MiB。
#
# Build artifacts:
#   - tests/case_default/build/target/
#
# Usage:
#   ./test.sh          # build only, print launch.json snippet
#   ./test.sh -g       # build + auto load/run via GDB
#   ./test.sh -r       # load/run only (agent mode, requires prior build)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "${TEST_ROOT}/../../scripts/test_common.sh"

TEST_NAME="case_default"

# Parse options: -g enables automatic GDB load/run.
AUTO_GDB=""
while [ $# -gt 0 ]; do
    case "$1" in
        -g)
            AUTO_GDB="y"
            shift
            ;;
        -r)
            AGENT_RUN="y"
            shift
            ;;
        *)
            echo -e "${COLOR_YELLOW}Unknown option: $1${COLOR_RESET}"
            echo "Usage: $0 [-g] [-r]"
            exit 1
            ;;
    esac
done

if [ "${AUTO_GDB}" != "y" ]; then
    SKIP_LOAD="y"
fi

if [ "${AGENT_RUN}" = "y" ]; then
    SKIP_LAUNCH_PRINT="y"
    RUN_TIMEOUT="0"
fi

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_spi_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
# 1. Build target image first.
#    Board 走 test_common.sh 的 SINGLE_BOARD 变量（单核 = 1os 变体）——
#    单核固件一律 1os 编译测试（SPEC_REFERENCE §6.1.10，禁在此硬编码 board）。
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TEST_BUILD_DIR}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 2. Print test summary after build so it is not buried in build logs.
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}What: (A) 套件 spi 18 项——zephyr/drivers/spi/spi_ls.c 全 API：device_ready/DT 契约 / configure 位回读(MSTR-CPOL-CPHA-LSBFIRST-DS-SSOE-TXFTH) / 分频表七档与纯位宽 / 频率上限±1 / NULL 入参短路 / 长度扫描 1..256 / 全双工与单工通路 / 半双工与 TI 模式异常返回 / release 契约 / 同指针重配的分频漂移(固化) / HOLD_ON_CS 的 SPE 保持 / LOCK_ON 持锁释放 / 双线程并发 / 复位终态与恢复。${COLOR_RESET}"
echo -e "${COLOR_YELLOW}      (B) 套件 spi_flash 11 项——spif3(master)->spi4->flash4_0 板载通路：malloc 只读冒烟、ready/JEDEC、擦除全 0xFF、静态/malloc 缓冲的对齐与非对齐读 1..4096 B、写 1..256 B（擦除后写读逐字节比对）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: all items PASS + PROJECT EXECUTION SUCCESSFUL.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: FAIL - <name> + zassert reason (file:line, expected vs actual).${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM24, CH342）；banner 'CPU1 SPI_MASTER' -> TGT.${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: (A) 片内，无外部接线——spi1 = PA00/PA01/PA02 + CS GPIOA 3；spi2 = PJ12/PJ13/PJ14 + CS GPIOJ 15。 (B) 无外部接线——板载 spif3 master -> spi4 -> flash4_0，测试区 0x100000/0x110000/0x120000。${COLOR_RESET}"
echo ""
echo -e "${COLOR_GREEN}Build OK:${COLOR_RESET}"
echo "  ${TEST_BUILD_DIR}"
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""

fi

if [ "${AGENT_RUN}" != "y" ]; then
# 3. Print launch.json snippet for manual copy-paste.
print_launch_json_single "${TEST_NAME}" "${TARGET_ELF}"
fi

# 4. Optionally load into RAM and run.
if [ "${SKIP_LOAD}" != "y" ]; then
    echo ""
    load_and_run_single "${TEST_NAME}" "${TARGET_ELF}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 5. Hint when GDB load is skipped.
if [ "${AUTO_GDB}" != "y" ]; then
    echo ""
    echo -e "${COLOR_YELLOW}Hint:${COLOR_RESET} run with ${COLOR_GREEN}-g${COLOR_RESET} to automatically load and run via GDB."
    echo -e "  ${COLOR_CYAN}$0 -g${COLOR_RESET}"
    echo ""
fi
fi
