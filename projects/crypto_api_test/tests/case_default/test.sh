#!/usr/bin/env bash
# Crypto API single-core test (cpu1 secure domain, Zephyr crypto driver API)
#
# What is tested (case_default): 三颗驱动编入同一镜像
#   zephyr/drivers/crypto/crypto_linkedsemi_sm4.c（sm4@40029400）
#     - SM4 ECB / CBC / CTR / CCM / GCM 五模式，各 KAT + 解密回环（AEAD 校验 tag）
#   zephyr/drivers/crypto/crypto_linkedsemi_sha256.c（sha256@40002800）
#     - SHA224 / SHA256 / SM3 各短报文 KAT + 分段 update 等价
#   zephyr/drivers/crypto/crypto_linkedsemi_sha512.c（sha512@40005000）
#     - SHA384 / SHA512 各短报文 KAT + 分段 update 等价
#     - SHA256 / SHA512 各一条 padding 边界（0/55/56/64 与 0/111/112/128 字节）
#
# CPU1 (single core, TGT):
#   - 单核工程，无对侧核；test_main → zt_init + zt_run_all 纯 ztest。
#
#   zephyr/drivers/crypto/crypto_linkedsemi_*.c 的错误返回（负例）：不支持的
#   algo / keylen / flags / mode → -ENOTSUP；CCM 参数越界 → -EINVAL（带正对照）。
#
# Normal behavior:
#   28 项全部 PASS（sm4_api ×5 + hash_api ×12 + sm4_err ×6 + hash_err ×5）
#   + PROJECT EXECUTION SUCCESSFUL。
#   每项带 INFO 约定行（algo/bytes/kat/roundtrip/segments/ret）。
#
# Abnormal behavior:
#   任一项 FAIL 带 zassert 可归因 reason（digest != reference vector / ret 码）。
#
# Feedback:
#   cpu1 uart1（COM16/17，CH342）；banner 'CPU1 CRYPTO API' → TGT。
#
# Hardware IO connections:
#   无外部接线（片内 SHA/SM3 引擎，cpu1 安全域，CLAUDE.md §14 片内）。
#
# Build artifacts:
#   - tests/case_default/build/（zephyr.elf，dbg_run/dump/run 脚本共用此目录）
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
    RUN_TIMEOUT="${RUN_TIMEOUT:-120}"
fi

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_crypto_api_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"
if [ -f "${SCRIPT_DIR}/case.conf" ]; then
    CASE_CONF="$(realpath "${SCRIPT_DIR}/case.conf")"
else
    CASE_CONF=""
fi

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
# 1. Build target image first（conf 差异经 CONF_FILE=prj.conf;case.conf 注入）.
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "${CASE_CONF}" "${TEST_BUILD_DIR}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 2. Print test summary after build so it is not buried in build logs.
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}What: SM4 五模式 + SHA224/256/384/512 + SM3 经 Zephyr crypto 驱动 API：KAT + 回环/分段等价 + padding 边界 + 错误返回（-ENOTSUP/-EINVAL）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: 28/28 PASS（sm4_api 5 + hash_api 12 + sm4_err 6 + hash_err 5）+ PROJECT EXECUTION SUCCESSFUL。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: FAIL - <name> + zassert reason（ciphertext/tag/digest != reference vector，round trip 不符）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM16/17，CH342）；banner 'CPU1 CRYPTO API' → TGT。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: 无外部接线（片内 SHA/SM3 引擎，CLAUDE.md §14 片内标注）。${COLOR_RESET}"
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
