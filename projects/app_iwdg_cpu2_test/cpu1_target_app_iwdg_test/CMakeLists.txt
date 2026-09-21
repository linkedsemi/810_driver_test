# SPDX-License-Identifier: Apache-2.0
# app_iwdg_cpu2_test - cpu1 (target) side CMakeLists.txt
#
# 双核工程（SPEC_REFERENCE §6.1.10）：本目录存在即构成"有 cpu1 之外的固件
# 目录"中的 cpu1 侧；另一侧为 cpu2_controller_app_iwdg_test。

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_target_app_iwdg_test)

# ztest 单出口覆盖点（SPEC_REFERENCE §6.9.3/§6.11.5）：本核为从核，把 ztest 的
# `PRINT_DATA` 指向 RLOG。覆盖头必须对**所有** TU 可见（ztest 库自己也用
# PRINT_DATA）⇒ 用 zephyr_include_directories 挂到 zephyr_interface。
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(app PRIVATE
    src/main.c
    src/app_iwdg_domain_test.c)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)

# RLOG 单出口（SPEC_REFERENCE §6.9.3）：只在本工程编入 —— 其余工程没有
# `rlog_channel` 设备树节点，编进去会因 DT_NODELABEL 取不到而构建失败。
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/rlog.c)

# ztest 单出口的长行预算（common/rlog.h::RLOG_TEXT_MAX 说明）：逐项 D1 行
# 可长到 ~56B（测试名 + " in X.XXX seconds"），默认 54B 会把结尾的
# 'seconds' 截掉 ⇒ 逐项证据被静默吞掉。两核取同一值（帧是双方 ABI），
# 且 overlay 的通道 width 必须 ≥ RLOG_TEXT_MAX+9（此处 118+9=127 ≤ 128）。
target_compile_definitions(app PRIVATE RLOG_TEXT_MAX=118)

