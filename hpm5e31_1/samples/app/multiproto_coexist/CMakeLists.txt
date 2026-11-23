cmake_minimum_required(VERSION 3.13)

# 多协议 / 4 通道共存验证（U-39-1 / U-39-6）
#   CAN0 = SAE J1939 ECU（SA=0x00，500 kbit/s，EEC1 10ms / CCVS1 100ms / ET1+DM1 1s）
#   CAN1 = app_can_gw（cannelloni v2，UDP 4000 <-> 主机 vcan0）
#   CAN2 = CANopenNode v4 从站（节点 1，500 kbit/s，心跳 1s + SYNC 型 TPDO）
#   CAN3 = app_canopen 最小主站（SYNC 10ms + SDO 读 0x2102 + 心跳监视）
# 三种组件各自初始化一路 MCAN，互不重复初始化；app_lwip/app_can 只服务 CAN1 网关。
set(CONFIG_LWIP 1)
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_multiproto_coexist)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(CANOPEN_DIR ${REPO_ROOT}/components/canopen_node)
set(APP_CANOPEN_DIR ${REPO_ROOT}/components/app_canopen)
set(J1939_DIR ${REPO_ROOT}/components/app_j1939)
include(${CANOPEN_DIR}/canopen.cmake)
include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)
include(${REPO_ROOT}/components/app_can/app_can.cmake)
include(${REPO_ROOT}/components/app_can_gw/app_can_gw.cmake)

sdk_inc(${CANOPEN_INCS} ${APP_CANOPEN_DIR} ${J1939_DIR}/inc)
sdk_app_src(src/main.c ${CANOPEN_SRCS} ${APP_CANOPEN_DIR}/co_master.c
            ${J1939_DIR}/src/j1939_id.c
            ${J1939_DIR}/src/j1939_stack.c
            ${J1939_DIR}/src/j1939_tp.c
            ${J1939_DIR}/src/j1939_dm.c
            ${J1939_DIR}/src/j1939_port_mcan.c)
