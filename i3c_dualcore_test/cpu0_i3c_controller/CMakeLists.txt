# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# 810_driver_test 通用：cpu2 控制台 uart5（PN14/PN13, 115200）
set(EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/cpu2_console_uart5.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu0_i3c_controller)

target_sources(app PRIVATE src/main.c src/i3c_controller_test.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)
