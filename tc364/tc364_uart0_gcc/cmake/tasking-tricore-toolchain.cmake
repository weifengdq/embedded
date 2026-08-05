set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR tricore)

# 裸机目标缺少完整运行时，强制让 try_compile 只构建静态库，避免配置阶段链接探测失败。
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# TASKING ctc bin 目录，可通过 -DAURIX_TASKING_BIN=... 覆盖。
# 默认指向商业版 TASKING TriCore v6.3r1 安装路径。
set(AURIX_TASKING_BIN
    "C:/z/app/TASKING/TriCore_v6.3r1/ctc/bin"
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

# TASKING 工具链需要目标专用链接脚本才能通过 CMake 的可执行文件链接检测，
# 跳过检测可避免配置阶段因缺少链接脚本而报错。
set(CMAKE_C_COMPILER_WORKS   TRUE CACHE BOOL "Skip compiler link test for TASKING bare-metal target")
set(CMAKE_ASM_COMPILER_WORKS TRUE CACHE BOOL "Skip ASM compiler test for TASKING bare-metal target")

# 只扫描项目内部文件的依赖，避免 cmake_depends 尝试扫描 TASKING 系统头文件时报错。
set(CMAKE_DEPENDS_IN_PROJECT_ONLY ON)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
