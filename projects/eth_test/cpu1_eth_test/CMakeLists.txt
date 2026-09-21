# SPDX-License-Identifier: Apache-2.0
# Ethernet test (eth1@40064000 + 板载 RTL8211F PHY) - CMakeLists.txt
#
# 单核（cpu1）工程，四个 case 变体共享同一源目录，由 test.sh 用 -D 选择源集：
#   -DETH_ARP_ICMP_TEST=y -> src/eth_arp_icmp_test.c （B 组：数据面 ARP/ICMP）
#   -DETH_UDP_ECHO_TEST=y -> src/eth_udp_echo_test.c  （B 组：数据面 UDP echo）
#   -DETH_LINK_TEST=y     -> src/eth_link_test.c      （B 组：MDIO 之上，链路状态）
#   默认                  -> src/eth_mdio_test.c      （A 组：零对端，不依赖网线状态）
# 变体机制与 wdt_test 一致（SPEC_REFERENCE §6.1.8）。

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu1_eth_test)

if(ETH_ARP_ICMP_TEST)
    target_sources(app PRIVATE
        src/main.c
        src/eth_arp_icmp_test.c)
    target_compile_definitions(app PRIVATE ETH_ARP_ICMP_TEST=1)
elseif(ETH_UDP_ECHO_TEST)
    target_sources(app PRIVATE
        src/main.c
        src/eth_udp_echo_test.c)
    target_compile_definitions(app PRIVATE ETH_UDP_ECHO_TEST=1)
elseif(ETH_LINK_TEST)
    target_sources(app PRIVATE
        src/main.c
        src/eth_link_test.c)
    target_compile_definitions(app PRIVATE ETH_LINK_TEST=1)
else()
    target_sources(app PRIVATE
        src/main.c
        src/eth_mdio_test.c)
    target_compile_definitions(app PRIVATE ETH_MDIO_TEST=1)
endif()

# 统一 CMake 入口（common_sources.cmake）：注入公共源
# common/test_results.c + common/ztest_glue.c + common/gpio_toggle.c
# 及 include 目录。禁止在此单独 target_sources 这三个文件（SPEC_REFERENCE §6.1.9）。
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/common_sources.cmake)
