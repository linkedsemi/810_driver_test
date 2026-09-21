# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# cpu2 侧必须注入（find_package 之前）：
#   cpu2_console_uart5.overlay  - 控制台 uart5（PN14/PN13, 115200）
#   cpu2_mem_layout.overlay     - 内存布局（sram_ro/sram 地址与大小，统一管理）
set(EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_console_uart5.overlay;
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_mem_layout.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu0_i2c_master)

# 项目特定源文件
target_sources(app PRIVATE
    src/main.c)

# 项目特定include目录
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

# 包含公共源文件和配置（test_results.c, ztest_glue.c, gpio_toggle.c）
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)

# RLOG 单出口（SPEC_REFERENCE §6.9.3）：本核是**主核**（唯一串口出口），收下
# 从核经 mbox 送来的 token 行并原样转发。只在本工程编入 —— 其余工程没有
# `rlog_channel` 设备树节点，编进去会因 DT_NODELABEL 取不到而构建失败。
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/rlog.c)
