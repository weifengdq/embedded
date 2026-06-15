cmake_minimum_required(VERSION 3.24)
project(tc387_blink_gcc_test LANGUAGES C ASM)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AurixProject.cmake")

# ---------- 可配置入口 ----------
set(AURIX_TARGET_NAME "${PROJECT_NAME}" CACHE STRING "Output target base name")
# GCC 使用 tc38xx, TASKING 使用 tc38x（两者命名规则不同）
if(AURIX_TOOLCHAIN_TYPE STREQUAL "TASKING")
    set(_default_cpu "tc38x")
else()
    set(_default_cpu "tc38xx")
endif()
set(AURIX_CPU "${_default_cpu}" CACHE STRING "TriCore CPU variant for -mcpu/-C flag")
set(AURIX_CPROJECT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.cproject" CACHE FILEPATH "ADS .cproject")

# 链接脚本依据工具链类型自动选择默认值
if(AURIX_TOOLCHAIN_TYPE STREQUAL "TASKING")
    set(_default_lsl "${CMAKE_CURRENT_SOURCE_DIR}/Lcf_Tasking_Tricore_Tc.lsl")
else()
    set(_default_lsl "${CMAKE_CURRENT_SOURCE_DIR}/Lcf_Gnuc_Tricore_Tc.lsl")
endif()
set(AURIX_LINKER_SCRIPT "${_default_lsl}" CACHE FILEPATH "Linker script")

# ---------- 排除规则 ----------
set(AURIX_EXCLUDED_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/build"
    "${CMAKE_CURRENT_SOURCE_DIR}/.ads"
    "${CMAKE_CURRENT_SOURCE_DIR}/.settings"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/.ads"
    "${CMAKE_CURRENT_SOURCE_DIR}/TriCore Debug (TASKING)"
    "${CMAKE_CURRENT_SOURCE_DIR}/TriCore Release (TASKING)"
    "${CMAKE_CURRENT_SOURCE_DIR}/TriCore Debug (GCC)"
    "${CMAKE_CURRENT_SOURCE_DIR}/TriCore Release (GCC)"
    CACHE STRING "Directories excluded from recursive source scan"
)
set(AURIX_EXCLUDED_FILES "" CACHE STRING "Files excluded from recursive source scan")

aurix_extract_cproject_excluded_dirs(
    CPROJECT_FILE "${AURIX_CPROJECT_FILE}"
    ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    OUT_EXCLUDED_DIRS AURIX_ADS_EXCLUDED_DIRS
)

set(AURIX_EFFECTIVE_EXCLUDED_DIRS ${AURIX_EXCLUDED_DIRS} ${AURIX_ADS_EXCLUDED_DIRS})
list(REMOVE_DUPLICATES AURIX_EFFECTIVE_EXCLUDED_DIRS)

if(NOT EXISTS "${AURIX_LINKER_SCRIPT}")
    message(FATAL_ERROR "Linker script not found: ${AURIX_LINKER_SCRIPT}")
endif()

# ---------- 收集源码 ----------
aurix_collect_project_files(
    ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    EXCLUDED_DIRS "${AURIX_EFFECTIVE_EXCLUDED_DIRS}"
    EXCLUDED_FILES "${AURIX_EXCLUDED_FILES}"
    OUT_SOURCES AURIX_SOURCE_FILES
    OUT_HEADERS AURIX_HEADER_FILES
    OUT_INCLUDE_DIRS AURIX_INCLUDE_DIRS
)

if(NOT AURIX_SOURCE_FILES)
    message(FATAL_ERROR "No C source files were found after applying exclusions.")
endif()

# ---------- 构建目标 ----------
add_executable(${PROJECT_NAME} ${AURIX_SOURCE_FILES} ${AURIX_HEADER_FILES})

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${AURIX_TARGET_NAME}"
    SUFFIX ".elf"
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINKER_LANGUAGE C
)

target_include_directories(${PROJECT_NAME} PRIVATE ${AURIX_INCLUDE_DIRS})

