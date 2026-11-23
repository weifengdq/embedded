cmake_minimum_required(VERSION 3.13)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_xcp_can_cal)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_inc(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_xcp)
sdk_app_src(src/main.c
            src/xcp_test_master_cal.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_xcp/xcp_core.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_xcp/xcp_port_can.c)
