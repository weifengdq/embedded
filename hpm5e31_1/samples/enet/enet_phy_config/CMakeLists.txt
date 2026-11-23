cmake_minimum_required(VERSION 3.13)
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_enet_phy_config)

set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

set(NET2_COMPONENT ${CMAKE_CURRENT_LIST_DIR}/../../../components/enet_phy_yt8011)

sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
sdk_inc(${NET2_COMPONENT}/inc)
sdk_app_src(${NET2_COMPONENT}/src/hpm_yt8011.c)
sdk_app_src(src/main.c)
