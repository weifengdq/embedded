cmake_minimum_required(VERSION 3.13)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_j1939_dm)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_inc(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/inc)
sdk_app_src(src/main.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_id.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_stack.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_tp.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_dm.c
            ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_j1939/src/j1939_port_mcan.c)