# ---------- 工具链特定选项 ----------
if(AURIX_TOOLCHAIN_TYPE STREQUAL "TASKING")
    # TASKING C 编译选项（与 ADS TriCore Debug(TASKING) 配置对应）
    target_compile_options(${PROJECT_NAME} PRIVATE
        -D__CPU__=${AURIX_CPU}
        --iso=99
        --language=+volatile
        --fp-model=3
        -O0
        --tradeoff=4
        --compact-max-size=200
        -g
        -Wc-w544
        -Wc-w557
        -Wc-w508
        -C${AURIX_CPU}
        -Y0 -N0 -Z0
    )

    # TASKING 链接选项
    # --format=ihex: 链接器直接输出 Intel HEX 内容（后续 post-build 重命名为 .hex）
    target_link_options(${PROJECT_NAME} PRIVATE
        -d${AURIX_LINKER_SCRIPT}
        -Wl-Oc -Wl-OL -Wl-Ot -Wl-Ox -Wl-Oy
        -Wl-mc -Wl-mf -Wl-mi -Wl-mk -Wl-ml -Wl-mm -Wl-md -Wl-mr -Wl-mu
        --no-warnings=
        --fp-model=3
        -lrt
        --lsl-core=vtc
        --exceptions
        --strict
        --anachronisms
        --force-c++
        -C${AURIX_CPU}
        --format=ihex
    )

    # TASKING 链接器输出为 ihex 内容但文件名后缀是 .elf（CMake 命名），
    # post-build 将其复制为 .hex 供烧录工具使用。
    set(AURIX_HEX_FILE "${CMAKE_CURRENT_BINARY_DIR}/${AURIX_TARGET_NAME}.hex")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:${PROJECT_NAME}>"
            "${AURIX_HEX_FILE}"
        COMMENT "Copying ihex output to ${AURIX_TARGET_NAME}.hex"
        VERBATIM
    )

else()
    # TriCore GCC 编译选项
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:-Og>
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Debug>:-gdwarf-3>
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:-O2>
        $<$<CONFIG:RelWithDebInfo>:-g>
        $<$<CONFIG:RelWithDebInfo>:-DNDEBUG>
        $<$<CONFIG:MinSizeRel>:-Os>
        $<$<CONFIG:MinSizeRel>:-DNDEBUG>
        -Wall
        -fmessage-length=0
        -fno-common
        -fstrict-volatile-bitfields
        -fdata-sections
        -ffunction-sections
        -mcpu=${AURIX_CPU}
    )

    set(AURIX_MAP_FILE "${CMAKE_CURRENT_BINARY_DIR}/${AURIX_TARGET_NAME}.map")
    set(AURIX_HEX_FILE "${CMAKE_CURRENT_BINARY_DIR}/${AURIX_TARGET_NAME}.hex")

    # TriCore GCC 链接选项
    target_link_options(${PROJECT_NAME} PRIVATE
        -T${AURIX_LINKER_SCRIPT}
        -nocrt0
        -mcpu=${AURIX_CPU}
        -Wl,--gc-sections
        -Wl,-Map,${AURIX_MAP_FILE}
    )

    # ELF -> HEX
    add_custom_command(
        OUTPUT "${AURIX_HEX_FILE}"
        COMMAND ${CMAKE_OBJCOPY} -O ihex "$<TARGET_FILE:${PROJECT_NAME}>" "${AURIX_HEX_FILE}"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Generating ${AURIX_TARGET_NAME}.hex"
        VERBATIM
    )
    add_custom_target(${PROJECT_NAME}_hex ALL DEPENDS "${AURIX_HEX_FILE}")

    add_custom_target(${PROJECT_NAME}_size
        COMMAND ${CMAKE_SIZE} --format=berkeley "$<TARGET_FILE:${PROJECT_NAME}>"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Printing ${AURIX_TARGET_NAME}.elf size"
        VERBATIM
    )
endif()

# ---------- 配置摘要 ----------
list(LENGTH AURIX_SOURCE_FILES AURIX_SOURCE_COUNT)
list(LENGTH AURIX_INCLUDE_DIRS AURIX_INCLUDE_COUNT)
message(STATUS "AURIX target name     : ${AURIX_TARGET_NAME}")
message(STATUS "AURIX toolchain type  : ${AURIX_TOOLCHAIN_TYPE}")
message(STATUS "AURIX CPU             : ${AURIX_CPU}")
message(STATUS "AURIX linker script   : ${AURIX_LINKER_SCRIPT}")
message(STATUS "AURIX source count    : ${AURIX_SOURCE_COUNT}")
message(STATUS "AURIX include count   : ${AURIX_INCLUDE_COUNT}")
message(STATUS "AURIX hex output      : ${AURIX_HEX_FILE}")
