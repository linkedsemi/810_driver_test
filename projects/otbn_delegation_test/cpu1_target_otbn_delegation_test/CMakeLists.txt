# SPDX-License-Identifier: Apache-2.0
# cpu1 (target) firmware CMakeLists.txt -- the delegation SERVER.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_target_otbn_delegation_test)

# 项目特定源文件
target_sources(app PRIVATE
    src/main.c)

# 测试体：cpu1/cpu2 共用同一份源码（tests/src/），差异全在 Kconfig（CLIENT/SERVER）。
# case_throughput（-DOTBN_DELEG_BENCH=1）换成基准源码：只跑 bench 一项，不与功能/负例并存。
if(OTBN_DELEG_BENCH)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/src/otbn_delegation_bench.c)
elseif(OTBN_DELEG_SOAK)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/src/otbn_delegation_soak.c)
else()
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/src/otbn_delegation_test.c)
endif()

# 项目特定 include 目录（协议头）
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

# 包含公共源文件和配置（test_results.c, ztest_glue.c, gpio_toggle.c）
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)

# 用例可选测试面：case 的 test.sh 可传 -DOTBN_DELEG_NO_SERVER=1 建模"对端缺席"
# （见 tests/case_no_server/test.sh）。默认不设，构建输入与 case_default 一致。
if(OTBN_DELEG_NO_SERVER)
    target_compile_definitions(app PRIVATE OTBN_DELEG_NO_SERVER=1)
endif()
if(OTBN_DELEG_DEAD_CHANNEL)
    target_compile_definitions(app PRIVATE OTBN_DELEG_DEAD_CHANNEL=1)
endif()
if(OTBN_DELEG_BENCH)
    target_compile_definitions(app PRIVATE OTBN_DELEG_BENCH=1)
endif()
if(OTBN_DELEG_SOAK)
    target_compile_definitions(app PRIVATE OTBN_DELEG_SOAK=1)
endif()
