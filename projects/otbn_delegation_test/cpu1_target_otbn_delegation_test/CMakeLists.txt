# SPDX-License-Identifier: Apache-2.0
# cpu1 (target) firmware CMakeLists.txt -- the delegation SERVER.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_target_otbn_delegation_test)

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
    # case_rsa_delegation (-DOTBN_DELEG_RSA=1): the RSA item set gets its own
    # translation unit (see the cpu2 CMakeLists for why it must not share the
    # ECDSA body).
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/src/otbn_delegation_rsa.c)
elseif(OTBN_DELEG_SLOT)
    # case_slot (-DOTBN_DELEG_SLOT=1): private-key slot item set (own TU).
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/src/otbn_delegation_slot.c)
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
# OTBN_DELEG_BENCH_TRNG1=1 (ad-hoc, trng1 timing): the entropy bench additionally
# times trng1 (ls-dwtrng, DT_NODELABEL(trng1)) through the same three fetch APIs,
# in the same run as trng0. Requires the trng1 node to be enabled by an overlay
# (injected via EXTRA_DTC_OVERLAY_FILE); without it DT_NODELABEL(trng1) has no
# ready device and the bench's device_is_ready assert fires. Additive only.
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
# case_alt_via_proxy (-DOTBN_DELEG_VIA_PROXY=1): see the cpu2 CMakeLists. Passed to
# BOTH cores; on the server it only registers the t90 item for name symmetry
# (t90 SKIPs there -- no delegation forwarder on this image).
if(OTBN_DELEG_VIA_PROXY)
    target_compile_definitions(app PRIVATE OTBN_DELEG_VIA_PROXY=1)
endif()
# case_rsa_delegation (-DOTBN_DELEG_RSA=1): see the cpu2 CMakeLists. Passed to
# BOTH cores; the server runs r01/r02/r03 against its local OTBN RSA backend
# (case_rsa_delegation/target.conf) and SKIPs r90 (no delegation client here).
if(OTBN_DELEG_RSA)
    target_compile_definitions(app PRIVATE OTBN_DELEG_RSA=1)
endif()
# case_slot (-DOTBN_DELEG_SLOT=1): selects the slot TU and enables the slot 0
# registration in test_main. Passed to BOTH cores.
if(OTBN_DELEG_SLOT)
    target_compile_definitions(app PRIVATE OTBN_DELEG_SLOT=1)
endif()
