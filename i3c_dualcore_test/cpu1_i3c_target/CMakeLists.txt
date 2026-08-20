# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_i3c_target)

target_sources(app PRIVATE src/main.c src/i3c_target_test.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)
