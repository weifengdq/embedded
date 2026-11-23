# gs_usb_candle - gs_usb / candleLight 兼容 4 路 CAN FD USB 适配器
#
# USB: CherryUSB device stack（vendor class 自定义描述符，gs_usb 协议）
# CAN: components/app_can（4x MCAN，经典/FD 任意位时序）
cmake_minimum_required(VERSION 3.13)

set(CONFIG_CHERRYUSB 1)
set(CONFIG_USB_DEVICE 1)
set(CONFIG_CHERRYUSB_DEVICE 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_gs_usb_candle)

sdk_inc(${HPM_SDK_BASE}/samples/cherryusb/config)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)

include(${CMAKE_CURRENT_LIST_DIR}/../../../components/gs_usb/gs_usb.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_can/app_can.cmake)

sdk_app_src(src/main.c)
sdk_app_src(src/bridge.c)
sdk_app_src(src/usb_gs.c)
