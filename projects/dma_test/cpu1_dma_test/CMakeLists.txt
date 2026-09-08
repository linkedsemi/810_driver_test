# SPDX-License-Identifier: Apache-2.0
# DMA M2M test - CMakeLists.txt

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_dma_test)

if(DMA_DESTRUCTIVE)
    target_sources(app PRIVATE
        src/main.c
        src/dma_destructive_test.c)
else()
    target_sources(app PRIVATE
        src/main.c
        src/dma_test.c)
endif()

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)
