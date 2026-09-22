#!/usr/bin/env bash
# Common helpers for dual-core per-test automation (V2).
# Generic: shared by all <driver>_test projects under 810_driver_test.
# Source this file from tests/case_*/test.sh (set TEST_ROOT first).
#
# Required environment:
#   - west, riscv64-unknown-elf-gdb in PATH
#   - XuanTie DebugServer (PC 进程) reachable at ${GDB_SERVER_HOST}:${GDB_SERVER_PORT}
#     (DebugServerConsole.exe 提供 1025 端口，bin/default.ini 的 SOCKETPORT；
#     不是 CKLink 固件内置。被 CSKYFlashProgrammer.exe 占用时先杀之)
#
# Required variable (set before sourcing):
#   TEST_ROOT             path to the <driver>_test project root
#
# Optional environment overrides:
#   GDB_SERVER_HOST       (default: localhost)
#   GDB_SERVER_PORT       (default: 1025)
#   RUN_TIMEOUT           (default: 10s, 0 means run until manually stopped)
#   SKIP_LOAD             (set to "y" to skip GDB load/run)
#   SKIP_LAUNCH_PRINT     (set to "y" to skip launch snippet print)
#   CONTROLLER_BOARD      (default: lsqsh_evb@2os/lsqsh/cpu2)
#   TARGET_BOARD          (default: lsqsh_evb@2os/lsqsh/cpu1)
#   CONTROLLER_LOAD_ADDR  (default: 0x10000000)
#   TARGET_ROM_START      (default: 0x100c0000)
#   CPU1_XIP_ADDR         (default: 0x08000000, XIP flash base, cpu1; CLAUDE.md §15)
#   CPU2_XIP_ADDR         (default: 0x08100000, XIP flash base, cpu2)
#   FP_EXE / FP_ALGO      (flash programmer exe / algorithm ELF, §15; scripts/flash_program.py)
#   I3C_CTRL_UART         (controller UART port, e.g. COM3, used by agent)
#   I3C_TGT_UART          (target UART port, e.g. COM4, used by agent)
#   AGENT_RUN             (set to "y" by test.sh -r; load/run only for agent)

set -e

COMMON_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${COMMON_SCRIPT_DIR}/.." && pwd)"   # = 810_driver_test
# 工程根（<driver>_test 目录）必须由调用方在 source 前设置，例如：
#   TEST_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"   # case 的 test.sh 内
: "${TEST_ROOT:?TEST_ROOT must be set before sourcing test_common.sh}"
I3C_TEST_ROOT="${TEST_ROOT}"

