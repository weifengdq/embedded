# xcp_can_daq —— XCP on CAN 从站 + DAQ 测量（第 34 章例程 1）
# Bench B：板端 CAN0 = XCP 从站，CAN1 = cannelloni 网关（gw on 打开），主机 vcan0 上用 pyxcp
cmake_minimum_required(VERSION 3.13)

# 下面 3 个 set 必须在 find_package 之前（板级库在 find_package 内配置）
set(CONFIG_LWIP 1)
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_xcp_can_daq)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)
include(${REPO_ROOT}/components/app_can/app_can.cmake)
include(${REPO_ROOT}/components/app_can_gw/app_can_gw.cmake)

sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_inc(${REPO_ROOT}/components/app_xcp)
sdk_app_src(src/main.c
            src/xcp_test_master.c
            ${REPO_ROOT}/components/app_xcp/xcp_core.c
            ${REPO_ROOT}/components/app_xcp/xcp_port_can.c)
