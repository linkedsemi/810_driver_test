# SPDX-License-Identifier: Apache-2.0
# QSPI flash test (qspi2@40040000 + on-board 32 MiB Puya NOR) - CMakeLists.txt

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_qspi_flash_test)

target_sources(app PRIVATE
    src/main.c
    src/qspi_flash_test.c)

# 统一 CMake 入口：common/test_results.c + common/ztest_glue.c + gpio_toggle.c
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)