GDB_SERVER_HOST="${GDB_SERVER_HOST:-127.0.0.1}"
GDB_SERVER_PORT="${GDB_SERVER_PORT:-1025}"
RUN_TIMEOUT="${RUN_TIMEOUT:-10}"
SKIP_LOAD="${SKIP_LOAD:-}"
SKIP_LAUNCH_PRINT="${SKIP_LAUNCH_PRINT:-}"
AGENT_RUN="${AGENT_RUN:-}"
I3C_CTRL_UART="${I3C_CTRL_UART:-}"
I3C_TGT_UART="${I3C_TGT_UART:-}"
CONTROLLER_BOARD="${CONTROLLER_BOARD:-lsqsh_evb@2os/lsqsh/cpu2}"
TARGET_BOARD="${TARGET_BOARD:-lsqsh_evb@2os/lsqsh/cpu1}"
# 用户需求#8：单核固件一律用 1os 编译测试（双核路径的 TARGET_BOARD 保持 2os 不动）
SINGLE_BOARD="${SINGLE_BOARD:-lsqsh_evb@1os/lsqsh/cpu1}"
CONTROLLER_LOAD_ADDR="${CONTROLLER_LOAD_ADDR:-0x10000000}"
TARGET_ROM_START="${TARGET_ROM_START:-0x100c0000}"
# 用户需求#8：单核一律 1os 构建（SINGLE_BOARD），cpu1 入口 = 0x10000000
# （1os readelf 实测）；TARGET_ROM_START 保持双核 2os cpu1 语义 0x100c0000 不动。
# 两变量分工（shell 侧入口）：SINGLE_ROM_START = 单核路径
# （load_and_run_single / print_launch_json_single 用），默认 0x10000000；
# TARGET_ROM_START = 双核路径，默认 0x100c0000。
# 注意 Python 脚本是另一套入口：4 个采集脚本（run×2 / dbg_run×2）与
# dump_results.py 读 TARGET_ROM_START 环境变量、默认 0x10000000（1os 语义；
# 2os 时需显式 TARGET_ROM_START=0x100c0000 覆盖）——与 shell 侧命名分工
# 不同，CLAUDE.md 的统一同步由 Task 12 做，本文件只加注释不改名。
SINGLE_ROM_START="${SINGLE_ROM_START:-0x10000000}"
# XIP flash 固化（CLAUDE.md §15）：地址信息集中在此掌握，test.sh 直接引用
CPU1_XIP_ADDR="${CPU1_XIP_ADDR:-0x08000000}"
CPU2_XIP_ADDR="${CPU2_XIP_ADDR:-0x08100000}"
# ★ 公共构建注入点（默认全空 ⇒ 既有调用方走**逐字节相同**的代码路径；KNOWN_ISSUES #70/#76）：
#   EXTRA_CMAKE_ARGS  追加到 `west build` 的任意 -D<var>=<val>（空格分隔，可多个）；
#                     也可由 build_* 的第 6 个位置参数按次传入（两者叠加）。
#   BOARD_OVERRIDE    强制 build 的 board，跳过 _detect_board 的目录名关键词判板
#                     （如 src 目录名不含 controller/target 关键词的 cpu1_<driver>_test，#70）。
#   CONF_MODE         append（默认）= <src>/prj.conf 前拼第 4 参 conf（原行为）；
#                     replace = **只用**第 4 参 conf，不拼上游 prj.conf（复用上游
#                     test/sample 需整体替换其 prj.conf 时用，#76）。
EXTRA_CMAKE_ARGS="${EXTRA_CMAKE_ARGS:-}"
BOARD_OVERRIDE="${BOARD_OVERRIDE:-}"
CONF_MODE="${CONF_MODE:-append}"
# FP_EXE/FP_ALGO 默认值依赖 win_path()（第 77 行），定义在其后（见下）

DEFAULT_CONTROLLER_SRC="${I3C_TEST_ROOT}/cpu0_i3c_controller"
DEFAULT_TARGET_SRC="${I3C_TEST_ROOT}/cpu1_i3c_target"

# Terminal color helpers.
if [ -t 1 ]; then
    COLOR_YELLOW='\033[1;33m'
    COLOR_GREEN='\033[1;32m'
    COLOR_CYAN='\033[1;36m'
    COLOR_RESET='\033[0m'
else
    COLOR_YELLOW=''
    COLOR_GREEN=''
    COLOR_CYAN=''
    COLOR_RESET=''
fi

# Convert a path to Windows mixed format (D:/...) if running under MSYS/Cygwin.
# Falls back to forcing forward slashes otherwise.
win_path() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "$1"
    else
        printf '%s' "$1" | sed 's/\\/\//g'
    fi
}

# Flash 烧录工具默认值（CLAUDE.md §15）：工程内打包副本优先
# （tools/flash_program/，自包含可拷贝），外部 C:\user_file\... 回退。
# 依赖 win_path()，须定义在其后；用户可用 FP_EXE/FP_ALGO 覆盖。
if [ -z "${FP_EXE:-}" ]; then
    _FP_TOOLS="${WORKSPACE_ROOT}/tools/flash_program"
    if [ -f "${_FP_TOOLS}/CSKYFlashProgramerConsole.exe" ]; then
        FP_EXE="$(win_path "${_FP_TOOLS}/CSKYFlashProgramerConsole.exe")"
    else
        FP_EXE="C:/user_file/EC_FLASH_PROGRAM/FlashProgrammer/Bins/CSKYFlashProgramerConsole.exe"
    fi
fi
if [ -z "${FP_ALGO:-}" ]; then
    _FP_TOOLS="${WORKSPACE_ROOT}/tools/flash_program"
    if [ -f "${_FP_TOOLS}/qsh_flash_algo.elf" ]; then
        FP_ALGO="$(win_path "${_FP_TOOLS}/qsh_flash_algo.elf")"
    else
        FP_ALGO="C:/user_file/EC_FLASH_PROGRAM/qsh_flash_algo.elf"
    fi
fi

