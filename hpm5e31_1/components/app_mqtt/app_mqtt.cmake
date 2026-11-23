# app_mqtt.cmake - 把 MQTT 3.1.1 客户端组件接入一个 hpm-sdk 例程
#
# 依赖：lwIP（CONFIG_LWIP=1）与 components/app_lwip（提供 1 ms 时基 sys_now）。
# 用法（在 include(app_lwip.cmake) 之后）：
#   include(${REPO_ROOT}/components/app_mqtt/app_mqtt.cmake)
#
# 说明：组件本身只依赖 lwIP raw TCP API，不依赖 ENET 驱动细节；
#       app_lwip 负责 ENET/PHY/静态 IP/时基，本文件只加编译单元与头文件路径。

set(APP_MQTT_COMPONENT_DIR ${CMAKE_CURRENT_LIST_DIR})

sdk_inc(${APP_MQTT_COMPONENT_DIR}/inc)
sdk_app_src(${APP_MQTT_COMPONENT_DIR}/src/app_mqtt.c)
