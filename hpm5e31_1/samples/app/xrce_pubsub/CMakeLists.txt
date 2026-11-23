cmake_minimum_required(VERSION 3.13)

# ---- lwIP + YT8011AN（沿用 samples/enet/enet_lwip_iperf 的网络底座） ----
set(CONFIG_LWIP 1)
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

set(SDK_DIR $ENV{HPM_SDK_BASE})
set(LWIP_DIR ${SDK_DIR}/samples/lwip)
set(NET2_COMPONENT ${CMAKE_CURRENT_LIST_DIR}/../../../components/enet_phy_yt8011)
set(APP_XRCE_COMPONENT ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_xrce)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_xrce_pubsub)

# R5 U-36-3: -DXRCE_ADC_PERIOD_MS=2 编译 500 Hz 版（默认 10 ms/100 Hz）
if(DEFINED XRCE_ADC_PERIOD_MS)
  sdk_compile_definitions(-DADC_PERIOD_MS=${XRCE_ADC_PERIOD_MS}U)
endif()

sdk_compile_definitions(-D__DISABLE_AUTO_NEGO=0 -D__ENABLE_ENET_RECEIVE_INTERRUPT=0)
sdk_compile_definitions(-DLWIP_DHCP=0 -DUSE_LWIPOPTS_APP_H=1 -DLWIP_SUPPORT_CUSTOM_PBUF=1)
# ENET 硬件插校验和（与 12/13 章一致）；关掉 lwIP 软件校验和
sdk_compile_definitions(-DCHECKSUM_GEN_IP=0 -DCHECKSUM_GEN_TCP=0 -DCHECKSUM_GEN_UDP=0 -DCHECKSUM_GEN_ICMP=0)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
# 静态 IP：192.168.2.100/24，GW 192.168.2.1（netinfo.h 是 #ifndef 保护，可用 -D 注入）
sdk_compile_definitions(-DIP0_CONFIG=192.168.2.100 -DNETMASK0_CONFIG=255.255.255.0 -DGW0_CONFIG=192.168.2.1)
sdk_compile_options(-O2)

sdk_inc(${LWIP_DIR}/ports/baremetal)
sdk_inc(${LWIP_DIR}/ports/baremetal/arch)
sdk_inc(${LWIP_DIR}/common)
sdk_inc(${LWIP_DIR}/common/single)
sdk_inc(${LWIP_DIR}/common/utils)
sdk_inc(inc)
sdk_inc(${SDK_DIR}/components/enet_phy)
sdk_inc(${NET2_COMPONENT}/inc)

# ---- app_xrce 组件（Micro-XRCE-DDS Client + microcdr + lwIP 传输） ----
include(${APP_XRCE_COMPONENT}/app_xrce.cmake)
app_xrce_configure()

sdk_app_src(${LWIP_DIR}/ports/baremetal/arch/sys_arch.c)
sdk_app_src(${LWIP_DIR}/ports/baremetal/ethernetif.c)
sdk_app_src(${LWIP_DIR}/common/single/common.c)
sdk_app_src(${LWIP_DIR}/common/single/netconf.c)
sdk_app_src(${LWIP_DIR}/common/utils/utils.c)
sdk_app_src(${NET2_COMPONENT}/src/hpm_yt8011.c)
sdk_app_src(src/phy_glue.c)
sdk_app_src(src/main.c)
