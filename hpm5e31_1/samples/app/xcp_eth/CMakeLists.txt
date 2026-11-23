# xcp_eth —— XCP on UDP（XCPonEth）从站 + DAQ 测量（第 35 章例程 3）
# 网络底座：components/app_lwip（ENET RGMII + YT8011AN + 静态 IP 192.168.2.100 + 1 ms 时基）
# 协议：components/app_xcp（协议层）+ 本目录新写的 xcp_port_udp.c（传输层）
cmake_minimum_required(VERSION 3.13)

# 下面 3 个 set 必须在 find_package 之前（板级库在 find_package 内配置）
set(CONFIG_LWIP 1)
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_xcp_eth)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)

sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
# ENET TX CRC：0 = append（MAC 在帧尾追加 FCS，不回写发送缓冲）。
# replace 模式在连续大帧时会踩坏相邻 pbuf（第 36 章 U-36-10 实测），这里沿用 append。
sdk_compile_definitions(-DENET_TX_HW_CRC_MODE=0)

# XCPonEth 构型：单条 DTO 上限 256 B（CAN 构型仍是 8 B）；DAQ 资源相应放大。
# 这些宏只影响本目标的 app_xcp 编译，xcp_can_daq 不受影响。
sdk_compile_definitions(-DXCP_MAX_CTO=255 -DXCP_MAX_DTO=256)
sdk_compile_definitions(-DXCP_MAX_ODT_PER_LIST=8 -DXCP_MAX_ENTRY_PER_ODT=16)
sdk_compile_definitions(-DXCP_DAQ_TX_QUEUE_LEN=32)

sdk_inc(${REPO_ROOT}/components/app_xcp)
sdk_app_src(src/main.c
            ${REPO_ROOT}/components/app_xcp/xcp_core.c
            ${REPO_ROOT}/components/app_xcp/xcp_port_udp.c)
