# app_lwip_eth.cmake - 把 "MCAL Eth + lwIP" 组件接进一个 hpm-sdk 例程
#
# 用法（例程 CMakeLists.txt，顺序不能变）：
#   cmake_minimum_required(VERSION 3.13)
#   set(CONFIG_LWIP 1)                  # 必须在 find_package 之前：编 lwIP 中间件
#   set(CONFIG_LWIP_IPERF 1)            # 编 apps/lwiperf（iperf2 协议）
#   set(APP_LWIP_ETH_PORT "baremetal")  # 或 "freertos"（后者还要 set(CONFIG_FREERTOS 1)
#                                       #   与 set(CONFIG_LWIP_NETCONN_API 1)）
#   set(APP_USE_ENET_PORT_COUNT 1)      # 板级 CMake 据此开 ENET0 RGMII 时钟/引脚
#   find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
#   project(xxx)
#   include(${CMAKE_CURRENT_LIST_DIR}/../../../mcal/mcal.cmake)
#   mcal_add(Eth)
#   include(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_lwip_eth/app_lwip_eth.cmake)
#   sdk_app_src(src/main.c)
#
# 本文件负责：
#   1) lwIP 端口选择（baremetal：SDK samples/lwip/ports/baremetal 的 sys_arch；
#      freertos：components/freertos_lwip_port 的自写 sys_arch）；
#   2) 本组件三个源文件与头文件目录；
#   3) lwIP 选项开关（custom pbuf 零拷贝接收）。
# lwIP 内存/协议栈参数在各例程自己的 inc/lwipopts_app.h 里（例程级决策）。

if(NOT DEFINED APP_LWIP_ETH_PORT)
  set(APP_LWIP_ETH_PORT "baremetal")
endif()

set(APP_LWIP_ETH_DIR ${CMAKE_CURRENT_LIST_DIR})
set(_app_lwip_sdk_dir $ENV{HPM_SDK_BASE})
set(_app_lwip_lwip_dir ${_app_lwip_sdk_dir}/samples/lwip)

if(APP_LWIP_ETH_PORT STREQUAL "freertos")
  if(NOT CONFIG_FREERTOS)
    message(FATAL_ERROR "app_lwip_eth: freertos 端口需要在 find_package 之前 set(CONFIG_FREERTOS 1)")
  endif()
  include(${APP_LWIP_ETH_DIR}/../freertos_lwip_port/freertos_lwip_port.cmake)
  sdk_inc(${_app_lwip_lwip_dir}/ports/freertos ${_app_lwip_lwip_dir}/ports/freertos/arch)
elseif(APP_LWIP_ETH_PORT STREQUAL "baremetal")
  # 只借 SDK 的 baremetal lwipopts.h（NO_SYS=1），不编它的 sys_arch.c（依赖 samples/lwip/common/common.h）
  sdk_inc(${_app_lwip_lwip_dir}/ports/baremetal)
  sdk_app_src(${APP_LWIP_ETH_DIR}/src/app_lwip_eth_timebase.c)
else()
  message(FATAL_ERROR "app_lwip_eth: APP_LWIP_ETH_PORT 只能是 baremetal 或 freertos")
endif()

sdk_inc(${APP_LWIP_ETH_DIR}/inc)
sdk_app_src(
  ${APP_LWIP_ETH_DIR}/src/app_lwip_eth.c
  ${APP_LWIP_ETH_DIR}/src/app_lwip_eth_hw_mcal.c
  ${APP_LWIP_ETH_DIR}/src/app_lwip_eth_cli.c
  )

# USE_LWIPOPTS_APP_H：让 SDK 的 ports/<port>/lwipopts.h 包含例程 inc/lwipopts_app.h
sdk_compile_definitions(-DUSE_LWIPOPTS_APP_H=1)
# LWIP_SUPPORT_CUSTOM_PBUF：零拷贝接收（pbuf 直接指向 Eth Rx 缓冲）必需
sdk_compile_definitions(-DLWIP_SUPPORT_CUSTOM_PBUF=1)
# 本组件统计路径需要 lwIP 的 pbuf/netif 头，-O2 起（SDK 默认 -O0 太慢）
sdk_compile_options(-O2)
