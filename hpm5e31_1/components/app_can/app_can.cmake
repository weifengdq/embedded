# app_can.cmake - 把 CAN FD 节点组件接入一个 hpm-sdk 例程
#
# 用法（放在 find_package(hpm-sdk) 之后、sdk_app_src(src/main.c) 之前）:
#     include(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_can/app_can.cmake)
#
# 组件依赖板级包 boards/hpm5e31_1（board_init_can / board_can_transceivers_enable），
# 这些符号由 -DBOARD=hpm5e31_1 的板级库提供，不需要额外 target_link_libraries。

set(APP_CAN_COMPONENT_DIR ${CMAKE_CURRENT_LIST_DIR})

sdk_inc(${APP_CAN_COMPONENT_DIR}/inc)
sdk_app_src(${APP_CAN_COMPONENT_DIR}/src/app_can.c)
