set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR tricore)

# 裸机目标缺少完整运行时，强制让 try_compile 只构建静态库，避免配置阶段链接探测失败。
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 工具链 bin 目录，可通过 -DAURIX_TOOLCHAIN_BIN=... 覆盖。
set(AURIX_TOOLCHAIN_BIN
    "C:/Infineon/AURIX-Studio-1.10.28/tools/Compilers/tricore-gcc11/bin"
    CACHE PATH "Path to the tricore-gcc11 bin directory"
)

# 供 CMakeLists.txt 区分工具链类型
set(AURIX_TOOLCHAIN_TYPE "GCC" CACHE STRING "Toolchain type: GCC or TASKING" FORCE)

file(TO_CMAKE_PATH "${AURIX_TOOLCHAIN_BIN}" AURIX_TOOLCHAIN_BIN)

set(_aurix_tool_prefix "${AURIX_TOOLCHAIN_BIN}/tricore-elf")

set(CMAKE_C_COMPILER   "${_aurix_tool_prefix}-gcc.exe")
set(CMAKE_CXX_COMPILER "${_aurix_tool_prefix}-g++.exe")
set(CMAKE_ASM_COMPILER "${_aurix_tool_prefix}-gcc.exe")
set(CMAKE_AR           "${_aurix_tool_prefix}-ar.exe")
set(CMAKE_RANLIB       "${_aurix_tool_prefix}-ranlib.exe")
set(CMAKE_OBJCOPY "${_aurix_tool_prefix}-objcopy.exe" CACHE FILEPATH "TriCore objcopy tool")
set(CMAKE_OBJDUMP "${_aurix_tool_prefix}-objdump.exe" CACHE FILEPATH "TriCore objdump tool")
set(CMAKE_SIZE    "${_aurix_tool_prefix}-size.exe"    CACHE FILEPATH "TriCore size tool")

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

# ------------------------------------------------------------------
# tricore-elf 的 ld.exe 不支持 @response 文件
# ------------------------------------------------------------------
# 现象：链接期报
#   ld.exe: cannot find @C:\WINDOWS\TEMP\ccXXXXXX: Invalid argument
#
# 原因：CMake 的 Ninja 生成器在对象文件较多时会把对象列表写进
#   CMakeFiles/<target>.rsp，并以 "@rsp" 形式传给 gcc；gcc 的 collect2
#   随后又生成自己的临时 @file 转交给 ld，而这个 ld 移植版不认 @file 语法。
#
# 处理：改写 CMAKE_C_LINK_EXECUTABLE，用 <OBJECTS> 直接展开对象列表，
#   不经过 response file。本工程展开后命令行约 21KB，低于 Windows
#   cmd.exe 的 32KB 上限，安全。
#
# 注意：编译阶段仍保留 response file（48 个 include 目录），
#   cc1.exe 能正确解析 @rsp，只有 ld.exe 不行。
set(CMAKE_C_USE_RESPONSE_FILE_FOR_OBJECTS   0)
set(CMAKE_C_USE_RESPONSE_FILE_FOR_LIBRARIES 0)
set(CMAKE_C_USE_RESPONSE_FILE_FOR_INCLUDES  0)
set(CMAKE_ASM_USE_RESPONSE_FILE_FOR_OBJECTS   0)
set(CMAKE_ASM_USE_RESPONSE_FILE_FOR_LIBRARIES 0)
set(CMAKE_ASM_USE_RESPONSE_FILE_FOR_INCLUDES  0)

# 编译阶段的 response file 已在 CMakeLists.txt（project() 之前）关闭，
# 展开后编译命令行约 3.6KB，安全。
#
# 但链接规则的 RSP_FILE 是 Ninja 生成器硬编码的，无法通过变量关闭，
# 因此链接改为经由 gcc-link-wrapper.cmake：它会把 "@xxx.rsp" 就地展开
# 成对象列表后再调用 gcc，从而绕开 ld.exe 不支持 @file 的限制。
set(_aurix_link_wrapper "${CMAKE_CURRENT_LIST_DIR}/gcc-link-wrapper.cmake")

set(CMAKE_C_LINK_EXECUTABLE
    "\"${CMAKE_COMMAND}\" -DGCC=<CMAKE_C_COMPILER> \"-DARGS=<FLAGS>|<CMAKE_C_LINK_FLAGS>|<LINK_FLAGS>|<OBJECTS>|-o|<TARGET>|<LINK_LIBRARIES>\" -P \"${_aurix_link_wrapper}\"")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
