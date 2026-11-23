cmake_minimum_required(VERSION 3.13)
# can_logger 同时支持：Bench A（CAN0 记录 + CAN1 回放注入）与 Bench B（gw on：CAN1 交给 app_can_gw，
# 主机 canplayer -> vcan0 -> 网关 -> CAN0 记录，板端 verify 校验顺序）
set(CONFIG_LWIP 1)
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_can_logger)
set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)
include(${REPO_ROOT}/components/app_can/app_can.cmake)
include(${REPO_ROOT}/components/app_can_gw/app_can_gw.cmake)
sdk_app_src(src/main.c)
