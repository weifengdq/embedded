# app_can_gw.cmake - 把 cannelloni v2 兼容网关组件接入一个 hpm-sdk 例程
#
# 依赖（三份都要）：
#   include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)   # lwIP 底座
#   include(${REPO_ROOT}/components/app_can/app_can.cmake)     # CAN 节点
#   include(${REPO_ROOT}/components/app_can_gw/app_can_gw.cmake)
#
# 本文件必须在 find_package(hpm-sdk) 之后 include。

set(APP_CAN_GW_COMPONENT_DIR ${CMAKE_CURRENT_LIST_DIR})

sdk_inc(${APP_CAN_GW_COMPONENT_DIR}/inc)
sdk_app_src(${APP_CAN_GW_COMPONENT_DIR}/src/app_can_gw.c)
