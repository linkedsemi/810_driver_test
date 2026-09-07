# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_mbedtls_test)

# 项目特定源文件
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
    src/robustness_test.c)

# 项目特定include目录
# 注：../tests/expected 必须保留（protocol.h 引用的 *_vectors.h 向量本体在该目录）。
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/expected)

# 包含公共源文件和配置（test_results.c, ztest_glue.c, gpio_toggle.c）
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)
