cmake_minimum_required(VERSION 3.24)
project(tc397_lwip_iperf LANGUAGES C ASM)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AurixProject.cmake")

# ---------- 可配置入口 ----------
set(AURIX_TARGET_NAME "${PROJECT_NAME}" CACHE STRING "Output target base name")
# GCC 使用 tc39xx, TASKING 使用 tc39xb（两者命名规则不同）
if(AURIX_TOOLCHAIN_TYPE STREQUAL "TASKING")
    set(_default_cpu "tc39xb")
else()
    set(_default_cpu "tc39xx")
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

# DTS driver for temperature (PMS)
list(APPEND AURIX_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/iLLD/TC3xx/Tricore/Dts/Dts/IfxDts_Dts.c"
)

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

# Letter-shell configuration
target_compile_definitions(${PROJECT_NAME} PRIVATE SHELL_CFG_USER="shell_cfg_user.h")

# GETH DMA 描述符数量（必须全局定义，保证 iLLD 与 lwIP port 编译单元一致）
# RX 环必须大于 TCP 窗口内的最大在途包数（44），否则线速突发下必然丢包，
# 触发 Go-Back-N + 客户端 RTO，吞吐崩溃。64 x 1536 = 96KB，放 LMU。
# NOTE 2026-09-18: TX 描述符试过 8 -> 32 想给 ACK 更多缓冲，实测吞吐反而崩到
#   ~4 Mbps（ping 正常但 RX 几乎收不到包）。本工程对 DMA 描述符/缓冲的
#   LMU 布局很敏感，未定位到根因前**不要**改这两个数字，保持 64/8。
target_compile_definitions(${PROJECT_NAME} PRIVATE
    IFXGETH_MAX_RX_DESCRIPTORS=64
    IFXGETH_MAX_TX_DESCRIPTORS=8
)

# ---------- 工具链特定选项 ----------
if(AURIX_TOOLCHAIN_TYPE STREQUAL "TASKING")
    # CMake 自动识别 Tasking 6.3 会在 CMAKE_C_FLAGS 中注入 "-O0 -g --iso=99 --strict"，
    # 会和 target_compile_options 中的相同选项重复导致 E204 错误，此处清空默认 flags，
    # 由 target_compile_options 统一接管所有编译选项。
    set(CMAKE_C_FLAGS   "" CACHE STRING "" FORCE)
    set(CMAKE_C_FLAGS_DEBUG   "" CACHE STRING "" FORCE)
    set(CMAKE_C_FLAGS_RELEASE "" CACHE STRING "" FORCE)

    # TASKING C 编译选项（与 ADS TriCore Debug(TASKING) 配置对应）。
    #
    # ===== 2026-09-18 测速根因复盘（为什么 Release 必须显式 -O3 + --tradeoff=0）=====
    # 现象：Tasking Release 只跑出 73.7Mbps，Ubuntu GCC Release（-O3）是 592Mbps，差 8 倍。
    # 根因 1（主因）：不写 -O 时 cctc 用默认优化级 = -O2，而 -O2 是"关内联"的。
    #   手册 ctc_user_guide.pdf p.446-447「--optimize (-O)」的预定义别名（大写字母=关闭该项）：
    #     -O0 = -OaCEFGIKLMNOPRSUVWY,-predict            （全关）
    #     -O2 = -OacefgIklMnoprsUvwy,+predict  ← 默认     I=inline 关、U=unroll 关、
    #                                                     M=simd 关、N=align-loop 关，
    #                                                     而小写 r=compact(反向内联) 反而开着
    #     -O3 = -Oacefgiklmnoprsuvwy,+predict            i/u/m/n 全开
    #   lwIP 热路径（tcp_input/tcp_output/pbuf/etharp/memcpy）是"深调用链 + 大量小函数"，
    #   关内联等于每个小函数都真调用一次，在 TriCore 上代价极大。
    # 根因 2：--tradeoff 不只调"大小/速度权重"，它直接改变指令选择（本目录小实验实测）：
    #     --tradeoff=0：循环用 TriCore 硬件 loop 指令（零开销循环）
    #     --tradeoff=4：循环退化成 add + jlt.u 的软件计数循环（每个循环多 2~3 条指令）
    #   手册 p.222-224「Optimize for Code Size or Execution Speed」也写明 LD.D/ST.D 只在
    #   tradeoff 0/1/2 才使用、loop 对齐只在 tradeoff<=2 才生效。
    #
    # 因此 Release 固定 -O3 + --tradeoff=0，与 tc387_lwip_iperf_gcc2 的 Tasking 千兆配置
    # （TC387 实测 ~945Mbps）完全一致；Debug 保持 --tradeoff=4（体积优先，配 -O0 好调试）。
    # GCC 分支在下面的 else 中，完全不受影响（Ubuntu 26 + GCC 基线保持）。
    #
    # ===== 上板实测（2026-09-18，TC397 + COM165 + 千兆转换盒）=====
    # iperf 客户端为 C:\github\embedded\tc397\tools\iperf-2.2.1-win64.exe，参数 `-w 1M`。
    #   Tasking -O3/tradeoff=0：30s 测试 90.9 / 279 Mbps（input=706 tk/pkt）
    #   Tasking --tradeoff=4   ：30s 测试 77.9 / 43.8 Mbps（input=803 tk/pkt）
    # -O3 的 input（lwIP 协议处理）每包快约 12%，峰值也明显更高，故选 -O3。
    # 单流 TCP 20s 分区间报告可见峰值 331 Mbps，但会周期性掉到 2~8 Mbps：
    #   那是**链路丢包**导致客户端 cwnd 崩溃（UDP 灌流实测 300M→4.6%、
    #   600M→14.5%、900M→19.1% 丢包，与编译器无关），不是 CPU 不够
    #   （掉速时 busy 仅 4~17%）。详见 README §7.6。
    target_compile_options(${PROJECT_NAME} PRIVATE
        -D__CPU__=${AURIX_CPU}
        --language=+volatile,+gcc
        --fp-model=3
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:--tradeoff=0>
        $<$<NOT:$<CONFIG:Release>>:--tradeoff=4>
        --compact-max-size=200
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
        # NOTE: NO -fdata-sections! TriCore GCC puts each static into a bare
        # .<sym> section (not .data.<sym>), which Lcf_Gnuc_Tricore_Tc.lsl does
        # not collect into .data/.bss and not into copy/clear tables -> the
        # variables would keep power-on garbage (shellList wild pointers hung
        # the shell). Keep -ffunction-sections (code needs no init).
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
