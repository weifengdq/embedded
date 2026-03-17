# Copyright (c) 2021 HPMicro
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.13)

if("${HPM_BUILD_TYPE}" STREQUAL "")
	set(HPM_BUILD_TYPE flash_xip)
endif()

set(CONFIG_LWIP 1)
set(CONFIG_LWIP_IPERF 1)
set(CONFIG_HPM_SPI 1)
set(CONFIG_DMA_MGR 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)

project(hpm5361_lan8651)

sdk_inc(inc)
sdk_inc(../lwip/ports/baremetal/single)

sdk_app_src(src/main.c)
sdk_app_src(src/lan8651.c)
sdk_app_src(src/ethernetif_lan8651.c)

sdk_compile_definitions(-DUSE_LWIPOPTS_APP_H=1)
sdk_compile_definitions(-DLWIP_SUPPORT_CUSTOM_PBUF=0)

generate_ide_projects()
