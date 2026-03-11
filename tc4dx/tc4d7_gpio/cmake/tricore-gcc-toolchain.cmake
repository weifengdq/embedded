# TriCore GCC 交叉编译工具链文件。
# 这个文件的职责是告诉 CMake：
# - 当前不是主机原生构建，而是 Generic + tricore 目标；
# - 编译器、ar、ranlib、objcopy、objdump、size 分别在哪里；
# - 查找程序/库/头文件时不要误用主机系统路径策略。
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR tricore)

# 对交叉编译工程，CMake 的 try_compile 默认会尝试生成可执行文件。
# 裸机目标往往缺少完整运行时环境，因此这里强制让 try_compile 仅构建静态库，
# 以避免配置阶段因为链接探测失败而中断。
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 允许用户通过 cache 覆盖工具链 bin 目录。
# 默认值指向本机 AURIX Studio 1.10.28 自带的 tricore-gcc11 工具链。
set(AURIX_TOOLCHAIN_BIN
    "C:/Infineon/AURIX-Studio-1.10.28/tools/Compilers/tricore-gcc11/bin"
    CACHE PATH
    "Path to the tricore-gcc11 bin directory"
)

# 统一路径格式，避免 Windows 反斜杠在后续字符串拼接里引入转义/比较问题。
file(TO_CMAKE_PATH "${AURIX_TOOLCHAIN_BIN}" AURIX_TOOLCHAIN_BIN)

# TriCore GCC 工具名基本都共享 tricore-elf 前缀，后续按这个前缀拼接各个工具。
set(_aurix_tool_prefix "${AURIX_TOOLCHAIN_BIN}/tricore-elf")

# 设置 C/C++/ASM 编译器与 binutils 路径。
# 对本工程而言，真正关键的是 C 编译器、ar、ranlib、objcopy、objdump、size；
# CXX_COMPILER 也一并设置，方便后续扩展到 C++ 文件。
set(CMAKE_C_COMPILER "${_aurix_tool_prefix}-gcc.exe")
set(CMAKE_CXX_COMPILER "${_aurix_tool_prefix}-g++.exe")
set(CMAKE_ASM_COMPILER "${_aurix_tool_prefix}-gcc.exe")
set(CMAKE_AR "${_aurix_tool_prefix}-ar.exe")
set(CMAKE_RANLIB "${_aurix_tool_prefix}-ranlib.exe")
set(CMAKE_OBJCOPY "${_aurix_tool_prefix}-objcopy.exe" CACHE FILEPATH "TriCore objcopy tool")
set(CMAKE_OBJDUMP "${_aurix_tool_prefix}-objdump.exe" CACHE FILEPATH "TriCore objdump tool")
set(CMAKE_SIZE "${_aurix_tool_prefix}-size.exe" CACHE FILEPATH "TriCore size tool")

# 配置阶段立刻检查关键工具是否存在。
# 这样一旦用户机器没装工具链或者路径配置错了，会在最早阶段收到清晰报错。
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

# 裸机 TriCore 的最终可执行文件通常输出为 .elf，显式声明后更符合用户预期。
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

# 关闭 root path 模式，避免 CMake 在交叉编译场景下替我们重写程序/库/头文件搜索逻辑。
# 对这种“工具链和依赖都由工程自己控制”的裸机项目，保持 NEVER 通常最可控。
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)