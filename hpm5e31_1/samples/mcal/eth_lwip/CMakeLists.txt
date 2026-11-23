cmake_minimum_required(VERSION 3.13)

# eth_lwip —— AUTOSAR MCAL Eth + lwIP 2.1.2（裸机 NO_SYS=1）ping / iperf2 实测
set(CONFIG_LWIP 1)              # 编 lwIP 中间件
set(CONFIG_LWIP_IPERF 1)        # 编 apps/lwiperf（iperf2 协议，主机用 iperf v2）
set(APP_LWIP_ETH_PORT "baremetal")
set(APP_USE_ENET_PORT_COUNT 1)  # 板级 CMake 据此开 ENET0 RGMII 引脚/时钟
set(APP_USE_ENET_ITF_RGMII 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_mcal_eth_lwip)

sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_inc(${CMAKE_CURRENT_LIST_DIR}/inc)

include(${CMAKE_CURRENT_LIST_DIR}/../../../mcal/mcal.cmake)
mcal_add(Eth EthTrcv)
include(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_lwip_eth/app_lwip_eth.cmake)

sdk_app_src(src/main.c)
