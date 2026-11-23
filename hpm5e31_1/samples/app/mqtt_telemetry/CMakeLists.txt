cmake_minimum_required(VERSION 3.13)

# mqtt_telemetry —— MQTT 3.1.1 遥测上报 + 下行命令（板 192.168.2.100 -> 主机 broker 192.168.2.1:1883）
# 网络底座：components/app_lwip（ENET RGMII + YT8011AN + 静态 IP + 1 ms 时基）
# 协议：components/app_mqtt（裸机 lwIP raw TCP 上的最小 MQTT 客户端）
set(CONFIG_LWIP 1)
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_mqtt_telemetry)

# 共享底座：lwIP 端口 + ENET/PHY + 静态 IP/时基（必须在 find_package 之后 include）
include(${REPO_ROOT}/components/app_lwip/app_lwip.cmake)

# ENET TX CRC 模式：0 = append（MAC 在帧尾追加 4 B FCS，不回写发送缓冲）。
# 与第 35/36 章一致：replace 模式下连续大帧会踩坏相邻 pbuf（见第 35 章 U-35-10）。
sdk_compile_definitions(-DENET_TX_HW_CRC_MODE=0)

# app_mqtt 组件（MQTT 3.1.1 客户端）
include(${REPO_ROOT}/components/app_mqtt/app_mqtt.cmake)

# app_can 共享组件（CAN0 发遥测帧 / CAN1 收，板内两两相连）
sdk_inc(${REPO_ROOT}/components/app_can/inc)
sdk_app_src(${REPO_ROOT}/components/app_can/src/app_can.c)

sdk_app_src(src/main.c)
