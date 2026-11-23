cmake_minimum_required(VERSION 3.13)

# CherryUSB device stack (CDC ACM)
set(CONFIG_CHERRYUSB 1)
set(CONFIG_USB_DEVICE 1)
set(CONFIG_USB_DEVICE_CDC_ACM 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_usb_cdc_acm_echo)

sdk_inc(${HPM_SDK_BASE}/samples/cherryusb/config)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_app_src(src/main.c)
sdk_app_src(src/cdc_acm.c)
