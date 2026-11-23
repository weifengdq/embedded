# can_gateway - cannelloni v2 兼容网关 + CAN0 监视/回显节点
#
# 板端：CAN0 = DUT 监视/回显节点（0x123 回显成 0x321 + 100 ms 心跳）
#       CAN1 = cannelloni 网关（UDP 4000 <-> 主机 vcan0），可选 CAN2/CAN3 -> 4002/4003
# 主机：host/can_bridge/ 里起 vcan + cannelloni，然后 candump vcan0 / cansend vcan0
cmake_minimum_required(VERSION 3.13)

# 下面 3 个 set 必须在 find_package 之前（板级库在 find_package 内配置）
set(CONFIG_LWIP 1)
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_can_gateway)

# samples/app/can_gateway -> 仓库根
set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)
include(${REPO_ROOT}/components/app_can/app_can.cmake)
include(${REPO_ROOT}/components/app_can_gw/app_can_gw.cmake)

sdk_app_src(src/main.c)
