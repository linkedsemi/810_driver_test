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
    ${COMMON_DIR}/gpio_toggle.c
)

# Common include directories
target_include_directories(app PRIVATE
    ${COMMON_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include
)
