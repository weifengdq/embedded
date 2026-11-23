cmake_minimum_required(VERSION 3.13)

# someip_event —— SOME/IP over UDP（SOME/IP-SD + 事件订阅）
# 网络底座：components/app_lwip（tp-infra 共享组件，ENET RGMII + YT8011AN + 静态 IP + 1 ms 时基）
# 协议：components/app_someip
set(CONFIG_LWIP 1)
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(SOMEIP_COMPONENT ${REPO_ROOT}/components/app_someip)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_someip_event)

# 共享底座：lwIP 端口 + ENET/PHY + 静态 IP/时基（必须在 find_package 之后 include）
include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)

# ENET TX CRC 模式：0 = append（MAC 在帧尾追加 4 B FCS，不回写发送缓冲），
# 1 = replace（MAC 把 4 B FCS 写回发送缓冲的最后 4 B）。
# 第 35 章 U-35-10 实测：replace 模式下连续发多个大帧时，写回会踩坏相邻 pbuf，
# 导致前几帧 FCS 错、被对端丢弃（主机 rx_crc_errors 增长）。这里改成 append。
sdk_compile_definitions(-DENET_TX_HW_CRC_MODE=0)

sdk_inc(${SOMEIP_COMPONENT}/inc)
sdk_app_src(${SOMEIP_COMPONENT}/src/app_someip.c)
sdk_app_src(${SOMEIP_COMPONENT}/src/app_someip_sd.c)
sdk_app_src(${SOMEIP_COMPONENT}/src/app_someip_tp.c)
sdk_app_src(${SOMEIP_COMPONENT}/src/app_someip_net.c)
sdk_app_src(${SOMEIP_COMPONENT}/src/app_someip_selftest.c)

sdk_app_src(src/main.c)