# Escape a string for JSON output.
json_escape() {
    # Prefer plain 'python' on Windows/Git Bash to avoid the WindowsApps shim.
    if command -v python >/dev/null 2>&1 && python -c 'import sys,json' >/dev/null 2>&1; then
        python -c 'import sys,json; print(json.dumps(sys.argv[1]), end="")' "$1"
    elif command -v python3 >/dev/null 2>&1 && python3 -c 'import sys,json' >/dev/null 2>&1; then
        python3 -c 'import sys,json; print(json.dumps(sys.argv[1]), end="")' "$1"
    else
        printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; s/\t/\\t/g'
    fi
}

# Compute path relative to workspace root, with forward slashes.
rel_path() {
    local abs_path="$1"
    local rel
    if command -v realpath >/dev/null 2>&1; then
        rel="$(realpath --relative-to="${WORKSPACE_ROOT}" "${abs_path}" 2>/dev/null || echo "${abs_path}")"
    else
        rel="${abs_path}"
    fi
    # Force forward slashes for launch.json compatibility.
    printf '%s' "${rel}" | sed 's/\\/\//g'
}

# Detect board qualifier from source directory name.
_detect_board() {
    local src_dir="$1"
    local base
    base="$(basename "${src_dir}")"
    case "${base}" in
        *controller*|*master*|*host*)
            echo "${CONTROLLER_BOARD}"
            ;;
        *target*|*slave*|*device*)
            echo "${TARGET_BOARD}"
            ;;
        *)
            # ★ 2026-09-22：报错时点出兜底开关。判据本身保留"关键词 + 猜不中即
            #   fail-loud"——SPEC_REFERENCE §6.1.10 的理想判据是"工程根下是否存在
            #   cpu1 之外的固件目录"，但改判据会影响全部工程的板级选型，而选错是
            #   **静默烧错板级**（不是构建失败），风险大于收益。故改用显式覆盖：
            #   调用方（生成器 / 人）知道分型时直接给 BOARD_OVERRIDE，比让脚本猜安全。
            echo "ERROR: cannot detect board for ${src_dir}" >&2
            echo "       （目录名不含 controller/master/host 也不含 target/slave/device）" >&2
            echo "       处置：设环境变量 BOARD_OVERRIDE=<board> 显式指定，跳过本判据。" >&2
            echo "       分型判据见 SPEC_REFERENCE.md §6.1.10。" >&2
            return 1
            ;;
    esac
}

# Build one image for a test.
# Usage: build_test <test_name> <src_dir> <overlay_path> <conf_path> <build_dir>
build_test() {
    local test_name="$1"
    local src_dir="$2"
    local overlay="$3"
    local conf="$4"
    local build_dir="$5"
    local extra_cmake="${6:-}"          # 可选：额外 CMake 变量（空格分隔，追加到 west build）

    local board
    if [ -n "${BOARD_OVERRIDE}" ]; then
        board="${BOARD_OVERRIDE}"       # 显式兜底，跳过目录名关键词判板（#70）
    else
        board="$(_detect_board "${src_dir}")" || return 1
    fi

    local extra_args=()
    if [ -n "${overlay}" ]; then
        extra_args+=("-DDTC_OVERLAY_FILE=${overlay}")
    fi
    if [ -n "${conf}" ]; then
        local base_conf="${src_dir}/prj.conf"
        if [ "${CONF_MODE}" = "replace" ]; then
            # #76：整体替换（只用本参数 conf，不前拼 <src>/prj.conf）
            extra_args+=("-DCONF_FILE=$(win_path "${conf}")")
        elif [ -f "${base_conf}" ]; then
            base_conf="$(win_path "$(realpath "${base_conf}")")"
            conf="$(win_path "${conf}")"
            extra_args+=("-DCONF_FILE=${base_conf};${conf}")
        else
            extra_args+=("-DCONF_FILE=${conf}")
        fi
    fi
    # ★ 额外 CMake 变量透传（#70）：env EXTRA_CMAKE_ARGS + 第 6 参，空格分隔。
    #   两者皆空时 for 列表为空 ⇒ **不追加任何元素**，既有调用路径逐字节不变。
    local _tok
    # shellcheck disable=SC2086
    for _tok in ${EXTRA_CMAKE_ARGS} ${extra_cmake}; do
        extra_args+=("${_tok}")
    done

    echo "[${test_name}] Build $(basename "${src_dir}") -> ${build_dir}"
    west build -p always \
        -d "${build_dir}" \
        -b "${board}" \
        "${src_dir}" \
        "${extra_args[@]}"
}

