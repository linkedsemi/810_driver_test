# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# ---------------------------------------------------------------------------
# pfr_tpm 模块注册（本工程私有，不得移入 common/）
#
# modules/lib/pfr_tpm 未登记进 zephyr/west.yml，compatible `linkedsemi,spid`
# 默认解析不到（DT_HAS_LINKEDSEMI_SPID_ENABLED 不成立 → 驱动不编入、节点无 binding）。
# 用 EXTRA_ZEPHYR_MODULES 就地注册；必须在 find_package(Zephyr) 之前 set ——
# zephyr/cmake/modules/zephyr_module.cmake:39 的 zephyr_get() 在此刻读该变量。
#
# ⚠️ 只写在本工程；写进 common/common_sources.cmake 会影响全部既有工程（用户明令禁止）。
# ---------------------------------------------------------------------------
get_filename_component(PFR_TPM_WS "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
if(EXISTS "${PFR_TPM_WS}/modules/lib/zephyr_pfr_tpm/zephyr/module.yml")
  set(EXTRA_ZEPHYR_MODULES "${PFR_TPM_WS}/modules/lib/zephyr_pfr_tpm")
elseif(EXISTS "${PFR_TPM_WS}/modules/lib/pfr_tpm/zephyr/module.yml")
  set(EXTRA_ZEPHYR_MODULES "${PFR_TPM_WS}/modules/lib/pfr_tpm")
else()
  message(FATAL_ERROR "pfr_tpm module not found under ${PFR_TPM_WS}/modules/lib")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_spid_test)

# 项目特定源文件
target_sources(app PRIVATE
    src/main.c
    src/spid_test.c)

# 项目特定 include 目录
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

# 包含公共源文件和配置（test_results.c, ztest_glue.c, gpio_toggle.c）
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)
