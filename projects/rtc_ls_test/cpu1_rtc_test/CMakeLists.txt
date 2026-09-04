# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_rtc_test)

# 项目特定源文件
target_sources(app PRIVATE
    src/main.c
    src/rtc_test.c)

# 项目特定include目录
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

# 包含公共源文件（仅test_results.c和gpio_toggle.c，rtc_test不使用ztest_glue）
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../common)
target_sources(app PRIVATE
    ${COMMON_DIR}/test_results.c
    ${COMMON_DIR}/gpio_toggle.c)
target_include_directories(app PRIVATE ${COMMON_DIR})

# 选择性运行测试：1=编译进去跑，0=编译期裁掉。
# 日历边界专项：仅 RTC_EN_CALENDAR_ROLLOVER=1，其余 0。
target_compile_definitions(app PRIVATE
    RTC_EN_SET_GET_ROUNDTRIP=1
    RTC_EN_TIME_ADVANCES=1
    RTC_EN_INVALID_SET_ARGS=1
    RTC_EN_ALARM_SET_GET=1
    RTC_EN_ALARM_FIRES=1
    RTC_EN_ALARM_INVALID_ARGS=1
    RTC_EN_CALIBRATION_ROUNDTRIP=1
    RTC_EN_CALIBRATION_NEGATIVE=1
    RTC_EN_CALIBRATION_NULL_ARG=1
    RTC_EN_CALENDAR_ROLLOVER=1
)
