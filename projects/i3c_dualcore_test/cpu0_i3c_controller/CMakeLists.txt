# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# 810_driver_test 通用配置（cpu2 侧所有工程必须注入）：
#   cpu2_console_uart5.overlay  - 控制台 uart5（PN14/PN13, 115200）
#   cpu2_mem_layout.overlay     - 内存布局（sram_ro/sram 地址与大小，统一管理）
set(EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_console_uart5.overlay;
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_mem_layout.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu0_i3c_controller)

target_sources(app PRIVATE
    src/main.c
    src/i3c_controller_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/test_results.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/ztest_glue.c)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common)
