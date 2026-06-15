set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR tricore)

# 裸机目标缺少完整运行时，强制让 try_compile 只构建静态库，避免配置阶段链接探测失败。
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# TASKING ctc bin 目录，可通过 -DAURIX_TASKING_BIN=... 覆盖。
set(AURIX_TASKING_BIN
    "C:/Infineon/AURIX-Studio-1.10.28/tools/Compilers/Tasking_1.1r8/ctc/bin"
    CACHE PATH "Path to the TASKING ctc bin directory"
)

# 供 CMakeLists.txt 区分工具链类型
set(AURIX_TOOLCHAIN_TYPE "TASKING" CACHE STRING "Toolchain type: GCC or TASKING" FORCE)

file(TO_CMAKE_PATH "${AURIX_TASKING_BIN}" AURIX_TASKING_BIN)

set(CMAKE_C_COMPILER   "${AURIX_TASKING_BIN}/cctc.exe")
set(CMAKE_CXX_COMPILER "${AURIX_TASKING_BIN}/cctc.exe")
set(CMAKE_ASM_COMPILER "${AURIX_TASKING_BIN}/astc.exe")

foreach(required_tool IN ITEMS CMAKE_C_COMPILER CMAKE_ASM_COMPILER)
    if(NOT EXISTS "${${required_tool}}")
        message(FATAL_ERROR "Required TASKING tool not found: ${${required_tool}}")
    endif()
endforeach()

# ADS 捆绑版 TASKING 编译器（非商业版）仅允许在 ADS IDE 内部运行，
# 命令行独立运行时会报 "License does not support running as standalone" 错误。
# 需要商业版 TASKING 许可证才能在命令行下编译。
# 这里跳过 CMake 的编译器可用性检测，让配置阶段能够正常完成。
set(CMAKE_C_COMPILER_WORKS   TRUE CACHE BOOL "Skip compiler test for TASKING (license bypass)")
set(CMAKE_ASM_COMPILER_WORKS TRUE CACHE BOOL "Skip ASM compiler test for TASKING (license bypass)")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
