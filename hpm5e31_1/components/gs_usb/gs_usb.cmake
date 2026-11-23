# gs_usb.cmake - 把 gs_usb 协议层（纯软件）接入一个 hpm-sdk 例程
#
# 用法（find_package 之后）：
#   include(${CMAKE_CURRENT_LIST_DIR}/../../../components/gs_usb/gs_usb.cmake)
#
# 本组件不依赖 USB/HPM SDK，只提供 gs_usb 结构体、控制请求常量、帧编解码与位时序换算；
# USB 栈与 MCAN 由例程自己接（samples/app/gs_usb_candle）。

set(GS_USB_COMPONENT_DIR ${CMAKE_CURRENT_LIST_DIR})

sdk_inc(${GS_USB_COMPONENT_DIR}/inc)
sdk_app_src(${GS_USB_COMPONENT_DIR}/src/gs_usb_proto.c)
