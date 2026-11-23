cmake_minimum_required(VERSION 3.13)

# doip_server —— DoIP(ISO 13400-2) 服务端 + UDS(ISO 14229-1) 诊断（TCP/UDP 13400）
# 网络底座：components/app_lwip（ENET0 RGMII + YT8011AN + baremetal lwIP 2.1.2）
# 协议：components/app_doip（本任务新增）+ components/app_uds（第 31 章，复用）
set(CONFIG_LWIP 1)
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_doip_server)

sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_compile_options(-O2)

# 共享底座：lwIP 端口 + ENET/PHY + 静态 IP/时基（必须在 find_package 之后 include）
include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)

# ENET TX CRC 模式：0 = append（MAC 在帧尾追加 4 B FCS，不回写发送缓冲），
# 1 = replace（写回发送缓冲最后 4 B）。第 36 章实测 replace 会踩坏相邻 pbuf，
# 这里按第 36 章的结论用 append。
sdk_compile_definitions(-DENET_TX_HW_CRC_MODE=0)

# DoIP 组件
include(${REPO_ROOT}/components/app_doip/app_doip.cmake)

# UDS 组件（第 31 章，2026 已上板验证）
set(APP_UDS_DIR ${REPO_ROOT}/components/app_uds)
sdk_inc(${APP_UDS_DIR}/include)
sdk_app_src(${APP_UDS_DIR}/src/app_uds.c
            ${APP_UDS_DIR}/src/app_uds_dtc.c
            ${APP_UDS_DIR}/src/app_uds_flash.c)

sdk_app_src(src/main.c)
