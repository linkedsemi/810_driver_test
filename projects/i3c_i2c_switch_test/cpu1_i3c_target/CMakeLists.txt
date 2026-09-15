# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_i3c_target)

# 项目特定源文件
target_sources(app PRIVATE
    src/main.c
    src/i3c_i2c_target_test.c)

# 禁用 cpu1 侧 PA05 toggle 线程（cpu2 用 PA05 做 read 探针标记）
target_compile_definitions(app PRIVATE GPIO_TOGGLE_DISABLE_PA05=1)

# 项目特定include目录
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

# 包含公共源文件和配置（test_results.c, ztest_glue.c, gpio_toggle.c）
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)
