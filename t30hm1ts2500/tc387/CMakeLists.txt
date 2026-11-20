cmake_minimum_required(VERSION 3.20)
project(tc387_s2500 C ASM)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(AURIX_CPU "tc38xx" CACHE STRING "TriCore CPU")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AurixProject.cmake")
add_compile_options(-include sys/types.h -DSSIZE_MAX=0x7FFFFFFF)

# ---- 收集本工程源码 (src / Configurations / Libraries) ----
set(AURIX_EXCLUDED_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/build"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)
aurix_collect_project_files(
    ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    EXCLUDED_DIRS "${AURIX_EXCLUDED_DIRS}"
    EXCLUDED_FILES ""
    OUT_SOURCES AURIX_SOURCES
    OUT_HEADERS AURIX_HEADERS
    OUT_INCLUDE_DIRS AURIX_INCLUDES
)

# ---- 复用 common/ 下的 S2500 驱动 + lwIP + letter-shell ----
set(COMMON "${CMAKE_CURRENT_SOURCE_DIR}/../common")
file(GLOB COMMON_S2500 "${COMMON}/s2500/*.c")
file(GLOB LWIP_CORE   "${COMMON}/lwip/src/core/*.c")
file(GLOB LWIP_IPV4   "${COMMON}/lwip/src/core/ipv4/*.c")
file(GLOB SHELL_SRC   "${COMMON}/letter-shell/src/*.c")
set(LWIP_MISC
    "${COMMON}/lwip/src/netif/ethernet.c"
    "${COMMON}/lwip/src/apps/lwiperf/lwiperf.c"
)

add_executable(${PROJECT_NAME}
    ${AURIX_SOURCES}
    ${COMMON_S2500}
    ${LWIP_CORE}
    ${LWIP_IPV4}
    ${LWIP_MISC}
    ${SHELL_SRC}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${AURIX_INCLUDES}
    "${COMMON}/s2500"
    "${COMMON}/lwip/src/include"
    "${COMMON}/lwip-port"
    "${COMMON}/letter-shell/src"
    "${COMMON}/letter-shell-port"
)

# 只启动 CPU0, 其余核保持复位/空闲
target_compile_definitions(${PROJECT_NAME} PRIVATE
    IFX_CFG_SSW_ENABLE_TRICORE1=0
    IFX_CFG_SSW_ENABLE_TRICORE2=0
    IFX_CFG_SSW_ENABLE_TRICORE3=0
    SHELL_CFG_USER="shell_cfg_user.h"
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -O2 -g3 -gdwarf-3
    -Wall -fmessage-length=0
    -fno-common
    -fstrict-volatile-bitfields
    -ffunction-sections
    -mcpu=${AURIX_CPU}
)

set(AURIX_MAP "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map")
set(AURIX_HEX "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex")

target_link_options(${PROJECT_NAME} PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/Lcf_Gnuc_Tricore_Tc.lsl
    -nocrt0
    -mcpu=${AURIX_CPU}
    -Wl,--gc-sections
    -Wl,-Map,${AURIX_MAP}
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex "$<TARGET_FILE:${PROJECT_NAME}>" "${AURIX_HEX}"
    COMMENT "Generating ${PROJECT_NAME}.hex"
)

add_custom_target(size
    COMMAND ${CMAKE_SIZE} --format=berkeley "$<TARGET_FILE:${PROJECT_NAME}>"
    DEPENDS ${PROJECT_NAME}
    COMMENT "Size"
)
