# SPDX-License-Identifier: Apache-2.0
# perf_test cpu1: throughput suites for TRNG, crypto, DMA, flash/qspi and mbox.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_perf_test)

target_sources(app PRIVATE
    src/main.c
    src/perf_buf.c
    src/perf_flash_lock.c
    src/perf_trng.c
    src/perf_crypto.c
    src/perf_dma.c
    src/perf_flash.c
    src/perf_mbox.c
    src/perf_proxy.c
    src/perf_telemetry.c)

set(COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../../../common)

# Deliberately do not include common_sources.cmake: it adds gpio_toggle.c,
# whose startup LOG_INF lines interleave with the PERF output.  The perf
# project needs contiguous measurement lines, so only the result/glue modules
# are pulled in.
target_sources(app PRIVATE
    ${COMMON}/test_results.c
    ${COMMON}/ztest_glue.c)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${COMMON}
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)
