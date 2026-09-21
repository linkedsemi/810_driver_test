# SPDX-License-Identifier: Apache-2.0
# mbox_test cpu1 (target) CMakeLists.txt

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_target_mbox_test)

target_sources(app PRIVATE
    src/main.c
    src/mbox_test_target.c)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)

# 结果上报 + 判定模块（§1.1 修复）：cpu1/cpu2 共用同一份源码
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tests/src/mbox_test_report.c)

# 扩展 case（P1c 追加）：MBOX_TEST_CASE_ID != 0 时才编入实现文件，
# 保证 case_mbox_basic（ID=0）的构建输入与合并前完全一致。
if(CONFIG_MBOX_TEST_CASE_ID)
    target_sources(app PRIVATE src/mbox_cases.c)
endif()
