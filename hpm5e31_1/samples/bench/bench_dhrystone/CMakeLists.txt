# bench_dhrystone: Dhrystone 2.1 (DMIPS/MHz) 基准
#
# 官方 dhry_1.c / dhry_2.c 直接取 HPM SDK samples/dhrystone/src/
# （不复制进仓库；用编译单元包装把它的 main() 改名，便于同一固件多轮运行）
cmake_minimum_required(VERSION 3.13)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_bench_dhrystone)

if(NOT DEFINED BENCH_OPT)
    set(BENCH_OPT "-O3")
endif()
if(NOT DEFINED BENCH_RUNS)
    set(BENCH_RUNS 3)
endif()
if(NOT DEFINED BENCH_LOOPS)
    set(BENCH_LOOPS 10000000)
endif()

sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_compile_definitions(-DBENCH_OPT_STR="${BENCH_OPT}")
sdk_compile_definitions(-DBENCH_RUNS=${BENCH_RUNS})
sdk_compile_definitions(-DLOOP_COUNT=${BENCH_LOOPS})

# 与 HPM SDK samples/dhrystone 保持一致的测试规则：关闭内联（Dhrystone 可比性要求），
# 并允许 K&R 风格原型（dhry_1.c 是老代码）
sdk_compile_options(${BENCH_OPT})
sdk_compile_options(-fno-inline -fmessage-length=0)
sdk_compile_options(-Wno-implicit-int -Wno-implicit-function-declaration -Wno-return-type)
sdk_compile_options(-Wno-deprecated-non-prototype -Wno-strict-prototypes)
sdk_compile_options(-fno-builtin-printf)

set(_wrap "${CMAKE_CURRENT_BINARY_DIR}/dhry_wrap")
file(MAKE_DIRECTORY "${_wrap}")
file(WRITE "${_wrap}/cm_dhry_1.c"
     "/* auto-generated: rename official main() and compile dhry_1.c */\n#define main dhrystone_main_unused\n#include \"dhry_1.c\"\n")
file(WRITE "${_wrap}/cm_dhry_2.c"
     "/* auto-generated: compile dhry_2.c */\n#include \"dhry_2.c\"\n")

sdk_app_inc(src ${HPM_SDK_BASE}/samples/dhrystone/src)
sdk_app_src(src/main.c "${_wrap}/cm_dhry_1.c" "${_wrap}/cm_dhry_2.c")

message(STATUS "bench_dhrystone: BENCH_OPT=${BENCH_OPT} BENCH_LOOPS=${BENCH_LOOPS} BENCH_RUNS=${BENCH_RUNS}")
