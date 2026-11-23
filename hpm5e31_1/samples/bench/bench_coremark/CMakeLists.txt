# bench_coremark: CoreMark 1.0 基准（HPM5E31 @480MHz）
#
# 变体通过 CMake 变量选择（不改源码）：
#   -DBENCH_OPT="-O2|-O3"   编译优化级别（默认 -O3）
#   -DBENCH_EXTRA=""        追加编译选项（默认空；可传 HPM SDK 的 coremark 激进参数）
#   -DBENCH_DATA=dlm|sram   CoreMark 数据块位置：dlm(默认, 栈在 DLM) / sram(MEM_STATIC, .bss 在 AXI SRAM)
#   -DBENCH_RUNS=3          每次上电自动重复运行次数
#
# 官方 CoreMark 源码直接取自 HPM SDK（只读参考，不复制进仓库）:
#   $HPM_SDK_BASE/middleware/coremark/
cmake_minimum_required(VERSION 3.13)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)

project(hpm5e31_1_bench_coremark)

if(NOT DEFINED BENCH_OPT)
    set(BENCH_OPT "-O3")
endif()
if(NOT DEFINED BENCH_EXTRA)
    set(BENCH_EXTRA "")
endif()
if(NOT DEFINED BENCH_DATA)
    set(BENCH_DATA "dlm")
endif()
if(NOT DEFINED BENCH_RUNS)
    set(BENCH_RUNS 3)
endif()

sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_compile_definitions(-DBENCH_RUNS=${BENCH_RUNS})
sdk_compile_definitions(-DBENCH_OPT_STR="${BENCH_OPT}")

# 优化选项：追加在 SDK 默认选项之后（GCC 以最后一个 -O 为准）
sdk_compile_options(${BENCH_OPT})
if(NOT "${BENCH_EXTRA}" STREQUAL "")
    sdk_compile_options(${BENCH_EXTRA})
endif()

if("${BENCH_DATA}" STREQUAL "sram")
    # CoreMark 用 MEM_STATIC 把数据块放进 .bss（AXI SRAM, 走 L1 D-Cache）
    sdk_compile_definitions(-DMEM_METHOD=MEM_STATIC -DBENCH_DATA_SRAM=1)
else()
    # CoreMark 用 MEM_STACK，栈在 DLM(0x00200000)，见 SoC linker script .stack
    sdk_compile_definitions(-DBENCH_DATA_DLM=1)
endif()

# CoreMark 报告里打印的编译器选项串（可复现性证据）
sdk_compile_definitions(COMPILER_FLAGS="gcc-11.1.0 ${BENCH_OPT} ${BENCH_EXTRA}")

sdk_app_inc(src ${HPM_SDK_BASE}/middleware/coremark)
sdk_app_src(src/main.c src/core_portme.c)

# ---- CoreMark 官方源文件的"编译单元包装" ----
# 包装成独立 TU：core_main.c 的 main() 需改名 coremark_main()，
# 这样同一个固件里可以连续跑多轮（原始 core_main.c 只在启动时跑一次）。
set(_wrap "${CMAKE_CURRENT_BINARY_DIR}/coremark_wrap")
file(MAKE_DIRECTORY "${_wrap}")
set(_cm_algo core_list_join core_matrix core_state core_util)
foreach(_s ${_cm_algo})
    file(WRITE "${_wrap}/cm_${_s}.c"
         "/* auto-generated: CoreMark ${_s}.c wrapper (source in HPM SDK) */\n#include \"${_s}.c\"\n")
    sdk_app_src("${_wrap}/cm_${_s}.c")
endforeach()
file(WRITE "${_wrap}/cm_core_main.c"
         "/* auto-generated: rename official main() to coremark_main() */\n#define main coremark_main\n#include \"core_main.c\"\n")
sdk_app_src("${_wrap}/cm_core_main.c")

message(STATUS "bench_coremark: BENCH_OPT=${BENCH_OPT} BENCH_DATA=${BENCH_DATA} BENCH_RUNS=${BENCH_RUNS}")
