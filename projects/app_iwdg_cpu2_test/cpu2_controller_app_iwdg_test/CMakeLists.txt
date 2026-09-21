# SPDX-License-Identifier: Apache-2.0
# app_iwdg_cpu2_test - cpu2 (controller) side CMakeLists.txt
#
# 注意：EXTRA_DTC_OVERLAY_FILE 必须在 find_package 之前。

cmake_minimum_required(VERSION 3.20.0)

# cpu2 侧必须注入（find_package 之前）：
#   cpu2_console_uart5.overlay  - 控制台 uart5（PN14/PN13, 115200）
#   cpu2_mem_layout.overlay     - 内存布局（sram_ro/sram 地址与大小）
set(EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_console_uart5.overlay;
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_mem_layout.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu2_controller_app_iwdg_test)

target_sources(app PRIVATE
    src/main.c)

# case 变体（见本目录 Kconfig）：只编入被选中的那份用例实现，
# 使各 case 的构建输入互不重叠。
if(CONFIG_APP_IWDG_TEST_CASE_ID EQUAL 1)
    target_sources(app PRIVATE src/app_iwdg_quick_test.c)
elseif(CONFIG_APP_IWDG_TEST_CASE_ID EQUAL 2)
    target_sources(app PRIVATE src/app_iwdg_quick_invalid_test.c)
elseif(CONFIG_APP_IWDG_TEST_CASE_ID EQUAL 3)
    target_sources(app PRIVATE src/app_iwdg_reset_test.c)
else()
    target_sources(app PRIVATE src/app_iwdg_test.c)
endif()

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)

# RLOG 单出口（SPEC_REFERENCE §6.9.3）：主核收从核 token 行并加核信封转发。
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/rlog.c)

# ztest 单出口的长行预算（common/rlog.h::RLOG_TEXT_MAX 说明）：逐项 D1 行
# 可长到 ~56B（测试名 + " in X.XXX seconds"），默认 54B 会把结尾的
# 'seconds' 截掉 ⇒ 逐项证据被静默吞掉。两核取同一值（帧是双方 ABI），
# 且 overlay 的通道 width 必须 ≥ RLOG_TEXT_MAX+9（此处 118+9=127 ≤ 128）。
target_compile_definitions(app PRIVATE RLOG_TEXT_MAX=118)

