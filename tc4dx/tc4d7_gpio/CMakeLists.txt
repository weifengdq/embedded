cmake_minimum_required(VERSION 3.24)

# tc4d7_gpio 的顶层构建脚本。
# 这个文件负责三件事：
# 1. 定义工程与默认构建类型；
# 2. 调用本地的 AURIX 文件收集逻辑，递归收集源码/头文件/包含目录；
# 3. 为 TriCore GCC 目标补齐编译、链接、hex 生成和 size 输出规则。
project(tc4d7_gpio LANGUAGES C)

# 如果用户没有显式指定 CMAKE_BUILD_TYPE，则默认走 Debug。
# 对嵌入式工程来说，这样可以避免第一次配置时直接生成高优化版本，
# 方便先完成调试和符号验证。
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# 显式限制可选构建类型，便于 VS Code/CMake Tools 等前端展示下拉选项。
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

# 引入本工程自定义的 AURIX 辅助函数：
# - 归一化路径
# - 判断排除目录/文件
# - 递归收集工程源码
# - 从 ADS 的 .cproject 中提取 excluding 规则
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AurixProject.cmake")

# 下面这些 CACHE 变量构成了工程最核心的可配置入口：
# - AURIX_TARGET_NAME: 最终输出文件名基名
# - AURIX_CPU: 传给 -mcpu 的 TriCore 变体
# - AURIX_LINKER_SCRIPT: 链接脚本路径
# - AURIX_CPROJECT_FILE: 用来同步 ADS 排除规则的 .cproject
set(AURIX_TARGET_NAME "${PROJECT_NAME}" CACHE STRING "Output target base name")
set(AURIX_CPU "tc4DAx" CACHE STRING "TriCore CPU variant passed to -mcpu")
set(AURIX_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/Lcf_Gcc_Tricore_Tc.lsl" CACHE FILEPATH "Linker script")
set(AURIX_CPROJECT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.cproject" CACHE FILEPATH "ADS .cproject used to import source exclusion rules")

# 这些目录默认不参加递归扫描：
# - build: CMake/Ninja 输出目录
# - TriCore Debug/Release (GCC): ADS 传统构建输出目录
# 这样可以避免把构建产物目录再次当作源目录扫描进去。
set(AURIX_EXCLUDED_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/build"
    "${CMAKE_CURRENT_SOURCE_DIR}/TriCore Debug (GCC)"
    "${CMAKE_CURRENT_SOURCE_DIR}/TriCore Release (GCC)"
    CACHE STRING "Semicolon-separated directories excluded from recursive source scan"
)
set(AURIX_EXCLUDED_FILES "" CACHE STRING "Semicolon-separated files excluded from recursive source scan")

# 从 ADS .cproject 中解析 sourceEntries/excluding 规则。
# 这样做的目的，是让 CMake 侧和 ADS 侧尽量保持一致，避免某些 ADS 明确排除的目录
# 在 CMake 中又被错误地收进来，最终造成重复编译或者把不该编译的目录编进目标文件。
aurix_extract_cproject_excluded_dirs(
    CPROJECT_FILE "${AURIX_CPROJECT_FILE}"
    ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    OUT_EXCLUDED_DIRS AURIX_ADS_EXCLUDED_DIRS
)

# 合并“手工排除目录”和“ADS 导入排除目录”，再去重，形成最终生效排除集。
set(AURIX_EFFECTIVE_EXCLUDED_DIRS ${AURIX_EXCLUDED_DIRS} ${AURIX_ADS_EXCLUDED_DIRS})
list(REMOVE_DUPLICATES AURIX_EFFECTIVE_EXCLUDED_DIRS)

# 链接脚本是裸机工程必备输入，缺失时直接终止配置，避免后续报出更难读的链接错误。
if(NOT EXISTS "${AURIX_LINKER_SCRIPT}")
    message(FATAL_ERROR "Linker script not found: ${AURIX_LINKER_SCRIPT}")
endif()

# 递归收集工程内所有参与编译的 .c/.h 和 include 目录。
# 注意：这里采用“扫描整个工程目录 + 排除规则”的方式，而不是手写源文件列表，
# 优点是复制工程后维护成本低，缺点是必须保证排除规则足够准确。
aurix_collect_project_files(
    ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    EXCLUDED_DIRS "${AURIX_EFFECTIVE_EXCLUDED_DIRS}"
    EXCLUDED_FILES "${AURIX_EXCLUDED_FILES}"
    OUT_SOURCES AURIX_SOURCE_FILES
    OUT_HEADERS AURIX_HEADER_FILES
    OUT_INCLUDE_DIRS AURIX_INCLUDE_DIRS
)

# 裸机工程至少要有一个 C 源文件，否则没有可构建目标，直接报错更直观。
if(NOT AURIX_SOURCE_FILES)
    message(FATAL_ERROR "No C source files were found after applying exclusions.")
endif()

# 使用收集到的源文件和头文件生成 ELF 可执行目标。
# 头文件显式加入目标，主要是为了让 IDE/生成器更容易感知到这些文件属于该 target。
add_executable(${PROJECT_NAME} ${AURIX_SOURCE_FILES} ${AURIX_HEADER_FILES})

# 设置输出属性：
# - OUTPUT_NAME 控制最终文件基名
# - SUFFIX 统一为 .elf
# - C_STANDARD 约束工程使用 C99
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${AURIX_TARGET_NAME}"
    SUFFIX ".elf"
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

# 把递归扫描得到的所有目录都加入 include path。
# 这是这套脚本“零维护收集”的核心之一：新增模块后通常不需要再手工补 include 目录。
target_include_directories(${PROJECT_NAME} PRIVATE ${AURIX_INCLUDE_DIRS})

# 编译选项分两部分：
# 1. 配置相关选项：Debug/Release/RelWithDebInfo/MinSizeRel
# 2. 通用裸机选项：节级别裁剪、严格 volatile 位域、目标 CPU 等
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

# 把“当前构建类型实际对应的优化参数”写入宏，便于代码中打印或诊断构建配置。
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:AURIX_COMPILER_FLAGS="-Og -g3">
    $<$<CONFIG:Release>:AURIX_COMPILER_FLAGS="-O3 -DNDEBUG">
    $<$<CONFIG:RelWithDebInfo>:AURIX_COMPILER_FLAGS="-O2 -g -DNDEBUG">
    $<$<CONFIG:MinSizeRel>:AURIX_COMPILER_FLAGS="-Os -DNDEBUG">
)

# map 文件和 hex 文件路径都绑定在当前 binary dir，便于区分不同 build type 的输出。
set(AURIX_MAP_FILE "${CMAKE_CURRENT_BINARY_DIR}/${AURIX_TARGET_NAME}.map")
set(AURIX_HEX_FILE "${CMAKE_CURRENT_BINARY_DIR}/${AURIX_TARGET_NAME}.hex")

# 链接阶段关键选项说明：
# -T<lsl>: 指定 TriCore 链接脚本
# -nocrt0: 不使用默认 C 运行时启动文件，启动流程由工程自带启动代码负责
# --gc-sections: 配合 -fdata-sections/-ffunction-sections 删除未引用节
# -Map: 生成 map 文件用于分析地址布局和占用情况
target_link_options(${PROJECT_NAME} PRIVATE
    -T${AURIX_LINKER_SCRIPT}
    -nocrt0
    -mcpu=${AURIX_CPU}
    -Wl,--gc-sections
    -Wl,-Map,${AURIX_MAP_FILE}
)

# 生成 ihex 文件，常用于烧录工具下载。
# 这里不直接覆盖 ELF，而是通过自定义命令从 ELF 派生生成 hex。
add_custom_command(
    OUTPUT "${AURIX_HEX_FILE}"
    COMMAND ${CMAKE_OBJCOPY} -O ihex "$<TARGET_FILE:${PROJECT_NAME}>" "${AURIX_HEX_FILE}"
    DEPENDS ${PROJECT_NAME}
    COMMENT "Generating ${AURIX_TARGET_NAME}.hex"
    VERBATIM
)

# 把 hex 产物声明成默认 ALL 目标的一部分，这样普通构建也会顺带得到 .hex。
add_custom_target(${PROJECT_NAME}_hex ALL DEPENDS "${AURIX_HEX_FILE}")

# 提供一个显式 size 目标，用来打印 ELF 的节大小汇总，便于快速观察资源占用。
add_custom_target(${PROJECT_NAME}_size
    COMMAND ${CMAKE_SIZE} --format=berkeley "$<TARGET_FILE:${PROJECT_NAME}>"
    DEPENDS ${PROJECT_NAME}
    COMMENT "Printing ${AURIX_TARGET_NAME}.elf size"
    VERBATIM
)

# 统计信息仅用于配置阶段输出，让使用者快速确认扫描结果是否合理。
list(LENGTH AURIX_SOURCE_FILES AURIX_SOURCE_COUNT)
list(LENGTH AURIX_INCLUDE_DIRS AURIX_INCLUDE_COUNT)

# 配置摘要：这几行经常是排查“为什么没扫到文件 / 为什么链接脚本不对”的第一现场。
message(STATUS "AURIX target name     : ${AURIX_TARGET_NAME}")
message(STATUS "AURIX linker script   : ${AURIX_LINKER_SCRIPT}")
message(STATUS "AURIX source count    : ${AURIX_SOURCE_COUNT}")
message(STATUS "AURIX include count   : ${AURIX_INCLUDE_COUNT}")
message(STATUS "AURIX map output      : ${AURIX_MAP_FILE}")
message(STATUS "AURIX hex output      : ${AURIX_HEX_FILE}")