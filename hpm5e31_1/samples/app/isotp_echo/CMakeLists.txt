cmake_minimum_required(VERSION 3.13)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_isotp_echo)

sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_compile_options(-O2)

set(APP_ISOTP_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_isotp)
sdk_inc(${APP_ISOTP_DIR}/include)
sdk_app_src(src/main.c
            ${APP_ISOTP_DIR}/src/app_isotp.c
            ${APP_ISOTP_DIR}/src/app_isotp_port.c)
