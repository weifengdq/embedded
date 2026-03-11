set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR tricore)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(AURIX_TOOLCHAIN_BIN
    "C:/Infineon/AURIX-Studio-1.10.28/tools/Compilers/tricore-gcc11/bin"
    CACHE PATH
    "Path to the tricore-gcc11 bin directory"
)

file(TO_CMAKE_PATH "${AURIX_TOOLCHAIN_BIN}" AURIX_TOOLCHAIN_BIN)

set(_aurix_tool_prefix "${AURIX_TOOLCHAIN_BIN}/tricore-elf")

set(CMAKE_C_COMPILER "${_aurix_tool_prefix}-gcc.exe")
set(CMAKE_CXX_COMPILER "${_aurix_tool_prefix}-g++.exe")
set(CMAKE_ASM_COMPILER "${_aurix_tool_prefix}-gcc.exe")
set(CMAKE_AR "${_aurix_tool_prefix}-ar.exe")
set(CMAKE_RANLIB "${_aurix_tool_prefix}-ranlib.exe")
set(CMAKE_OBJCOPY "${_aurix_tool_prefix}-objcopy.exe" CACHE FILEPATH "TriCore objcopy tool")
set(CMAKE_OBJDUMP "${_aurix_tool_prefix}-objdump.exe" CACHE FILEPATH "TriCore objdump tool")
set(CMAKE_SIZE "${_aurix_tool_prefix}-size.exe" CACHE FILEPATH "TriCore size tool")

foreach(required_tool IN ITEMS
    CMAKE_C_COMPILER
    CMAKE_AR
    CMAKE_RANLIB
    CMAKE_OBJCOPY
    CMAKE_OBJDUMP
    CMAKE_SIZE
)
    if(NOT EXISTS "${${required_tool}}")
        message(FATAL_ERROR "Required TriCore tool not found: ${${required_tool}}")
    endif()
endforeach()

set(CMAKE_EXECUTABLE_SUFFIX ".elf")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)