# Print a VSCode launch.json configuration snippet to stdout.
# Usage: print_launch_json <test_name> <target_elf> <controller_bin>
print_launch_json() {
    local test_name="$1"
    local target_elf="$2"
    local controller_bin="$3"

    local target_elf_rel controller_bin_rel
    target_elf_rel="$(rel_path "${target_elf}")"
    controller_bin_rel="$(rel_path "${controller_bin}")"

    local name_json target_json exe_json
    name_json="$(json_escape "${test_name}")"
    target_json="$(json_escape "${GDB_SERVER_HOST}:${GDB_SERVER_PORT}")"
    exe_json="$(json_escape "${target_elf_rel}")"

    cat <<EOF

===== VSCode launch.json snippet for ${name_json} =====
{
    "name": ${name_json},
    "type": "gdb",
    "request": "attach",
    "target": ${target_json},
    "executable": ${exe_json},
    "cwd": "\${workspaceFolder}",
    "gdbpath": "riscv64-unknown-elf-gdb",
    "remote": true,
    "autorun": [
        "set output-radix 16",
        "set \$ROM_START = ${TARGET_ROM_START}",
        "reset",
        "load",
        "restore ${controller_bin_rel} binary ${CONTROLLER_LOAD_ADDR}",
        "set \$pc=\$ROM_START",
        "set mem inaccessible-by-default on",
        "set print pretty on"
    ],
    "configurations": []
}
===== End of launch.json snippet =====

EOF
}

