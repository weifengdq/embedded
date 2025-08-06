# Copyright (c) 2021 HPMicro
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.13)

set(CUSTOM_GCC_LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linkers/gcc/user_linker.ld)
# set(CUSTOM_SES_LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linkers/segger/user_linker.icf)
# set(CUSTOM_IAR_LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linkers/iar/user_linker.icf)


find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})

project(hpm_slcanfd_x4)

sdk_app_inc(inc)
sdk_app_src(src/main.c)
sdk_app_src(src/bsp_gpio.c)

generate_ide_projects()
