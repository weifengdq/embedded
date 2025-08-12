# Copyright (c) 2021 HPMicro
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.13)

set(CUSTOM_GCC_LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linkers/gcc/user_linker.ld)
# set(CUSTOM_SES_LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linkers/segger/user_linker.icf)
# set(CUSTOM_IAR_LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linkers/iar/user_linker.icf)

set(CONFIG_CHERRYRB 1)
set(CONFIG_CHERRYUSB 1)
set(CONFIG_USB_DEVICE 1)
set(CONFIG_USB_DEVICE_CDC 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})

project(hpm4slcanfd)

# 调试
# set(SLCAN_DEBUG 1)

# 确保 usb_config.h 能被 CherryUSB 找到
sdk_inc(inc)

sdk_app_inc(inc)
sdk_app_src(src/main.c)
sdk_app_src(src/cdc_acm.c)
sdk_app_src(src/slcan.c)
sdk_app_src(src/mcan.c)

generate_ide_projects()
