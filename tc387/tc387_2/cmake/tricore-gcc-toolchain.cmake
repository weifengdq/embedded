set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR tricore)

# 裸机目标缺少完整运行时，强制让 try_compile 只构建静态库，避免配置阶段链接探测失败。
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 工具链 bin 目录，可通过 -DAURIX_TOOLCHAIN_BIN=... 覆盖。
# Linux 默认路径为 /opt/tricore-gcc/bin (符号链接到 /opt/tricore-gcc-13.4.1/bin)
# Windows 示例: C:/Infineon/AURIX-Studio-1.10.28/tools/Compilers/tricore-gcc11/bin
if(UNIX)
    set(_default_toolchain_bin "/opt/tricore-gcc/bin")
else()
    set(_default_toolchain_bin "C:/Infineon/AURIX-Studio-1.10.28/tools/Compilers/tricore-gcc11/bin")
endif()
set(AURIX_TOOLCHAIN_BIN
    "${_default_toolchain_bin}"
    CACHE PATH "Path to the tricore-gcc bin directory"
)

# 供 CMakeLists.txt 区分工具链类型
set(AURIX_TOOLCHAIN_TYPE "GCC" CACHE STRING "Toolchain type: GCC or TASKING" FORCE)

file(TO_CMAKE_PATH "${AURIX_TOOLCHAIN_BIN}" AURIX_TOOLCHAIN_BIN)

set(_aurix_tool_prefix "${AURIX_TOOLCHAIN_BIN}/tricore-elf")

if(WIN32)
    set(_aurix_exe_suffix ".exe")
else()
    set(_aurix_exe_suffix "")
endif()

set(CMAKE_C_COMPILER   "${_aurix_tool_prefix}-gcc${_aurix_exe_suffix}")
set(CMAKE_CXX_COMPILER "${_aurix_tool_prefix}-g++${_aurix_exe_suffix}")
set(CMAKE_ASM_COMPILER "${_aurix_tool_prefix}-gcc${_aurix_exe_suffix}")
set(CMAKE_AR           "${_aurix_tool_prefix}-ar${_aurix_exe_suffix}")
set(CMAKE_RANLIB       "${_aurix_tool_prefix}-ranlib${_aurix_exe_suffix}")
set(CMAKE_OBJCOPY "${_aurix_tool_prefix}-objcopy${_aurix_exe_suffix}" CACHE FILEPATH "TriCore objcopy tool")
set(CMAKE_OBJDUMP "${_aurix_tool_prefix}-objdump${_aurix_exe_suffix}" CACHE FILEPATH "TriCore objdump tool")
set(CMAKE_SIZE    "${_aurix_tool_prefix}-size${_aurix_exe_suffix}"    CACHE FILEPATH "TriCore size tool")

foreach(required_tool IN ITEMS
    CMAKE_C_COMPILER
    CMAKE_AR
    CMAKE_RANLIB
    CMAKE_OBJCOPY
    CMAKE_OBJDUMP
    CMAKE_SIZE
)
    if(NOT EXISTS "${${required_tool}}")
        message(FATAL_ERROR "Required TriCore GCC tool not found: ${${required_tool}}")
    endif()
endforeach()

set(CMAKE_EXECUTABLE_SUFFIX ".elf")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
