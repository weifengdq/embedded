# bench_bus: 总线/存储吞吐基准（XPI flash 直读带宽 + UART 实际吞吐 + 总线时钟）
cmake_minimum_required(VERSION 3.13)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_bench_bus)

if(NOT DEFINED BENCH_OPT)
    set(BENCH_OPT "-O3")
endif()

sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_compile_definitions(-DBENCH_OPT_STR="${BENCH_OPT}")
sdk_compile_options(${BENCH_OPT})

sdk_app_inc(src)
sdk_app_src(src/main.c)
