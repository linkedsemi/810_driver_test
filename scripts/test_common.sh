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
CONTROLLER_LOAD_ADDR="${CONTROLLER_LOAD_ADDR:-0x10000000}"
TARGET_ROM_START="${TARGET_ROM_START:-0x100c0000}"

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
        *controller*)
            echo "${CONTROLLER_BOARD}"
            ;;
        *target*)
            echo "${TARGET_BOARD}"
            ;;
        *)
            echo "ERROR: cannot detect board for ${src_dir}" >&2
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

    local board
    board="$(_detect_board "${src_dir}")" || return 1

    local extra_args=()
    if [ -n "${overlay}" ]; then
        extra_args+=("-DDTC_OVERLAY_FILE=${overlay}")
    fi
    if [ -n "${conf}" ]; then
        local base_conf="${src_dir}/prj.conf"
        if [ -f "${base_conf}" ]; then
            base_conf="$(win_path "$(realpath "${base_conf}")")"
            conf="$(win_path "${conf}")"
            extra_args+=("-DCONF_FILE=${base_conf};${conf}")
        else
            extra_args+=("-DCONF_FILE=${conf}")
        fi
    fi

    echo "[${test_name}] Build $(basename "${src_dir}") -> ${build_dir}"
    west build -p auto \
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

    echo "[${test_name}] Loading images into RAM via GDB..."
    echo "    target ELF:   ${target_elf}"
    echo "    controller BIN: ${controller_bin}"

    if [ "${AGENT_RUN}" = "y" ]; then
        echo "I3C_AGENT:RUN_START:${test_name}"
    fi

    local gdb_cmd=(
        riscv64-unknown-elf-gdb
        -ex "target remote ${GDB_SERVER_HOST}:${GDB_SERVER_PORT}"
        -ex "set output-radix 16"
        -ex "set confirm off"
        -ex "set pagination off"
        -ex "set \$ROM_START = ${TARGET_ROM_START}"
        -ex "file ${target_elf}"
        -ex "reset"
        -ex "load"
        -ex "restore ${controller_bin} binary ${CONTROLLER_LOAD_ADDR}"
        -ex "set \$pc=\$ROM_START"
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

    local extra_args=()
    if [ -n "${overlay}" ]; then
        extra_args+=("-DDTC_OVERLAY_FILE=${overlay}")
    fi
    if [ -n "${conf}" ]; then
        local base_conf="${src_dir}/prj.conf"
        if [ -f "${base_conf}" ]; then
            base_conf="$(win_path "$(realpath "${base_conf}")")"
            conf="$(win_path "${conf}")"
            extra_args+=("-DCONF_FILE=${base_conf};${conf}")
        else
            extra_args+=("-DCONF_FILE=${conf}")
        fi
    fi

    echo "[${test_name}] Build $(basename "${src_dir}") -> ${build_dir}"
    west build -p auto \
        -d "${build_dir}" \
        -b "${TARGET_BOARD}" \
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
        "set \$pc=${TARGET_ROM_START}",
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

    if [ "${AGENT_RUN}" = "y" ]; then
        echo "I3C_AGENT:RUN_START:${test_name}"
    fi

    local gdb_cmd=(
        riscv64-unknown-elf-gdb
        -ex "target remote ${GDB_SERVER_HOST}:${GDB_SERVER_PORT}"
        -ex "set output-radix 16"
        -ex "set confirm off"
        -ex "set pagination off"
        -ex "set \$ROM_START = ${TARGET_ROM_START}"
        -ex "file ${elf}"
        -ex "reset"
        -ex "load"
        -ex "set \$pc=\$ROM_START"
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
