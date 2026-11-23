cmake_minimum_required(VERSION 3.13)

# 本样片同时支持两种 bench：
#   Bench A：CAN0=CANopen 从站节点1，CAN1=板内最小主站（默认，上电即跑）
#   Bench B：shell 'gw on' 后 CAN1 交给 app_can_gw（CAN1<->UDP4000<->主机 vcan0），CAN0 仍是从站
# 下面 3 个 set 必须在 find_package 之前（板级库在 find_package 内配置）
set(CONFIG_LWIP 1)
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_canopen_node)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(CANOPEN_DIR ${REPO_ROOT}/components/canopen_node)
set(APP_DIR ${REPO_ROOT}/components/app_canopen)
include(${CANOPEN_DIR}/canopen.cmake)
include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)
include(${REPO_ROOT}/components/app_can/app_can.cmake)
include(${REPO_ROOT}/components/app_can_gw/app_can_gw.cmake)

sdk_inc(${CANOPEN_INCS} ${APP_DIR})
sdk_app_src(src/main.c ${CANOPEN_SRCS} ${APP_DIR}/co_master.c)
