# app_lwip.cmake - 把应用层 lwIP 组件接入一个 hpm-sdk 例程
#
# 用法（注意顺序！）：
#   cmake_minimum_required(VERSION 3.13)
#   set(CONFIG_LWIP 1)                 # 让 SDK 编 lwIP 中间件
#   set(APP_USE_ENET_PORT_COUNT 1)     # 单网口
#   set(APP_USE_ENET_ITF_RGMII 1)      # 板级 CMake 据此开 ENET0 RGMII 引脚/时钟
#   find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
#   project(xxx)
#   include(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_lwip/app_lwip.cmake)   # 必须在 find_package 之后
#   sdk_app_src(src/main.c)
#
# 上面 3 个 set() 必须在 find_package 之前，因为板级库在 find_package 内部配置；
# 本文件里的 sdk_inc/sdk_app_src 必须在 find_package 之后，因为宏由 SDK 定义。

set(APP_LWIP_COMPONENT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(_app_lwip_sdk_dir $ENV{HPM_SDK_BASE})
set(_app_lwip_lwip_dir ${_app_lwip_sdk_dir}/samples/lwip)
set(_app_lwip_yt8011_dir ${APP_LWIP_COMPONENT_DIR}/../enet_phy_yt8011)

sdk_compile_definitions(-D__DISABLE_AUTO_NEGO=0 -D__ENABLE_ENET_RECEIVE_INTERRUPT=0)
sdk_compile_definitions(-DLWIP_DHCP=0 -DUSE_LWIPOPTS_APP_H=1 -DLWIP_SUPPORT_CUSTOM_PBUF=1)
# IP/TCP/UDP/ICMP 校验和交给 ENET 硬件（第 13 章实测路径）
sdk_compile_definitions(-DCHECKSUM_GEN_IP=0 -DCHECKSUM_GEN_TCP=0 -DCHECKSUM_GEN_UDP=0 -DCHECKSUM_GEN_ICMP=0)
# 静态 IP 默认值（运行期 app_lwip_cfg_t 仍可覆盖）
sdk_compile_definitions(-DIP0_CONFIG=192.168.2.100 -DNETMASK0_CONFIG=255.255.255.0 -DGW0_CONFIG=192.168.2.1)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_compile_options(-O3)

# 组件自己的 lwipopts_app.h / common_cfg.h 必须排在最前
sdk_inc(${APP_LWIP_COMPONENT_DIR}/inc)
sdk_inc(${_app_lwip_lwip_dir}/ports/baremetal)
sdk_inc(${_app_lwip_lwip_dir}/ports/baremetal/arch)
sdk_inc(${_app_lwip_lwip_dir}/common)
sdk_inc(${_app_lwip_lwip_dir}/common/single)
sdk_inc(${_app_lwip_lwip_dir}/common/utils)
sdk_inc(${_app_lwip_sdk_dir}/components/enet_phy)
sdk_inc(${_app_lwip_yt8011_dir}/inc)

sdk_app_src(${_app_lwip_lwip_dir}/ports/baremetal/arch/sys_arch.c)
sdk_app_src(${_app_lwip_lwip_dir}/ports/baremetal/ethernetif.c)
sdk_app_src(${_app_lwip_lwip_dir}/common/single/common.c)
sdk_app_src(${_app_lwip_lwip_dir}/common/single/netconf.c)
sdk_app_src(${_app_lwip_lwip_dir}/common/utils/utils.c)
sdk_app_src(${_app_lwip_yt8011_dir}/src/hpm_yt8011.c)
sdk_app_src(${APP_LWIP_COMPONENT_DIR}/src/app_lwip.c)
