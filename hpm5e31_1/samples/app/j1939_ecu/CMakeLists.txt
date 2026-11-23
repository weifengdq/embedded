cmake_minimum_required(VERSION 3.13)

# Bench B 变体：J1939_WITH_GW=1（环境变量）时把 CAN1 让给 app_can_gw 网关，
# 板端 ECU 在 CAN0、主机经 vcan0 + cannelloni 访问。
# 默认（不设置该环境变量）是 Bench A 板内双节点自测。
if(DEFINED ENV{J1939_WITH_GW})
  set(CONFIG_LWIP 1)
  set(APP_USE_ENET_PORT_COUNT 1)
  set(APP_USE_ENET_ITF_RGMII 1)
endif()

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_j1939_ecu)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_inc(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/inc)
sdk_app_src(src/main.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_id.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_stack.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_tp.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_dm.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_port_mcan.c)

if(DEFINED ENV{J1939_WITH_GW})
  set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
  include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)
  include(${REPO_ROOT}/components/app_can/app_can.cmake)
  include(${REPO_ROOT}/components/app_can_gw/app_can_gw.cmake)
  sdk_compile_definitions(-DJ1939_WITH_GW=1)
endif()