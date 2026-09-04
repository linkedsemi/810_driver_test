# Common sources include file for 810_driver_test projects
#
# Usage in your CMakeLists.txt:
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)
#
# This will automatically add all common source files to the app target.

# Calculate common directory path
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../common)

# Common source files
target_sources(app PRIVATE
    ${COMMON_DIR}/test_results.c
    ${COMMON_DIR}/ztest_glue.c
)

# GPIO toggle（PA05/PA07 翻转线程）仅 cpu1 使用：
# AMP 下 GPIOA 为双核共享外设，cpu2 编入会并发驱动同一物理引脚，
# 故 cpu2 侧固件不编入 gpio_toggle.c。单核工程（均跑 cpu1）不受影响。
if(NOT "${BOARD_QUALIFIERS}" MATCHES "cpu2")
    target_sources(app PRIVATE ${COMMON_DIR}/gpio_toggle.c)
endif()

# Common include directories
target_include_directories(app PRIVATE
    ${COMMON_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include
)
