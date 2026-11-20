cmake_minimum_required(VERSION 3.20)
project(stm32h723_s2500 C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 下行 (主机 -> 板子) TCP 吞吐的可调项: lwIP 的 TCP 接收窗口。
# 留空 = 沿用 common/lwip-port/lwipopts.h 的 (2 * TCP_MSS)。
# 实测 2*MSS 时下行受"两段一个 ACK 往返"限制, 调大能明显提速。
# 实测 (iperf2, 主机 192.168.10.10): 2*MSS=2920 -> 7.44 Mbit/s
#                                          4*MSS=5840 -> 8.80 Mbit/s
#                                          6*MSS=8760 -> 9.07 Mbit/s  <- 默认
#                                          8*MSS=11680 -> 9.07 Mbit/s (无进一步收益)
# 传空 (或 0) 就退回 common/lwip-port/lwipopts.h 的默认值。
set(S2500_TCP_WND 8760 CACHE STRING "Override lwIP TCP_WND in bytes (empty = lwipopts.h default)")

set(S2500_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../common)
set(HAL_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver)
set(CMSIS_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS)

set(APP_SOURCES
    Core/Src/main.c
    Core/Src/board.c
    Core/Src/port.c
    Core/Src/bench.c
    Core/Src/stm32h7xx_it.c
    Core/Src/system_stm32h7xx.c
    Core/Src/syscalls.c
    Core/Src/sysmem.c
    startup_stm32h723xx.s
)

# 只编译用得到的 HAL 模块, 避免把 ETH/I2C/SDMMC 之类的几十个文件都拉进来
set(HAL_SOURCES
    ${HAL_DIR}/Src/stm32h7xx_hal.c
    ${HAL_DIR}/Src/stm32h7xx_hal_cortex.c
    ${HAL_DIR}/Src/stm32h7xx_hal_dma.c
    ${HAL_DIR}/Src/stm32h7xx_hal_dma_ex.c
    ${HAL_DIR}/Src/stm32h7xx_hal_exti.c
    ${HAL_DIR}/Src/stm32h7xx_hal_flash.c
    ${HAL_DIR}/Src/stm32h7xx_hal_flash_ex.c
    ${HAL_DIR}/Src/stm32h7xx_hal_gpio.c
    ${HAL_DIR}/Src/stm32h7xx_hal_pwr.c
    ${HAL_DIR}/Src/stm32h7xx_hal_pwr_ex.c
    ${HAL_DIR}/Src/stm32h7xx_hal_rcc.c
    ${HAL_DIR}/Src/stm32h7xx_hal_rcc_ex.c
    ${HAL_DIR}/Src/stm32h7xx_hal_spi.c
    ${HAL_DIR}/Src/stm32h7xx_hal_spi_ex.c
    ${HAL_DIR}/Src/stm32h7xx_hal_uart.c
    ${HAL_DIR}/Src/stm32h7xx_hal_uart_ex.c
)

file(GLOB S2500_SOURCES ${S2500_COMMON}/s2500/*.c)
file(GLOB LWIP_SOURCES
    ${S2500_COMMON}/lwip/src/core/*.c
    ${S2500_COMMON}/lwip/src/core/ipv4/*.c
    ${S2500_COMMON}/lwip/src/netif/ethernet.c
    ${S2500_COMMON}/lwip/src/apps/lwiperf/lwiperf.c
)
set(SHELL_SOURCES
    ${S2500_COMMON}/letter-shell/src/shell.c
    ${S2500_COMMON}/letter-shell/src/shell_ext.c
    ${S2500_COMMON}/letter-shell/src/shell_cmd_list.c
)

add_executable(${PROJECT_NAME}.elf
    ${APP_SOURCES}
    ${HAL_SOURCES}
    ${S2500_SOURCES}
    ${LWIP_SOURCES}
    ${SHELL_SOURCES}
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    Core/Inc
    ${HAL_DIR}/Inc
    ${HAL_DIR}/Inc/Legacy
    ${CMSIS_DIR}/Device/ST/STM32H7xx/Include
    ${CMSIS_DIR}/Include
    ${S2500_COMMON}/s2500
    ${S2500_COMMON}/lwip/src/include
    ${S2500_COMMON}/lwip-port
    ${S2500_COMMON}/letter-shell/src
    ${S2500_COMMON}/letter-shell-port
)

target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
    USE_HAL_DRIVER
    STM32H723xx
    "SHELL_CFG_USER=\"shell_cfg_user.h\""
)

target_compile_options(${PROJECT_NAME}.elf PRIVATE
    -Wno-unused-parameter
    -Wno-sign-compare
    # letter-shell 的 shell_ext.h 用了 size_t 但没有 include <stddef.h>
    "$<$<COMPILE_LANGUAGE:C>:-include>" "$<$<COMPILE_LANGUAGE:C>:stddef.h>"
)

if(S2500_TCP_WND)
  target_compile_definitions(${PROJECT_NAME}.elf PRIVATE TCP_WND=${S2500_TCP_WND})
endif()

target_link_libraries(${PROJECT_NAME}.elf m)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
    COMMENT "objcopy -> hex/bin"
)
