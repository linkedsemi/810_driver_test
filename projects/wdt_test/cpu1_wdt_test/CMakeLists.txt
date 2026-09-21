# SPDX-License-Identifier: Apache-2.0
# wdt_test CMakeLists.txt

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_wdt_test)

# 破坏性测试用不同源文件（test.sh 通过 -DWDT_DESTRUCTIVE_RESET 传递）
if(WDT_DESTRUCTIVE_RESET)
    target_sources(app PRIVATE
        src/main.c
        src/wdt_destructive_test.c)
    target_compile_definitions(app PRIVATE WDT_DESTRUCTIVE_RESET=1)
elseif(WDT_BOUNDARY_TEST)
    target_sources(app PRIVATE
        src/main.c
        src/wdt_boundary_test.c)
    target_compile_definitions(app PRIVATE WDT_BOUNDARY_TEST=1)
elseif(WDT_PMU_RESET_TEST)
    target_sources(app PRIVATE
        src/main.c
        src/wdt_pmu_reset_test.c)
    target_compile_definitions(app PRIVATE WDT_PMU_RESET_TEST=1)
else()
    target_sources(app PRIVATE
        src/main.c
        src/wdt_test.c)
endif()

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)




