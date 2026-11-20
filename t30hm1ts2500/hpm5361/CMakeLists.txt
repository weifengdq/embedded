# HPM5361 + T30HM1TS2500 (S2500) 10BASE-T1S 固件
# 基于 HPM SDK 1.13.0 (BOARD=hpm5300evk, HPM5361xCBx, 1MB QSPI NOR)
cmake_minimum_required(VERSION 3.13)

if("${HPM_BUILD_TYPE}" STREQUAL "")
    set(HPM_BUILD_TYPE flash_xip)
endif()

# 使用 SDK 的 SPI 组件 (hpm_spi.h: 阻塞式全双工收发 + DMA 启动)
set(CONFIG_HPM_SPI 1)

# letter-shell 的命令表使用 "shellCommand" 段, 需要在链接脚本里给出起止符号。
# 在 SDK 的 flash_xip.ld 基础上, 把该段放进 .text (这样 __fw_size__ 也正确)。
set(_sdk_base "$ENV{HPM_SDK_BASE}")
if(_sdk_base)
    set(_orig_ld "${_sdk_base}/soc/HPM5300/HPM5361/toolchains/gcc/flash_xip.ld")
    if(EXISTS "${_orig_ld}")
        file(READ "${_orig_ld}" _ld_content)
        string(REPLACE "*(.text*)"
            "*(.text*)\n        . = ALIGN(4);\n        _shell_command_start = .;\n        KEEP(*(shellCommand))\n        _shell_command_end = .;"
            _ld_content "${_ld_content}")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/hpm5361_s2500.ld" "${_ld_content}")
        set(CUSTOM_GCC_LINKER_FILE "${CMAKE_CURRENT_BINARY_DIR}/hpm5361_s2500.ld"
            CACHE FILEPATH "HPM5361 linker script with letter-shell section" FORCE)
    endif()
endif()

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)

project(hpm5361_s2500)

set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)
set(S2500_DIR  ${COMMON_DIR}/s2500)
set(LWIP_DIR   ${COMMON_DIR}/lwip)
set(SHELL_DIR  ${COMMON_DIR}/letter-shell)

file(GLOB S2500_SRC ${S2500_DIR}/*.c)
file(GLOB LWIP_SRC
    ${LWIP_DIR}/src/core/*.c
    ${LWIP_DIR}/src/core/ipv4/*.c
    ${LWIP_DIR}/src/netif/ethernet.c
    ${LWIP_DIR}/src/apps/lwiperf/lwiperf.c
)
set(SHELL_SRC
    ${SHELL_DIR}/src/shell.c
    ${SHELL_DIR}/src/shell_ext.c
    ${SHELL_DIR}/src/shell_cmd_list.c
)

sdk_inc(src)
sdk_inc(${S2500_DIR})
sdk_inc(${LWIP_DIR}/src/include)
sdk_inc(${COMMON_DIR}/lwip-port)
sdk_inc(${SHELL_DIR}/src)
sdk_inc(${COMMON_DIR}/letter-shell-port)

sdk_app_src(
    src/main.c
    src/bsp.c
    src/diag.c
    src/port.c
    ${S2500_SRC}
    ${LWIP_SRC}
    ${SHELL_SRC}
)

sdk_compile_definitions("-DSHELL_CFG_USER=\"shell_cfg_user.h\"")

# lwIP 调优只作用于本板: common/lwip-port/lwipopts.h 中这些宏都有 #ifndef 保护,
# 其它平台不传这些 -D 就保持原值, 行为完全不变。
sdk_compile_definitions(
    "-DMEM_SIZE=32768"              # lwIP 堆 24K -> 32K
    "-DTCP_WND=5840"                # 4*MSS 接收窗口 (原 2*MSS)
    "-DTCP_SND_BUF=8760"            # 6*MSS 发送窗口 (原 4*MSS)
    "-DTCP_SND_QUEUELEN=16"
    "-DPBUF_POOL_SIZE=28"           # RX pbuf 池 24 -> 28
    "-DMEMP_NUM_TCP_SEG=32"
    "-DMEMP_NUM_PBUF=8"
)

# letter-shell 的 shell_ext.h 使用 size_t 但没有 include <stddef.h>
target_compile_options(app PRIVATE
    -Wno-unused-parameter
    -Wno-sign-compare
    "$<$<COMPILE_LANGUAGE:C>:-include>" "$<$<COMPILE_LANGUAGE:C>:stddef.h>"
)

generate_ide_projects()
