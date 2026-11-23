cmake_minimum_required(VERSION 3.13)
set(CONFIG_HPM_SPI 1)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_lan8651_spi_probe)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_inc(${CMAKE_CURRENT_LIST_DIR}/../../../components/oa_tc6)
sdk_app_src(src/main.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/oa_tc6/oa_tc6.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/oa_tc6/lan865x.c)
