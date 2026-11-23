# freertos_lwip_port.cmake - 自写 FreeRTOS + lwIP sys_arch 移植组件
#
# 用法（例程 CMakeLists.txt，注意顺序）：
#   cmake_minimum_required(VERSION 3.13)
#   set(CONFIG_FREERTOS 1)          # 必须在 find_package 之前：让 SDK 编 FreeRTOS 中间件
#   set(CONFIG_LWIP 1)              # 必须在 find_package 之前：让 SDK 编 lwIP 中间件
#   find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
#   project(xxx)
#   include(${CMAKE_CURRENT_LIST_DIR}/../../../components/freertos_lwip_port/freertos_lwip_port.cmake)
#   sdk_app_src(src/main.c)
#
# 本文件只做三件事：头文件目录（含 arch/）、sys_arch.c、以及 CONFIG 检查。
# 另需应用侧提供 inc/FreeRTOSConfig.h（本组件不提供，因为堆大小/优先级是例程级决策）。

if(NOT CONFIG_FREERTOS)
  message(FATAL_ERROR "freertos_lwip_port: 必须在 find_package 之前 set(CONFIG_FREERTOS 1)")
endif()
if(NOT CONFIG_LWIP)
  message(FATAL_ERROR "freertos_lwip_port: 必须在 find_package 之前 set(CONFIG_LWIP 1)")
endif()

set(FREERTOS_LWIP_PORT_DIR "${CMAKE_CURRENT_LIST_DIR}")

# lwIP 的 lwip/sys.h 里是 #include "arch/sys_arch.h"，所以要把 inc/ 加进来（arch/ 在其下）
sdk_inc("${FREERTOS_LWIP_PORT_DIR}/inc")
sdk_app_src("${FREERTOS_LWIP_PORT_DIR}/src/sys_arch.c")

# 标记：使用自写移植（例程里可用它来打日志区分 SDK 样例移植）
sdk_compile_definitions(-DFREERTOS_LWIP_PORT_CUSTOM=1)
