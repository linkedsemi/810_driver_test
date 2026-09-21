# SPDX-License-Identifier: Apache-2.0
# eMMC block-layer test (sdhci1@40049000 + on-board eMMC) - CMakeLists.txt

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_emmc_block_test)

target_sources(app PRIVATE
    src/main.c
    src/emmc_block_test.c)

# 统一 CMake 入口：common/test_results.c + common/ztest_glue.c + gpio_toggle.c
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)
