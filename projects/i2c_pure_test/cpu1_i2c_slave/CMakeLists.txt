# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_i2c_slave)

# ztest 单出口覆盖点（SPEC_REFERENCE §6.9.3/§6.11.5）：本核为从核，把 ztest 的
# `PRINT_DATA` 指向 RLOG。覆盖头必须对**所有** TU 可见（ztest 库自己也用
# PRINT_DATA）⇒ 用 zephyr_include_directories 挂到 zephyr_interface。
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# 项目特定源文件
target_sources(app PRIVATE
    src/main.c)

# 项目特定include目录
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

# 包含公共源文件和配置（test_results.c, ztest_glue.c, gpio_toggle.c）
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)

# RLOG 单出口（SPEC_REFERENCE §6.9.3）：只在本工程编入 —— 其余工程没有
# `rlog_channel` 设备树节点，编进去会因 DT_NODELABEL 取不到而构建失败。
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/rlog.c)
