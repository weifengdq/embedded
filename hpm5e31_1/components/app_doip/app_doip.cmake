# app_doip.cmake - 把 DoIP(ISO 13400-2) 服务端组件接入一个 hpm-sdk 例程
#
# 用法（在 find_package(hpm-sdk) 与 components/app_lwip/app_lwip.cmake 之后）：
#   include(${REPO_ROOT}/components/app_doip/app_doip.cmake)
#
# 依赖：
#   * lwIP（CONFIG_LWIP=1，由 app_lwip.cmake 打开）
#   * app_lwip 组件（提供 app_udp_socket() 与 app_lwip_now_ms() 时基）
#   * 应用层的 UDS（可选）：本组件只通过 cfg.diag_rx 回调转发，不直接依赖 app_uds
#
# 静态内存：APP_DOIP_MAX_CONN x APP_DOIP_MAX_PAYLOAD = 3 x 4096 = 12 KB（每连接的接收缓冲）。
set(APP_DOIP_COMPONENT_DIR ${CMAKE_CURRENT_LIST_DIR})

sdk_inc(${APP_DOIP_COMPONENT_DIR}/include)
sdk_app_src(${APP_DOIP_COMPONENT_DIR}/src/app_doip.c
            ${APP_DOIP_COMPONENT_DIR}/src/app_doip_wire.c)
