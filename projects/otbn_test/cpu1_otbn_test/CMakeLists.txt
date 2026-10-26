# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_otbn_test)

# RSA keygen suite merged from projects/otbn_keygen_test.  The sources stay in
# place (also referenced by integrated/sets/itest_crypto_otbn) so there is one
# copy of the test bodies; this project only adds them to its own build.
set(KEYGEN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../otbn_keygen_test/cpu1_otbn_keygen_test/src)
set(KEYGEN_EXP ${CMAKE_CURRENT_SOURCE_DIR}/../../otbn_keygen_test/tests/expected)

# 项目特定源文件
target_sources(app PRIVATE
    src/main.c
    src/otbn_test.c
    src/otbn_api_test.c
    src/mix_test.c
    ${KEYGEN_SRC}/otbn_keygen_test.c
    ${KEYGEN_SRC}/otbn_modexp_kat.c
    ${KEYGEN_SRC}/otbn_bench.c
    ${KEYGEN_SRC}/trial_div_bench.c)

# 项目特定include目录
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include
    ${KEYGEN_SRC}
    ${KEYGEN_EXP})

# The merged image pays for both suites.  The standalone keygen project keeps
# TD_CANDIDATES=1000 (128 KB); shrink it here as the integrated set does.
target_compile_definitions(app PRIVATE TD_CANDIDATES=100)

# 包含公共源文件和配置（test_results.c, ztest_glue.c, gpio_toggle.c）
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)