# F5（2026-09-20）产物新鲜度自检：把"参考产物"逐个与所有构建输入源码比 mtime；
# 有源码更新 ⇒ 这份产物是旧源码编出来的，可能在跑旧断言（假 FAIL 或假 PASS）。
# **只告警不阻断**：阻断交给 regression/run_all.py 的 --skip-build 门（那里能直接
# 转成"强制重建"）。这里是给 `./test.sh -r` / dbg 这类绕过 run_all 的入口兜底，
# 让"复用陈旧产物"不再静默。
# 输入集 = 工程全树 + **本工程真正链入的** common/ 文件，但**排除**
# scripts/test_common.sh：它是构建配方，`west build -p auto` 的 ninja 不跟踪它，
# 一旦改了就永远清不掉这条告警（ELF mtime 不会因此更新）。配方变更由 run_all 的
# 门负责强制重建。
#
# common/ 的收窄（2026-09-20 修正）：common/ 是**公共目录**，但并非每份都进每个
# 工程 —— `rlog.c` 只在 i3c_i2c_switch_test 的 CMakeLists 里显式编入。原先把整个
# common/ 当输入 ⇒ common/ 每新增一个文件就让**所有**工程的产物判"陈旧"，全部误报
# WARN；告警被淹没后就不再有任何信号价值（**宁可漏报也不误报**）。
# 判据见 _common_build_inputs()。
# Usage: warn_if_build_stale <ref_path> [<ref_path> ...]
_common_build_inputs() {
    # 输出本工程会链入的 common/ 文件（每行一个绝对路径，无空格）。
    # 判据：文件基名出现在 (本工程各 CMakeLists) 或 (common_sources.cmake 的源
    # 列表) —— 后者是所有工程的公共入口，故 test_results/ztest_glue/gpio_toggle
    # 恒被计入。头文件按**同干 .c** 被引用即算（test_results.h ⇐ test_results.c）。
    # 取不到引用集时输出为空 = 不把任何 common/ 文件当输入（漏报可接受，误报不可）。
    local refs f base stem
    refs="$(grep -rhoE '[A-Za-z0-9_][A-Za-z0-9_.-]*\.(c|h|S|s|ld|lds|inc)' \
        ${I3C_TEST_ROOT}/*/CMakeLists.txt \
        "${WORKSPACE_ROOT}/common/common_sources.cmake" 2>/dev/null | sort -u)"
    [ -z "${refs}" ] && return 0
    for f in "${WORKSPACE_ROOT}/common"/*; do
        [ -f "${f}" ] || continue
        base="$(basename "${f}")"
        stem="${base%.*}"
        case "${base}" in
            *.c|*.h|*.S|*.s|*.ld|*.lds|*.overlay|*.conf|*.dtsi|*.dts|*.inc|*.cmake|*.sh|CMakeLists.txt) ;;
            *) continue ;;
        esac
        if printf '%s\n' "${refs}" | grep -qxF "${base}" \
           || printf '%s\n' "${refs}" | grep -qxF "${stem}.c" \
           || printf '%s\n' "${refs}" | grep -qxF "${stem}.cmake"; then
            printf '%s\n' "${f}"
        fi
    done
}

# 新鲜度扫描的硬上限（秒）。★ 存在的理由（2026-09-21）：
#   本函数的 find 遍历整个工程树，而调用点紧挨着 load_and_run 的**第一条
#   stdout**。未剪枝时实测 i3c_dualcore 树（56,501 文件，其中 56,353 在 build
#   下）单次耗时 **121s**，远超 harness 的判活窗口（common/acquire.py 的
#   LOAD_WINDOW_S=20）⇒ 被判 load_fail。现象与"真环境故障"完全同形
#   （prime 20s 零输出 + gdb 也无输出），极难归因 —— 2026-09-21 那轮 i3c
#   case 集体失败即此（由 43b232e 引入本函数触发）。
#   剪枝后实测 ~3s；此上限是第二道保险：无论树多大、IO 多慢，本函数都
#   不可能吃掉判活窗口（它是 WARN，从不定判定，超时跳过无副作用）。
STALE_SCAN_TIMEOUT_S="${STALE_SCAN_TIMEOUT_S:-8}"

warn_if_build_stale() {
    local ref newer common_inputs raw rc extra rc2
    common_inputs="$(_common_build_inputs)"
    for ref in "$@"; do
        [ -f "${ref}" ] || continue
        # ★ 用 -prune 剪枝，而不是事后 -not -path 过滤：后者仍会下降进每个
        #   build/ 目录并 stat 其中每个文件 —— 实测 121s vs 剪枝 ~3s（40 倍）。
        #   语义等价：build/ 与 tmp/ 下不会有被链入的源文件。
        # ★★ `|| rc=$?` 不可省（OPEN_ITEMS O-25）：`raw="$(cmd)"` 是"仅赋值 +
        #    末位命令替换"的简单命令，bash 取命令替换的退出码 ⇒ 命令替换失败时
        #    整条赋值即"失败命令"，**set -e 会在赋值那一步终止脚本**，后面的
        #    `rc=$?` / 超时分支根本轮不到执行。实测：超时触发时脚本直接退出
        #    rc=124，[WARN]…已跳过 打不出来。用 `||` 接住才使该分支可达。
        rc=0
        raw="$(timeout "${STALE_SCAN_TIMEOUT_S}" find "${I3C_TEST_ROOT}" \
            \( -name build -o -name tmp -o -name .git \) -prune -o \
            -type f \
            \( -name '*.c' -o -name '*.h' -o -name '*.cc' -o -name '*.hpp' \
               -o -name '*.S' -o -name '*.s' -o -name '*.ld' -o -name '*.lds' \
               -o -name '*.overlay' -o -name '*.conf' -o -name '*.dtsi' \
               -o -name '*.dts' -o -name '*.inc' -o -name '*.cmake' \
               -o -name 'CMakeLists.txt' -o -name '*.sh' \) \
            -newer "${ref}" -print 2>/dev/null)" || rc=$?
        if [ "${rc}" = "124" ]; then
            echo -e "${COLOR_YELLOW}[WARN] 产物新鲜度检查超时" \
                "(>${STALE_SCAN_TIMEOUT_S}s)，已跳过（不影响判定）${COLOR_RESET}" >&2
            continue
        fi
        if [ -n "${common_inputs}" ]; then
            # 同上的 set -e 写法（这一条查的是显式小文件表，正常不会超时，
            # 但写法必须一致，否则将来列表变大时又是一颗雷）。
            extra=""
            rc2=0
            extra="$(timeout "${STALE_SCAN_TIMEOUT_S}" find ${common_inputs} \
                -newer "${ref}" 2>/dev/null)" || rc2=$?
            if [ "${rc2}" = "124" ]; then
                echo -e "${COLOR_YELLOW}[WARN] 产物新鲜度检查超时" \
                    "(>${STALE_SCAN_TIMEOUT_S}s)，已跳过（不影响判定）${COLOR_RESET}" >&2
                continue
            fi
            raw="${raw:+${raw}
}${extra}"
        fi
        # 滤空行：raw 为空串时前面会带出一个空行，否则会被误判成"有新文件"。
        newer="$(printf '%s\n' "${raw}" | grep -v '^[[:space:]]*$' | head -5)"
        if [ -n "${newer}" ]; then
            echo -e "${COLOR_YELLOW}[WARN] 产物 $(basename "${ref}") 早于下列输入源码，" \
                "可能在烧旧断言（假 FAIL / 假 PASS）：${COLOR_RESET}" >&2
            echo "${newer}" >&2
            echo -e "${COLOR_YELLOW}[WARN] 请先 ./test.sh 重建再跑；" \
                "并核对是否走了 --skip-build 快路径。${COLOR_RESET}" >&2
        fi
    done
}

# Load both images into RAM and continue execution.
# Usage: load_and_run <test_name> <target_elf> <controller_bin>
load_and_run() {
    local test_name="$1"
    local target_elf="$2"
    local controller_bin="$3"

    if [ ! -f "${target_elf}" ]; then
        echo "ERROR: target elf not found: ${target_elf}"
        return 1
    fi
    if [ ! -f "${controller_bin}" ]; then
        echo "ERROR: controller bin not found: ${controller_bin}"
        return 1
    fi

    # ★ 顺序有意：先打加载横幅，再做新鲜度检查。检查含一次工程树遍历，
    #   若排在横幅**之前**，harness 的判活窗口（LOAD_WINDOW_S）会在"还没
    #   有任何 stdout"时就开始计时 ⇒ 检查一慢就被判 load_fail（2026-09-21
    #   的 i3c case 集体失败即此形态）。横幅先出 ⇒ 窗口从"已见到输出"起算。
    echo "[${test_name}] Loading images into RAM via GDB..."
    echo "    target ELF:   ${target_elf}"
    echo "    controller BIN: ${controller_bin}"

    warn_if_build_stale "${target_elf}" "${controller_bin}"

    # gdb 是 Windows 程序，只认 D:/... 形式；test.sh 经 pwd 推导的
    # MSYS 绝对路径（/d/...）会报 "No such file or directory" 并停在
    # 交互式 (gdb) 提示符（无 -batch，错误不退出）。
    local gdb_target_elf gdb_controller_bin
    gdb_target_elf="$(win_path "${target_elf}")"
    gdb_controller_bin="$(win_path "${controller_bin}")"

    if [ "${AGENT_RUN}" = "y" ]; then
        echo "I3C_AGENT:RUN_START:${test_name}"
    fi

    # 同一 gdb 会话里连续两个大块写（load ELF 后 restore bin）会卡死：
    # 第二个大块写停在 100% 进度后无响应（DebugServer 对同一 RSP 会话的
    # 第二次大块写不回应，实测必现）。拆成两个独立 gdb 会话：
    #   A. load target ELF -> detach
    #   B. restore controller bin -> continue
    # reset 只复位 CPU 不清 RAM，B 的 reset 不影响 A 已 load 的内容。
    local rc=0

    local gdb_a=(
        riscv64-unknown-elf-gdb
        -batch
        -ex "target remote ${GDB_SERVER_HOST}:${GDB_SERVER_PORT}"
        -ex "set output-radix 16"
        -ex "set confirm off"
        -ex "set pagination off"
        -ex "set \$ROM_START = ${TARGET_ROM_START}"
        -ex "file ${gdb_target_elf}"
        -ex "reset"
        -ex "load"
        -ex "set \$pc=\$ROM_START"
        -ex "set mem inaccessible-by-default on"
        -ex "set print pretty on"
        -ex "detach"
    )
    if [ "${RUN_TIMEOUT}" = "0" ]; then
        "${gdb_a[@]}" || rc=$?
    else
        timeout "${RUN_TIMEOUT}s" "${gdb_a[@]}" || rc=$?
    fi
    if [ ${rc} -ne 0 ]; then
        echo "ERROR: [${test_name}] load target ELF failed (rc=${rc})" >&2
        if [ "${AGENT_RUN}" = "y" ]; then
            echo "I3C_AGENT:RUN_END:${test_name}:${rc}"
        fi
        return ${rc}
    fi

    local gdb_b=(
        riscv64-unknown-elf-gdb
        -batch
        -ex "target remote ${GDB_SERVER_HOST}:${GDB_SERVER_PORT}"
        -ex "set output-radix 16"
        -ex "set confirm off"
        -ex "set pagination off"
        -ex "set \$ROM_START = ${TARGET_ROM_START}"
        -ex "reset"
        -ex "restore ${gdb_controller_bin} binary ${CONTROLLER_LOAD_ADDR}"
        -ex "set \$pc=\$ROM_START"
        -ex "set mem inaccessible-by-default on"
        -ex "set print pretty on"
        -ex "continue"
    )
    if [ "${RUN_TIMEOUT}" = "0" ]; then
        "${gdb_b[@]}" || rc=$?
    else
        timeout "${RUN_TIMEOUT}s" "${gdb_b[@]}" || rc=$?
    fi

    if [ "${AGENT_RUN}" = "y" ]; then
        echo "I3C_AGENT:RUN_END:${test_name}:${rc}"
    fi

    return ${rc}
}

# =====================================================================
# Single-core helpers (for drivers that live on cpu1 only, e.g. TRNG:
# dwtrng@40004000 is in the cpu1 secure domain and has no bus peer).
# These are additive; dual-core projects keep using the functions above.
# =====================================================================

# Build one image for a single-core test (always the cpu1/TARGET_BOARD).
# Usage: build_single_test <test_name> <src_dir> <overlay_path> <conf_path> <build_dir>
build_single_test() {
    local test_name="$1"
    local src_dir="$2"
    local overlay="$3"
    local conf="$4"
    local build_dir="$5"
    local extra_cmake="${6:-}"          # 可选：额外 CMake 变量（空格分隔，追加到 west build）

    local board="${SINGLE_BOARD}"
    if [ -n "${BOARD_OVERRIDE}" ]; then
        board="${BOARD_OVERRIDE}"       # 显式兜底（默认空 ⇒ 仍用 SINGLE_BOARD）
    fi

    local extra_args=()
    if [ -n "${overlay}" ]; then
        extra_args+=("-DDTC_OVERLAY_FILE=${overlay}")
    fi
    if [ -n "${conf}" ]; then
        local base_conf="${src_dir}/prj.conf"
        if [ "${CONF_MODE}" = "replace" ]; then
            # #76：整体替换（只用本参数 conf，不前拼 <src>/prj.conf）
            extra_args+=("-DCONF_FILE=$(win_path "${conf}")")
        elif [ -f "${base_conf}" ]; then
            base_conf="$(win_path "$(realpath "${base_conf}")")"
            conf="$(win_path "${conf}")"
            extra_args+=("-DCONF_FILE=${base_conf};${conf}")
        else
            extra_args+=("-DCONF_FILE=${conf}")
        fi
    fi
    # ★ 额外 CMake 变量透传（#70）：env EXTRA_CMAKE_ARGS + 第 6 参，空格分隔。
    #   两者皆空时 for 列表为空 ⇒ **不追加任何元素**，既有调用路径逐字节不变。
    local _tok
    # shellcheck disable=SC2086
    for _tok in ${EXTRA_CMAKE_ARGS} ${extra_cmake}; do
        extra_args+=("${_tok}")
    done

    echo "[${test_name}] Build $(basename "${src_dir}") -> ${build_dir}"
    west build -p always \
        -d "${build_dir}" \
        -b "${board}" \
        "${src_dir}" \
        "${extra_args[@]}"
}

# Print a VSCode launch.json snippet for a single-core (cpu1 ELF only) test.
# Usage: print_launch_json_single <test_name> <elf>
print_launch_json_single() {
    local test_name="$1"
    local elf="$2"

    local elf_rel
    elf_rel="$(rel_path "${elf}")"

    local name_json target_json exe_json
    name_json="$(json_escape "${test_name}")"
    target_json="$(json_escape "${GDB_SERVER_HOST}:${GDB_SERVER_PORT}")"
    exe_json="$(json_escape "${elf_rel}")"

    cat <<EOF

===== VSCode launch.json snippet for ${name_json} =====
{
    "name": ${name_json},
    "type": "gdb",
    "request": "attach",
    "target": ${target_json},
    "executable": ${exe_json},
    "cwd": "\${workspaceFolder}",
    "gdbpath": "riscv64-unknown-elf-gdb",
    "remote": true,
    "autorun": [
        "set output-radix 16",
        "reset",
        "load",
        "set \$pc=${SINGLE_ROM_START}",
        "set mem inaccessible-by-default on",
        "set print pretty on"
    ],
    "configurations": []
}
===== End of launch.json snippet =====

EOF
}

# Load a single cpu1 image into RAM and continue (no second core to restore).
# Usage: load_and_run_single <test_name> <elf>
load_and_run_single() {
    local test_name="$1"
    local elf="$2"

    if [ ! -f "${elf}" ]; then
        echo "ERROR: elf not found: ${elf}"
        return 1
    fi

    echo "[${test_name}] Loading cpu1 image into RAM via GDB..."
    echo "    ELF: ${elf}"

    # ★ 同 load_and_run：横幅先出、新鲜度检查后置（理由见那里）。
    warn_if_build_stale "${elf}"

    # gdb.exe 是 Windows 原生进程，不吃 MSYS 的 /d/... 路径；转成 D:/...
    # （win_path 与脚本内 FP_EXE 等同款处理）
    local elf_win
    elf_win="$(win_path "${elf}")"

    if [ "${AGENT_RUN}" = "y" ]; then
        echo "I3C_AGENT:RUN_START:${test_name}"
    fi

    # 用户需求#7：单核路径按住 cpu2 复位（SYSC_SEC_CPU.APP_CPU_SRST=0x4002200c
    # 是 W1S/W1C 寄存器：写 2=保持复位（SDK app_cpu_reset 语义）、写 1=释放
    # （app_cpu_dereset 语义），写 0 无效果）。防他方 RAM 残留镜像串扰本固件
    # 采集。跑完不释放（后续双核流程自己会写 1 释放）；load_and_run（双核）
    # 绝对不加。
    local gdb_cmd=(
        riscv64-unknown-elf-gdb
        -ex "target remote ${GDB_SERVER_HOST}:${GDB_SERVER_PORT}"
        -ex "set output-radix 16"
        -ex "set confirm off"
        -ex "set pagination off"
        -ex "set \$ROM_START = ${SINGLE_ROM_START}"
        -ex "file ${elf_win}"
        -ex "reset"
        -ex "monitor flash erase_address 0x10000000 0x10000"
        -ex "set memory-write-packet-size 16384"
        -ex "set remotewritesize 16384"
        -ex "load"
        -ex "set \$pc=\$ROM_START"
        -ex "set {int}0x4002200c = 2"
        -ex "set mem inaccessible-by-default on"
        -ex "set print pretty on"
        -ex "continue"
    )

    local rc=0
    if [ "${RUN_TIMEOUT}" = "0" ]; then
        "${gdb_cmd[@]}" || rc=$?
    else
        timeout "${RUN_TIMEOUT}s" "${gdb_cmd[@]}" || rc=$?
    fi

    if [ "${AGENT_RUN}" = "y" ]; then
        echo "I3C_AGENT:RUN_END:${test_name}:${rc}"
    fi

    return ${rc}
}

# Convenience: build, print launch snippet, and load/run (single-core).
# Usage: run_single_test <name> <src> <overlay> <conf>
run_single_test() {
    local test_name="$1"
    local src="$2"
    local overlay="$3"
    local conf="$4"

    local test_dir="${I3C_TEST_ROOT}/tests/${test_name}"
    local build="${test_dir}/build/target"
    local elf="${build}/zephyr/zephyr.elf"

    mkdir -p "${build}"

    build_single_test "${test_name}" "${src}" "${overlay}" "${conf}" "${build}"

    if [ "${SKIP_LAUNCH_PRINT}" != "y" ]; then
        print_launch_json_single "${test_name}" "${elf}"
    fi

    if [ "${SKIP_LOAD}" != "y" ]; then
        load_and_run_single "${test_name}" "${elf}"
    fi
}

# Convenience: build both images, print launch snippet, and load/run.
# Usage: run_test <name> <controller_src> <controller_overlay> <controller_conf>
#                       <target_src> <target_overlay> <target_conf>
run_test() {
    local test_name="$1"
    local controller_src="$2"
    local controller_overlay="$3"
    local controller_conf="$4"
    local target_src="$5"
    local target_overlay="$6"
    local target_conf="$7"

    local test_dir="${I3C_TEST_ROOT}/tests/${test_name}"
    local controller_build="${test_dir}/build/controller"
    local target_build="${test_dir}/build/target"
    local target_elf="${target_build}/zephyr/zephyr.elf"
    local controller_bin="${controller_build}/zephyr/zephyr.bin"

    mkdir -p "${controller_build}" "${target_build}"

    build_test "${test_name}" "${controller_src}" "${controller_overlay}" "${controller_conf}" "${controller_build}"
    build_test "${test_name}" "${target_src}" "${target_overlay}" "${target_conf}" "${target_build}"

    if [ "${SKIP_LAUNCH_PRINT}" != "y" ]; then
        print_launch_json "${test_name}" "${target_elf}" "${controller_bin}"
    fi

    if [ "${SKIP_LOAD}" != "y" ]; then
        load_and_run "${test_name}" "${target_elf}" "${controller_bin}"
    fi
}
