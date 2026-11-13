# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# ---------------------------------------------------------------------------
# pfr_tpm 模块注册（本工程私有，不得移入 common/）
#
# modules/lib/pfr_tpm 未登记进 zephyr/west.yml（实测 grep 计数 0），因此三个
# compatible（linkedsemi,i2c-filter / linkedsemi,spi-filter / linkedsemi,spid）
# 默认解析不到，DT_HAS_LINKEDSEMI_*_ENABLED 不成立 → 驱动不编入、DT 节点无 binding。
#
# 用 EXTRA_ZEPHYR_MODULES 就地注册（等价于 manifest 条目，零上游改动）：
# 模块自带 zephyr/module.yml 的 build.cmake / kconfig / settings.dts_root 全部生效，
# 含 dts binding 与 include 目录。必须在 find_package(Zephyr) 之前 set ——
# zephyr/cmake/modules/zephyr_module.cmake:39 的 zephyr_get() 在此刻读该变量。
#
# ⚠️ 只写在本工程。写进 common/common_sources.cmake 会给全部既有工程引入新的
# Kconfig 符号与 DT binding，可能影响之前已通过的测试（用户明令禁止）。
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
project(cpu1_i2cf_test)

# 项目特定源文件
target_sources(app PRIVATE
    src/main.c
    src/i2cf_test.c)

# 项目特定 include 目录
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

# 包含公共源文件和配置（test_results.c, ztest_glue.c, gpio_toggle.c）
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)

# 枚举矩阵回归用例（case_enum_matrix）：仅 CONFIG_I2CF_ENUM_MATRIX=y 时编入。
# 默认 n ⇒ 不置位时本文件不存在于构建中，case_default 逐字节不变。
if(CONFIG_I2CF_ENUM_MATRIX)
    target_sources(app PRIVATE src/i2cf_enum.c)
endif()

# 一次性诊断 shell 命令（log_flood）：仅 CONFIG_I2CF_LOG_FLOOD=y 时编入。
# 默认 n ⇒ 不置位时本文件不存在于构建中，既有 case 不受影响。
if(CONFIG_I2CF_LOG_FLOOD)
    target_sources(app PRIVATE src/log_flood.c)
endif()
