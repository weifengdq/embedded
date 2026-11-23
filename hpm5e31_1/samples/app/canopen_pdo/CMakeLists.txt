cmake_minimum_required(VERSION 3.13)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_canopen_pdo)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)

set(CANOPEN_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../components/canopen_node)
set(APP_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_canopen)
include(${CANOPEN_DIR}/canopen.cmake)

sdk_inc(${CANOPEN_INCS} ${APP_DIR})
sdk_app_src(src/main.c ${CANOPEN_SRCS} ${APP_DIR}/co_master.c)
