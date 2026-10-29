# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# ---------------------------------------------------------------------------
# pfr_tpm 模块注册。
# ⚠️ 更正：zephyr/west.yml **并未**注册该模块（`west list` 无此项，
# `modules/lib/zephyr_pfr_tpm` 也不存在；真身是 modules/lib/pfr_tpm）。
# app.overlay 的 "lsqsh_pinctrl_pfr_tpm.dtsi" 必须靠这里就地注册，
# 否则 DT 预处理直接失败："fatal error: lsqsh_pinctrl_pfr_tpm.dtsi: No such file or directory"。
# 旧版本写死 d:/zephyr4_0/... 换工作区/WSL 会失效，故改为相对路径推算
# （CMAKE_CURRENT_SOURCE_DIR 上溯 4 级 = 工作区根）。必须在 find_package(Zephyr) 之前 set。
# ---------------------------------------------------------------------------
get_filename_component(PFR_TPM_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../modules/lib/pfr_tpm" ABSOLUTE)
set(EXTRA_ZEPHYR_MODULES ${PFR_TPM_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_spif_test)

# 项目特定源文件
target_sources(app PRIVATE
    src/main.c
    src/spif_test.c
    src/spif_loopback.c)

# 项目特定 include 目录
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

# 包含公共源文件和配置（test_results.c, ztest_glue.c, gpio_toggle.c）
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)
