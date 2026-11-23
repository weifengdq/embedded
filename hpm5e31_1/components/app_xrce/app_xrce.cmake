# app_xrce.cmake —— components/app_xrce 的构建片段
#
# 用法（在每个 samples/app/*/CMakeLists.txt 里，且必须在 find_package(hpm-sdk) 之后）：
#   set(APP_XRCE_COMPONENT ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_xrce)
#   include(${APP_XRCE_COMPONENT}/app_xrce.cmake)
#   app_xrce_configure()
#
# 为什么不用 file(GLOB)：microxrcedds_client 的 src/c 下有 Posix/FreeRTOS/Windows 的
# 传输实现，裸机不能编译；这里显式列出需要的文件（custom transport + 核心 + microcdr）。

set(APP_XRCE_DIR ${CMAKE_CURRENT_LIST_DIR})
set(APP_XRCE_CLIENT_DIR ${APP_XRCE_DIR}/third_party/microxrcedds_client)
set(APP_XRCE_MICROCDR_DIR ${APP_XRCE_DIR}/third_party/microcdr)

set(APP_XRCE_SRCS
    # --- 适配层（本仓库作者代码） ---
    ${APP_XRCE_DIR}/src/app_xrce.c
    ${APP_XRCE_DIR}/src/app_xrce_transport.c
    ${APP_XRCE_DIR}/src/app_xrce_platform.c
    # --- Micro-XRCE-DDS Client 3.0.2 核心 ---
    ${APP_XRCE_CLIENT_DIR}/src/c/core/log/log.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/serialization/xrce_header.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/serialization/xrce_subheader.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/serialization/xrce_types.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/common_create_entities.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/create_entities_bin.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/create_entities_ref.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/create_entities_xml.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/object_id.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/read_access.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/session.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/session_info.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/submessage.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/write_access.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/stream/input_best_effort_stream.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/stream/input_reliable_stream.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/stream/output_best_effort_stream.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/stream/output_reliable_stream.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/stream/seq_num.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/stream/stream_id.c
    ${APP_XRCE_CLIENT_DIR}/src/c/core/session/stream/stream_storage.c
    ${APP_XRCE_CLIENT_DIR}/src/c/profile/transport/custom/custom_transport.c
    # --- Micro-CDR 2.0.2（CDR 编解码 + XRCE 内部序列化） ---
    ${APP_XRCE_MICROCDR_DIR}/src/common.c
    ${APP_XRCE_MICROCDR_DIR}/src/types/array.c
    ${APP_XRCE_MICROCDR_DIR}/src/types/basic.c
    ${APP_XRCE_MICROCDR_DIR}/src/types/sequence.c
    ${APP_XRCE_MICROCDR_DIR}/src/types/string.c
)

function(app_xrce_configure)
    sdk_inc(${APP_XRCE_DIR}/inc)
    sdk_inc(${APP_XRCE_CLIENT_DIR}/include)
    sdk_inc(${APP_XRCE_MICROCDR_DIR}/include)
    sdk_app_src(${APP_XRCE_SRCS})
endfunction()
