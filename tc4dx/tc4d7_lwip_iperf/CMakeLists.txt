cmake_minimum_required(VERSION 3.24)

project(tc4d7_lwip_iperf LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AurixProject.cmake")

set(AURIX_TARGET_NAME "${PROJECT_NAME}" CACHE STRING "Output target base name")
set(AURIX_CPU "tc4DAx" CACHE STRING "TriCore CPU variant passed to -mcpu")
set(AURIX_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/Lcf_Gcc_Tricore_Tc.lsl" CACHE FILEPATH "Linker script")
set(AURIX_CPROJECT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.cproject" CACHE FILEPATH "ADS .cproject used to import source exclusion rules")
set(AURIX_EXCLUDED_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/build"
    "${CMAKE_CURRENT_SOURCE_DIR}/TriCore Debug (GCC)"
    "${CMAKE_CURRENT_SOURCE_DIR}/TriCore Release (GCC)"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Ethernet/lwip/src/apps"
    CACHE STRING "Semicolon-separated directories excluded from recursive source scan"
)
set(AURIX_EXCLUDED_FILES "" CACHE STRING "Semicolon-separated files excluded from recursive source scan")

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

aurix_collect_project_files(
    ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    EXCLUDED_DIRS "${AURIX_EFFECTIVE_EXCLUDED_DIRS}"
    EXCLUDED_FILES "${AURIX_EXCLUDED_FILES}"
    OUT_SOURCES AURIX_SOURCE_FILES
    OUT_HEADERS AURIX_HEADER_FILES
    OUT_INCLUDE_DIRS AURIX_INCLUDE_DIRS
)

set(AURIX_MANUAL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/CpuGeneric/Geth/Std/IfxGeth.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/CpuGeneric/Geth/Eth/IfxGeth_Eth.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/CpuGeneric/Hsphy/Std/IfxHsphy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/Tricore/I2c/Std/IfxI2c.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/Tricore/I2c/I2c/IfxI2c_I2c.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/Tricore/Pms/Std/IfxPmsEvr.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/Tricore/Vmt/Std/IfxVmt.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Ethernet/lwip/src/apps/lwiperf/lwiperf.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/CpuGeneric/_PinMap/TC4Dx/IfxAsclin_PinMap_TC4Dx_BGA292_COM.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/CpuGeneric/_PinMap/TC4Dx/IfxGeth_PinMap_TC4Dx_BGA292_COM.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/Tricore/_PinMap/TC4Dx/IfxI2c_PinMap_TC4Dx_BGA292_COM.c"
)

list(APPEND AURIX_SOURCE_FILES ${AURIX_MANUAL_SOURCES})
list(REMOVE_DUPLICATES AURIX_SOURCE_FILES)

list(APPEND AURIX_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/CpuGeneric/Geth/Eth"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/CpuGeneric/Hsphy/Std"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/CpuGeneric/_PinMap"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/CpuGeneric/_PinMap/TC4Dx"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/Tricore/I2c/I2c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/Tricore/Pms/Std"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/Tricore/_PinMap"
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC4xx/Tricore/_PinMap/TC4Dx"
)
list(REMOVE_DUPLICATES AURIX_INCLUDE_DIRS)

if(NOT AURIX_SOURCE_FILES)
    message(FATAL_ERROR "No C source files were found after applying exclusions.")
endif()

add_executable(${PROJECT_NAME} ${AURIX_SOURCE_FILES} ${AURIX_HEADER_FILES})

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${AURIX_TARGET_NAME}"
    SUFFIX ".elf"
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

target_include_directories(${PROJECT_NAME} PRIVATE ${AURIX_INCLUDE_DIRS})

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

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:AURIX_COMPILER_FLAGS="-Og -g3">
    $<$<CONFIG:Release>:AURIX_COMPILER_FLAGS="-O3 -DNDEBUG">
    $<$<CONFIG:RelWithDebInfo>:AURIX_COMPILER_FLAGS="-O2 -g -DNDEBUG">
    $<$<CONFIG:MinSizeRel>:AURIX_COMPILER_FLAGS="-Os -DNDEBUG">
)

set(AURIX_MAP_FILE "${CMAKE_CURRENT_BINARY_DIR}/${AURIX_TARGET_NAME}.map")
set(AURIX_HEX_FILE "${CMAKE_CURRENT_BINARY_DIR}/${AURIX_TARGET_NAME}.hex")

target_link_options(${PROJECT_NAME} PRIVATE
    -T${AURIX_LINKER_SCRIPT}
    -nocrt0
    -mcpu=${AURIX_CPU}
    -Wl,--gc-sections
    -Wl,-Map,${AURIX_MAP_FILE}
)

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

list(LENGTH AURIX_SOURCE_FILES AURIX_SOURCE_COUNT)
list(LENGTH AURIX_INCLUDE_DIRS AURIX_INCLUDE_COUNT)

message(STATUS "AURIX target name     : ${AURIX_TARGET_NAME}")
message(STATUS "AURIX linker script   : ${AURIX_LINKER_SCRIPT}")
message(STATUS "AURIX source count    : ${AURIX_SOURCE_COUNT}")
message(STATUS "AURIX include count   : ${AURIX_INCLUDE_COUNT}")
message(STATUS "AURIX map output      : ${AURIX_MAP_FILE}")
message(STATUS "AURIX hex output      : ${AURIX_HEX_FILE}")