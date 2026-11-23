cmake_minimum_required(VERSION 3.13)

# LAN8651 (10BASE-T1S MAC-PHY over TC6/SPI2) + lwIP 2.x + iperf2
set(CONFIG_HPM_SPI 1)
set(CONFIG_LWIP 1)
set(CONFIG_LWIP_IPERF 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_lan8651_lwip_iperf)

sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_compile_definitions(-DUSE_LWIPOPTS_APP_H=1 -DLWIP_DHCP=0)
sdk_compile_options(-O2)

sdk_inc(${CMAKE_CURRENT_LIST_DIR}/../../../components/oa_tc6)
sdk_inc(${CMAKE_CURRENT_LIST_DIR}/inc)

sdk_app_src(src/main.c
            src/oa_tc6_netif.c
            src/sys_arch.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/oa_tc6/oa_tc6.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/oa_tc6/lan865x.c)
