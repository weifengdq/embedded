cmake_minimum_required(VERSION 3.13)

# eth_freertos_lwip —— FreeRTOS + lwIP(NO_SYS=0) + MCAL Eth
# lwIP 的 sys_arch 用 components/freertos_lwip_port（自写最小移植，带统计/CPU 占用）
set(CONFIG_FREERTOS 1)          # FreeRTOS 中间件
set(CONFIG_LWIP 1)              # lwIP 中间件
set(CONFIG_LWIP_NETCONN_API 1)  # 编 api/tcpip.c（NO_SYS=0 必需）
set(CONFIG_LWIP_IPERF 1)        # lwiperf
set(APP_LWIP_ETH_PORT "freertos")
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_mcal_eth_freertos_lwip)

sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_compile_definitions(-DUSE_NONVECTOR_MODE=1 -DDISABLE_IRQ_PREEMPTIVE=1)
sdk_inc(${CMAKE_CURRENT_LIST_DIR}/inc)

include(${CMAKE_CURRENT_LIST_DIR}/../../../mcal/mcal.cmake)
mcal_add(Eth EthTrcv)
include(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_lwip_eth/app_lwip_eth.cmake)

sdk_app_src(src/main.c)
