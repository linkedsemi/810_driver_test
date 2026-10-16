# SPDX-License-Identifier: Apache-2.0
# cpu2 (controller) firmware CMakeLists.txt -- the delegation CLIENT.
# 注意：EXTRA_DTC_OVERLAY_FILE 必须在 find_package 之前

cmake_minimum_required(VERSION 3.20.0)

# cpu2 侧必须注入（find_package 之前）：
#   cpu2_console_uart5.overlay  - 控制台 uart5（PN14/PN13, 115200）
#   cpu2_mem_layout.overlay     - 内存布局（sram_ro/sram 地址与大小）
set(EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_console_uart5.overlay;
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cpu2_mem_layout.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu2_controller_otbn_delegation_test)

# 项目特定源文件
target_sources(app PRIVATE
    src/main.c)

# 测试体：cpu1/cpu2 共用同一份源码（tests/src/），差异全在 Kconfig（CLIENT/SERVER）。
# case_throughput（-DOTBN_DELEG_BENCH=1）换成基准源码：只跑 bench 一项，不与功能/负例并存。
if(OTBN_DELEG_BENCH)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/src/otbn_delegation_bench.c)
elseif(OTBN_DELEG_SOAK)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/src/otbn_delegation_soak.c)
elseif(OTBN_DELEG_RSA)
    # case_rsa_delegation (-DOTBN_DELEG_RSA=1): the RSA item set gets its OWN
    # translation unit, like bench/soak. It must not share the ECDSA body: the
    # ECDSA items are all linked, so its channel-masking item would otherwise run
    # before t01 (link order is by name, and 'r' < 't') and wedge the matrix.
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/src/otbn_delegation_rsa.c)
else()
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/src/otbn_delegation_test.c)
endif()

# 项目特定 include 目录（协议头）
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include)

# 包含公共源文件和配置（test_results.c, ztest_glue.c, gpio_toggle.c）
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)

# 用例可选测试面：case 的 test.sh 可传 -DOTBN_DELEG_NO_SERVER=1 建模"对端缺席"
# （见 tests/case_no_server/test.sh）。默认不设，构建输入与 case_default 一致。
if(OTBN_DELEG_NO_SERVER)
    target_compile_definitions(app PRIVATE OTBN_DELEG_NO_SERVER=1)
endif()
if(OTBN_DELEG_DEAD_CHANNEL)
    target_compile_definitions(app PRIVATE OTBN_DELEG_DEAD_CHANNEL=1)
endif()
if(OTBN_DELEG_BENCH)
    target_compile_definitions(app PRIVATE OTBN_DELEG_BENCH=1)
endif()
# case_throughput_iso_{proxy,local} (-DOTBN_DELEG_BENCH_HOLD=1): the "hold" side
# of an isolated throughput measurement. This core compiles the same bench TU but
# does not bench, so the peer measures the shared OTBN uncontended. The item name
# is kept identical to the active core's, as the dual-core matrix requires.
if(OTBN_DELEG_BENCH_HOLD)
    target_compile_definitions(app PRIVATE OTBN_DELEG_BENCH_HOLD=1)
endif()
# case_throughput_iso_local_hwrng (-DOTBN_DELEG_BENCH_HWRNG=1): the bench feeds
# genkey/sign from the hardware entropy device instead of the in-process LCG, so
# the local baseline matches what the delegation SERVER uses when serving a
# proxied genkey/sign (see tests/src/otbn_delegation_bench.c).
if(OTBN_DELEG_BENCH_HWRNG)
    target_compile_definitions(app PRIVATE OTBN_DELEG_BENCH_HWRNG=1)
endif()
# case_entropy_iso (-DOTBN_DELEG_BENCH_ENTROPY=1): the bench measures the two
# entropy-device fetch APIs (blocking vs ENTROPY_BUSYWAIT) instead of OTBN ops.
# Passed to BOTH cores; the HOLD side registers the same b02_entropy placeholder.
if(OTBN_DELEG_BENCH_ENTROPY)
    target_compile_definitions(app PRIVATE OTBN_DELEG_BENCH_ENTROPY=1)
endif()
# OTBN_DELEG_BENCH_TRNG1=1 (ad-hoc, trng1 timing): see the cpu1 CMakeLists. On this
# HOLD core the entropy bench expands to the hold placeholder, so the define is
# inert here; it is accepted only so the same EXTRA_CMAKE_ARGS can drive both
# cores without a per-core branch.
if(OTBN_DELEG_BENCH_TRNG1)
    target_compile_definitions(app PRIVATE OTBN_DELEG_BENCH_TRNG1=1)
endif()
if(OTBN_DELEG_SOAK)
    target_compile_definitions(app PRIVATE OTBN_DELEG_SOAK=1)
endif()
# case_explicit_api (-DOTBN_DELEG_EXPLICIT_API=1): the client calls the explicit
# mbedtls_ls_otbn_ecdsa_* entry points by name (see the redirection block in
# tests/src/otbn_delegation_test.c). Passed to BOTH cores: the server registers
# the equivalence item for name symmetry and SKIPs (its image has no explicit
# entry point), while its matrix keeps the mbedtls_* names.
if(OTBN_DELEG_EXPLICIT_API)
    target_compile_definitions(app PRIVATE OTBN_DELEG_EXPLICIT_API=1)
endif()
# case_alt_via_proxy (-DOTBN_DELEG_VIA_PROXY=1): the client is built with
# CONFIG_MBEDTLS_ECDSA_LINKEDSEMI_OTBN_ALT_VIA_DELEGATION=y, so its PLAIN
# mbedtls_ecdsa_* names ARE the proxy; the shared body calls them (see the
# redirection block in tests/src/otbn_delegation_test.c) and registers the t90
# "plain API is really mailbox-bound" item. Passed to BOTH cores: the server
# registers t90 for name symmetry and SKIPs.
if(OTBN_DELEG_VIA_PROXY)
    target_compile_definitions(app PRIVATE OTBN_DELEG_VIA_PROXY=1)
endif()
# case_rsa_delegation (-DOTBN_DELEG_RSA=1): the RSA item set lives in its own
# source file (tests/src/otbn_delegation_rsa.c, selected above). Passed to BOTH
# cores: the client drives the RSA proxy, the server drives its local OTBN RSA
# backend (enabled by case_rsa_delegation/target.conf).
if(OTBN_DELEG_RSA)
    target_compile_definitions(app PRIVATE OTBN_DELEG_RSA=1)
endif()
