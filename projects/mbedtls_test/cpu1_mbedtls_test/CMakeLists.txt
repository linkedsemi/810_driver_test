# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_mbedtls_test)

target_sources(app PRIVATE
    src/main.c
    src/mbedtls_test_common.c
    src/hash_test.c
    src/cipher_test.c
    src/ecc_test.c
    src/rsa_test.c
    src/pointops_test.c
    src/thread_test.c
    src/concurrency_test.c
    src/robustness_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/test_results.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/ztest_glue.c)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/expected
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common)
