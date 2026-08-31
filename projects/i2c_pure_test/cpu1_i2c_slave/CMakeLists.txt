# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_i2c_slave)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/test_results.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/ztest_glue.c)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common)
