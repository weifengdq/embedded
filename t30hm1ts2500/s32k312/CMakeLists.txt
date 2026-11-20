cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME s32k312_s2500)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

enable_language(C ASM)

add_executable(${CMAKE_PROJECT_NAME})

# 公共 S2500 驱动 / lwIP / letter-shell
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Startup and system
    Project_Settings/Startup_Code/startup.c
    Project_Settings/Startup_Code/system.c
    Project_Settings/Startup_Code/exceptions.c
    Project_Settings/Startup_Code/nvic.c
    Project_Settings/Startup_Code/startup_cm7.s
    Project_Settings/Startup_Code/Vector_Table.s

    # Board (pin mux + generated peripheral cfg)
    board/Siul2_Port_Ip_Cfg.c
    board/Tspc_Port_Ip_Cfg.c

    # Generate
    generate/src/Clock_Ip_Cfg.c
    generate/src/Igf_Port_Ip_Cfg.c
    generate/src/IntCtrl_Ip_Cfg.c
    generate/src/Lpuart_Uart_Ip_Sa_PBcfg.c
    generate/src/OsIf_Cfg.c

    # RTD
    RTD/src/Clock_Ip.c
    RTD/src/Clock_Ip_Data.c
    RTD/src/Clock_Ip_Divider.c
    RTD/src/Clock_Ip_DividerTrigger.c
    RTD/src/Clock_Ip_ExtOsc.c
    RTD/src/Clock_Ip_FracDiv.c
    RTD/src/Clock_Ip_Frequency.c
    RTD/src/Clock_Ip_Gate.c
    RTD/src/Clock_Ip_IntOsc.c
    RTD/src/Clock_Ip_Irq.c
    RTD/src/Clock_Ip_Monitor.c
    RTD/src/Clock_Ip_Pll.c
    RTD/src/Clock_Ip_ProgFreqSwitch.c
    RTD/src/Clock_Ip_Selector.c
    RTD/src/Clock_Ip_Specific.c
    RTD/src/Det.c
    RTD/src/Det_stub.c
    RTD/src/Igf_Port_Ip.c
    RTD/src/IntCtrl_Ip.c
    RTD/src/Lpuart_Uart_Ip.c
    RTD/src/Lpuart_Uart_Ip_Irq.c
    RTD/src/OsIf_Interrupts.c
    RTD/src/OsIf_Software_Semaphore.c
    RTD/src/OsIf_Timer.c
    RTD/src/OsIf_Timer_System.c
    RTD/src/OsIf_Timer_System_Internal_Systick.c
    RTD/src/SchM_BaseNXP.c
    RTD/src/SchM_Dio.c
    RTD/src/SchM_Mcu.c
    RTD/src/SchM_Port.c
    RTD/src/SchM_Uart.c
    RTD/src/Siul2_Dio_Ip.c
    RTD/src/Siul2_Port_Ip.c
    RTD/src/Tspc_Port_Ip.c

    # 平台移植层
    src/main.c
    src/systick.c
    src/syscalls.c
    src/schm_stub.c
    src/s2500_port.c
    src/s2500_spi.c
    src/spi_soft.c
    src/lpspi_hw.c
    src/lpspi_diag.c

    # 公共 S2500 驱动
    ${COMMON_DIR}/s2500/s2500.c
    ${COMMON_DIR}/s2500/s2500_netif.c
    ${COMMON_DIR}/s2500/s2500_shell.c
    ${COMMON_DIR}/s2500/s2500_app.c

    # lwIP 2.2.0 (NO_SYS)
    ${COMMON_DIR}/lwip/src/core/init.c
    ${COMMON_DIR}/lwip/src/core/def.c
    ${COMMON_DIR}/lwip/src/core/dns.c
    ${COMMON_DIR}/lwip/src/core/inet_chksum.c
    ${COMMON_DIR}/lwip/src/core/ip.c
    ${COMMON_DIR}/lwip/src/core/mem.c
    ${COMMON_DIR}/lwip/src/core/memp.c
    ${COMMON_DIR}/lwip/src/core/netif.c
    ${COMMON_DIR}/lwip/src/core/pbuf.c
    ${COMMON_DIR}/lwip/src/core/raw.c
    ${COMMON_DIR}/lwip/src/core/stats.c
    ${COMMON_DIR}/lwip/src/core/sys.c
    ${COMMON_DIR}/lwip/src/core/tcp.c
    ${COMMON_DIR}/lwip/src/core/tcp_in.c
    ${COMMON_DIR}/lwip/src/core/tcp_out.c
    ${COMMON_DIR}/lwip/src/core/timeouts.c
    ${COMMON_DIR}/lwip/src/core/udp.c
    ${COMMON_DIR}/lwip/src/core/ipv4/etharp.c
    ${COMMON_DIR}/lwip/src/core/ipv4/icmp.c
    ${COMMON_DIR}/lwip/src/core/ipv4/igmp.c
    ${COMMON_DIR}/lwip/src/core/ipv4/ip4.c
    ${COMMON_DIR}/lwip/src/core/ipv4/ip4_addr.c
    ${COMMON_DIR}/lwip/src/core/ipv4/ip4_frag.c
    ${COMMON_DIR}/lwip/src/core/ipv4/acd.c
    ${COMMON_DIR}/lwip/src/netif/ethernet.c
    ${COMMON_DIR}/lwip/src/apps/lwiperf/lwiperf.c

    # letter-shell 3.2.4
    ${COMMON_DIR}/letter-shell/src/shell.c
    ${COMMON_DIR}/letter-shell/src/shell_cmd_list.c
    ${COMMON_DIR}/letter-shell/src/shell_ext.c
)


# common/letter-shell 3.2.4 的 shell_ext.h 未包含 <stddef.h>, 这里为单个源文件强制包含
set_source_files_properties(${COMMON_DIR}/letter-shell/src/shell_ext.c
    PROPERTIES COMPILE_OPTIONS "-include;stddef.h")

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    RTD/include
    generate/include
    board
    Project_Settings/Startup_Code
    PlatformSDK/header
    PlatformSDK/include
    PlatformSDK/startup/include
    ${COMMON_DIR}/s2500
    ${COMMON_DIR}/lwip/src/include
    ${COMMON_DIR}/lwip-port
    ${COMMON_DIR}/lwip-port/arch
    ${COMMON_DIR}/letter-shell/src
    ${COMMON_DIR}/letter-shell-port
    src
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    CPU_S32K312
    CPU_CORTEX_M7
    S32K312
    S32K3XX
    DERIVATIVE_S32K312
    S32K312_M7_0
    D_CACHE_ENABLE
    I_CACHE_ENABLE
    ENABLE_FPU
    MPU_ENABLE
    GCC
    DISABLE_MCAL_INTERMODULE_ASR_CHECK
    SHELL_CFG_USER="shell_cfg_user.h"
)

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMENT "Generating hex/bin and size info"
)

