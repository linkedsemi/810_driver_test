# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# ---------------------------------------------------------------------------
# pfr_tpm 模块注册：zephyr_pfr_tpm 已在 zephyr/west.yml 注册
# （path: modules/lib/zephyr_pfr_tpm），无需 EXTRA_ZEPHYR_MODULES。
# 旧版本这里写死 d:/zephyr4_0/modules/lib/pfr_tpm，WSL/新工作区会找不到。
# ---------------------------------------------------------------------------

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
