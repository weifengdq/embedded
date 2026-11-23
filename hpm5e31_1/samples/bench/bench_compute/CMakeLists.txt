# bench_compute: FPU / DSP / 位运算吞吐基准（含 ILM vs XIP 代码布局对比）
#
#   -DBENCH_FPU=1  启用硬件单精度 FPU（-march=rv32imafdc -mabi=ilp32f）；
#                  默认 0 时按板级默认 rv32imac + zba/zbb/zbc/zbs（软浮点）编译
#   -DBENCH_OPT    优化级别
cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED BENCH_FPU)
    set(BENCH_FPU 0)
endif()
if(BENCH_FPU)
    # 必须在 find_package(hpm-sdk) 之前设置：toolchain.cmake 用它们生成 -march/-mabi
    set(RV_ARCH "rv32imafc")
    set(RV_ABI "ilp32f")
    # a) 不要追加 zba/zbb/zbc/zbs：工具链的 ilp32f multilib 只按 -march=rv32imafc 精确匹配
    set(HPM_SOC_DISABLE_B_EXT 1)
endif()

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_bench_compute)

if(NOT DEFINED BENCH_OPT)
    set(BENCH_OPT "-O3")
endif()

sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_compile_definitions(-DBENCH_OPT_STR="${BENCH_OPT}")
sdk_compile_definitions(-DBENCH_FPU=${BENCH_FPU})
sdk_compile_options(${BENCH_OPT})

if(BENCH_FPU)
    # b) 链接期再显式给一次精确 -march/-mabi：multilib 选择只看最后一次 -march，
    #    否则会退化成 soft-float multilib，报 "can't link soft-float modules with single-float modules"
    sdk_ld_options("-march=rv32imafc")
    sdk_ld_options("-mabi=ilp32f")
endif()

# sinf/sqrtf 需要 libm（newlib-nano 默认不带数学库的强符号）
sdk_ld_options("-lm")

sdk_app_inc(src)
sdk_app_src(src/main.c)
