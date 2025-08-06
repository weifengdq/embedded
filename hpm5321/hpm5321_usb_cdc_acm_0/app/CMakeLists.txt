# Copyright (c) 2021 HPMicro
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.13)

set(CUSTOM_GCC_LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linkers/gcc/user_linker.ld)
# set(CUSTOM_SES_LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linkers/segger/user_linker.icf)
# set(CUSTOM_IAR_LINKER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/linkers/iar/user_linker.icf)

set(CONFIG_CHERRYUSB 1)
set(CONFIG_USB_DEVICE 1)
set(CONFIG_USB_DEVICE_CDC_ACM 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})

project(hpm5321_usb_cdc_acm_0)

# Include USB config directory (contains usb_config.h)
sdk_inc(src)
sdk_app_inc(inc)
sdk_app_src(src/main.c)
sdk_app_src(src/cdc_acm.c)

# # Enable USB device support
# sdk_compile_definitions(-DCONFIG_USB_DEVICE)
# sdk_compile_definitions(-DCONFIG_USB_DEVICE_CDC_ACM)

# # Include CherryUSB
# sdk_inc(../../sdk_env_v1.10.0/hpm_sdk/middleware/cherryusb/common)
# sdk_inc(../../sdk_env_v1.10.0/hpm_sdk/middleware/cherryusb/core)
# sdk_inc(../../sdk_env_v1.10.0/hpm_sdk/middleware/cherryusb/class/cdc)

# # Add CherryUSB sources
# sdk_src(../../sdk_env_v1.10.0/hpm_sdk/middleware/cherryusb/core/usbd_core.c)
# sdk_src(../../sdk_env_v1.10.0/hpm_sdk/middleware/cherryusb/class/cdc/usbd_cdc_acm.c)
# sdk_src(../../sdk_env_v1.10.0/hpm_sdk/soc/HPM5300/toolchains/usb/hpm_usbd.c)

generate_ide_projects()
