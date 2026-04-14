cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM and C languages
enable_language(C ASM)
# STM32CubeMX generated symbols (macros)
set(MX_Defines_Syms 
	USE_HAL_DRIVER 
	STM32F407xx
    $<$<CONFIG:Debug>:DEBUG>
)

# STM32CubeMX generated include paths
set(MX_Include_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../LWIP/App
    ${CMAKE_CURRENT_SOURCE_DIR}/../../LWIP/Target
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/system
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/include/netif/ppp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/include/lwip
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/include/lwip/apps
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/include/lwip/priv
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/include/lwip/prot
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/include/netif
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/include/compat/posix
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/include/compat/posix/arpa
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/include/compat/posix/net
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/include/compat/posix/sys
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/include/compat/stdc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/system/arch
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Include
)

# STM32CubeMX generated application sources
set(MX_Application_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../LWIP/App/lwip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../LWIP/Target/eth_custom_phy_interface.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32f4xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32f4xx_hal_msp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sysmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/syscalls.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../startup_stm32f407xx.s
)

# STM32 HAL/LL Drivers
set(STM32_Drivers_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/system_stm32f4xx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
)

set(STM32_HAL_ETH_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_eth.c
)

set(STM32_LWIP_Port_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../LWIP/Target/ethernetif.c
)

# Drivers Midllewares


set(LwIP_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/auth.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/ccp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/chap_ms.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/chap-md5.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/chap-new.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/demand.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/eap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/eui64.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/fsm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/ipcp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/ipv6cp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/lcp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/magic.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/mppe.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/multilink.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/ppp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/pppapi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/pppcrypt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/pppoe.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/pppol2tp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/pppos.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/upap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/vj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/bridgeif.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/bridgeif_fdb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ethernet.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/lowpan6.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/lowpan6_ble.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/lowpan6_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/slipif.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/zepif.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/netif/ppp/ecp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/api/api_lib.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/api/api_msg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/api/err.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/api/if_api.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/api/netbuf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/api/netdb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/api/netifapi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/api/sockets.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/api/tcpip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/altcp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/altcp_alloc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/altcp_tcp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/def.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/dns.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/inet_chksum.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/init.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/mem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/memp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/netif.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/pbuf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/raw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/stats.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/sys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/tcp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/tcp_in.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/tcp_out.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/timeouts.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/udp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv4/autoip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv4/dhcp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv4/etharp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv4/icmp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv4/igmp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv4/ip4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv4/ip4_addr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv4/ip4_frag.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv6/dhcp6.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv6/ethip6.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv6/icmp6.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv6/inet6.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv6/ip6.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv6/ip6_addr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv6/ip6_frag.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv6/mld6.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/core/ipv6/nd6.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/Third_Party/LwIP/src/apps/mqtt/mqtt.c
)

# Link directories setup
set(MX_LINK_DIRS

)
# Project static libraries
set(MX_LINK_LIBS 
    STM32_Drivers
    STM32_ETH_Driver
    STM32_LWIP_Port
    ${TOOLCHAIN_LINK_LIBRARIES}
    LwIP
	
)
# Interface library for includes and symbols
add_library(stm32cubemx INTERFACE)
target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})

# Create STM32_Drivers static library
add_library(STM32_Drivers OBJECT)
target_sources(STM32_Drivers PRIVATE ${STM32_Drivers_Src})
target_link_libraries(STM32_Drivers PUBLIC stm32cubemx)

# Build the ETH HAL driver separately so only this file can use a safer
# optimization level in Release builds.
add_library(STM32_ETH_Driver OBJECT)
target_sources(STM32_ETH_Driver PRIVATE ${STM32_HAL_ETH_SRC})
target_link_libraries(STM32_ETH_Driver PUBLIC stm32cubemx)

add_library(STM32_LWIP_Port OBJECT)
target_sources(STM32_LWIP_Port PRIVATE ${STM32_LWIP_Port_Src})
target_link_libraries(STM32_LWIP_Port PUBLIC stm32cubemx)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(STM32_ETH_Driver PRIVATE -O0)
    target_compile_options(STM32_LWIP_Port PRIVATE -O0)
endif()


# Create LwIP static library
add_library(LwIP OBJECT)
target_sources(LwIP PRIVATE ${LwIP_Src})
target_link_libraries(LwIP PUBLIC stm32cubemx)

# Add STM32CubeMX generated application sources to the project
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${MX_Application_Src})

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${MX_LINK_DIRS})

# Add libraries to the project
target_link_libraries(${CMAKE_PROJECT_NAME} ${MX_LINK_LIBS})

# Add the map file to the list of files to be removed with 'clean' target
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES ADDITIONAL_CLEAN_FILES ${CMAKE_PROJECT_NAME}.map)

# Validate that STM32CubeMX code is compatible with C standard
if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(ERROR "Generated code requires C11 or higher")
endif()
