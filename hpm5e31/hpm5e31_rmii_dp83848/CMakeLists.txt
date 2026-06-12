# Copyright (c) 2025
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.13)

if("${HPM_BUILD_TYPE}" STREQUAL "")
    SET(HPM_BUILD_TYPE flash_xip)
endif()

set(CONFIG_LWIP 1)
# CONFIG_LWIP_IPERF intentionally NOT set: using local lwiperf_local.c with bug fixes
# (tcp_err use-after-free, udp_close stale timeout, pbuf_clone OOM, fragmented header)

set(CONFIG_ENET_PHY 1)
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RMII 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)

# Disable auto-negotiation: DP83848 uses auto-negotiation by default (0=enable)
sdk_compile_definitions(-D__DISABLE_AUTO_NEGO=0)
sdk_compile_definitions(-D__ENABLE_ENET_RECEIVE_INTERRUPT=0)
sdk_compile_definitions(-DLWIP_DHCP=0)
sdk_compile_definitions(-DUSE_LWIPOPTS_APP_H=1)
sdk_compile_definitions(-DLWIP_SUPPORT_CUSTOM_PBUF=1)

# Static IP: 192.168.0.100, GW: 192.168.0.1, Netmask: 255.255.255.0
sdk_compile_definitions(-DIP0_CONFIG=192.168.0.100)
sdk_compile_definitions(-DNETMASK0_CONFIG=255.255.255.0)
sdk_compile_definitions(-DGW0_CONFIG=192.168.0.1)
sdk_compile_definitions(-DREMOTE_IP0_CONFIG=192.168.0.2)

# UDP iperf client target rate: 90 Mbps for 100Mbps RMII link
sdk_compile_definitions(-DIPERF_UDP_CLIENT_RATE=\(90*1024*1024\))

project(hpm5e31_rmii_dp83848)

sdk_inc(../../../sdk_env_v1.11.0/hpm_sdk/samples/lwip/ports/baremetal/single)
sdk_inc(../../../sdk_env_v1.11.0/hpm_sdk/samples/lwip/ports/baremetal/single/arch)
sdk_inc(../../../sdk_env_v1.11.0/hpm_sdk/samples/lwip/common)
sdk_inc(../../../sdk_env_v1.11.0/hpm_sdk/samples/lwip/common/single)
sdk_inc(../../../sdk_env_v1.11.0/hpm_sdk/samples/lwip/common/utils)
sdk_inc(inc)

sdk_app_src(../../../sdk_env_v1.11.0/hpm_sdk/samples/lwip/ports/baremetal/single/arch/sys_arch.c)
# Local ethernetif with TX pbuf-chain fix (copies all segments into one DMA buffer)
sdk_app_src(src/ethernetif_local.c)
sdk_app_src(../../../sdk_env_v1.11.0/hpm_sdk/samples/lwip/common/single/common.c)
sdk_app_src(../../../sdk_env_v1.11.0/hpm_sdk/samples/lwip/common/single/netconf.c)
sdk_app_src(../../../sdk_env_v1.11.0/hpm_sdk/samples/lwip/common/utils/utils.c)
# Local lwiperf with all stability fixes
sdk_app_src(src/lwiperf_local.c)
sdk_app_src(src/lwip.c)

generate_ide_projects()
