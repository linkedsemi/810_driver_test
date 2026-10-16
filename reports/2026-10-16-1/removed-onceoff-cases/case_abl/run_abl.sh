#!/usr/bin/env bash
# case_abl 消融驱动：逐 conf 构建 + 采集 + 快照 .config + 记指标。
# 用法: bash run_abl.sh <conf1> [<conf2> ...]
# 输出:
#   reports/2026-10-15-4/raw/build_<conf>.log
#   reports/2026-10-15-4/raw/run_<conf>.log
#   reports/2026-10-15-4/raw/config_<conf>.config      (该组 .config 快照)
#   reports/2026-10-15-4/raw/elfmd5_<conf>.txt         (ELF md5)
#   reports/2026-10-15-4/raw/abl_summary.txt           (累计指标表)
set -u
ABL_DIR="d:/zephyr4_0/810_driver_test/projects/i2cf_test/tests/case_abl"
TESTS_DIR="d:/zephyr4_0/810_driver_test/projects/i2cf_test/tests"
REPORT="d:/zephyr4_0/810_driver_test/reports/2026-10-15-4/raw"
BUILD="d:/zephyr4_0/810_driver_test/projects/i2cf_test/tests/case_abl/build/target"

for conf in "$@"; do
    echo "=========== $conf : BUILD ==========="
    bash "${ABL_DIR}/test.sh" "$conf" > "${REPORT}/build_${conf%.conf}.log" 2>&1
    rc=$?
    echo "[build] $conf rc=$rc"
    cp "${BUILD}/zephyr/.config" "${REPORT}/config_${conf%.conf}.config"
    md5sum "${BUILD}/zephyr/zephyr.elf" > "${REPORT}/elfmd5_${conf%.conf}.txt"
    if [ $rc -ne 0 ]; then
        echo "[build] $conf FAILED — skip run" | tee -a "${REPORT}/abl_summary.txt"
        continue
    fi
    echo "=========== $conf : RUN ==========="
    ( cd "${TESTS_DIR}" && python scripts/dbg_run.py --case case_abl --timeout 45 --raw --tgt-uart COM16 ) \
        > "${REPORT}/run_${conf%.conf}.log" 2>&1
    echo "[run] $conf rc=$?"
    python "${ABL_DIR}/parse_abl.py" "${REPORT}/run_${conf%.conf}.log" | tee -a "${REPORT}/abl_summary.txt"
done
echo "ALL DONE"
