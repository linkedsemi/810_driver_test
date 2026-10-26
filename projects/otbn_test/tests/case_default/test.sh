#!/usr/bin/env bash
# OTBN single-core test (cpu1 secure domain, OTBN crypto accelerator)
#
# What is tested:
#   OTBN 适配层全路径（selftest 广谱套件在兄弟工程 projects/otbn_selftest/）：
#   Phase 1 api_*  OTBN 适配接口 KAT：sha224/256/sm3/384/512、sha256 多长度/偏移、
#                  aes/sm4（硬件引擎）、rsa2048 导入 key KAT、rsa keygen 2048/3072/4096、
#                  ecdsa p256 定 k KAT / p384 / sm2；perf_* 性能 INFO 上报
#   Phase 2 mix_*  OTBN 固件混用复现：RSA keygen/modexp 与 sha256/sha512/sm3
#                  来回穿插（已知问题：混用导致 hash 结果出错，本组测试判定该 bug）
#   Phase 3 keygen_* RSA 密钥生成（合并自 projects/otbn_keygen_test）：
#                  candidate 残差/顶位、直接 API、mbedtls hook、2048/3072/4096
#                  正确性与确定性、ref_vector KAT、RNG 错误传播、session 泄漏、
#                  非对齐拒绝、modexp KAT 与 bench（TD_CANDIDATES 降到 100）。
#
# CPU1 (target role, TGT token):
#   - 单核工程，无对侧核；main() 主动自驱动跑完所有套件。
#
# Normal behavior:
#   52 项测试 START→PASS，结尾 SUMMARY:END <passed>/<ran> PASS + DONE:PASS。
#   （若 RSA/SHA256 混用 bug 未修复，mix_* 项 FAIL 并带 byte[i] 差异 reason。）
#
# Abnormal behavior:
#   任一测试 FAIL 会打 FAIL token + 可归因 reason（§13.5）；
#   OTBN 挂死典型表现：ls_otbn_cmd timeout / wait idle timeout（LOG_ERR）。
#
# Feedback:
#   cpu1 uart1（COM16/17，CH342）；banner 'CPU1 OTBN' → TGT。
#
# Hardware IO connections:
#   无外部接线（OTBN 为片内协处理器，CLAUDE.md §14 片内标注）。
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
    # 全套件含 3 次 RSA keygen（4096 可达 10s 级）+ selftest，超时放宽
    RUN_TIMEOUT="${RUN_TIMEOUT:-500}"
fi

TARGET_SRC="${I3C_TEST_ROOT}/cpu1_otbn_test"
TARGET_OVERLAY="$(realpath "${TARGET_SRC}/app.overlay")"

TEST_BUILD_DIR="${I3C_TEST_ROOT}/tests/${TEST_NAME}/build/target"
TARGET_ELF="${TEST_BUILD_DIR}/zephyr/zephyr.elf"

if [ "${AGENT_RUN}" != "y" ]; then
# 1. Build target image first.
build_single_test "${TEST_NAME}" "${TARGET_SRC}" "${TARGET_OVERLAY}" "" "${TEST_BUILD_DIR}"
fi

if [ "${AGENT_RUN}" != "y" ]; then
# 2. Print test summary after build so it is not buried in build logs.
echo ""
echo -e "${COLOR_GREEN}================================================================================${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Test: ${TEST_NAME}${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}What: OTBN 整合回归：selftest 套件 → 适配接口 KAT（sha/aes/sm4/rsa/ecdsa/keygen）→ RSA/SHA 混用复现。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Normal: 全部 PASS + DONE:PASS（mix_* 在 bug 未修时预期 FAIL）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Abnormal: FAIL token + 可归因 reason（byte[i]:0xXX!=0xYY / ret 码）。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}Feedback: cpu1 uart1（COM16/17，CH342）；banner 'CPU1 OTBN' → TGT。${COLOR_RESET}"
echo ""
echo -e "${COLOR_YELLOW}IO: 无外部接线（片内协处理器，CLAUDE.md §14 片内标注）。${COLOR_RESET}"
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
