# SPDX-License-Identifier: Apache-2.0
# perf_test cpu2 (peer): minimal mbox echo companion for the perf_mbox suite.
# The cpu2 console/memory overlays must be injected before find_package().

cmake_minimum_required(VERSION 3.20.0)

set(EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_console_uart5.overlay;
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_mem_layout.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu2_perf_peer)

target_sources(app PRIVATE src/main.c)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../otbn_test/cpu1_otbn_test/src)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)
