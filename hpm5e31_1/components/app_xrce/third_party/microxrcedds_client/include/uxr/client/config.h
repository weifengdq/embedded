/*
 * uxr/client/config.h —— Micro-XRCE-DDS Client 3.0.2 的平台配置（手工生成版）
 *
 * 上游并不直接提供 config.h：它是 CMake 用 include/uxr/client/config.h.in 生成的。
 * 本仓库是非 CMake 组件式集成（SDK 的 sdk_app_src），因此把生成结果固化在这里，
 * 由 components/app_xrce/ 维护。改动配置项时请同步 README 的"配置项"表。
 *
 * 上游版本：Micro-XRCE-DDS-Client v3.0.2 (commit 6884d7bdd5682f34e0084f48dfe41a2192495465)
 * 平台    ：HPM5E31 + lwIP 2.1.2，裸机 NO_SYS=1
 * 传输    ：UCLIENT_PROFILE_CUSTOM_TRANSPORT（app_xrce_transport.c 用 lwIP UDP 实现）
 */
#ifndef _UXR_CLIENT_CONFIG_H_
#define _UXR_CLIENT_CONFIG_H_

#define UXR_CLIENT_VERSION_MAJOR 3
#define UXR_CLIENT_VERSION_MINOR 0
#define UXR_CLIENT_VERSION_MICRO 2
#define UXR_CLIENT_VERSION_STR "3.0.2"

/* 只启用自定义传输；UDP/TCP/Serial/CAN 的 Posix 实现全部不编译（板端无 OS）。 */
#define UCLIENT_PROFILE_CUSTOM_TRANSPORT

/* 每类流各 1 条：可靠输出（带 ACK/重传）、best-effort 输出（高频数据）、
 * 可靠输入（订阅数据/状态）、best-effort 输入（保留）。 */
#define UXR_CONFIG_MAX_OUTPUT_BEST_EFFORT_STREAMS 1
#define UXR_CONFIG_MAX_OUTPUT_RELIABLE_STREAMS    1
#define UXR_CONFIG_MAX_INPUT_BEST_EFFORT_STREAMS  1
#define UXR_CONFIG_MAX_INPUT_RELIABLE_STREAMS     1

#define UXR_CONFIG_MAX_SESSION_CONNECTION_ATTEMPTS 5
#define UXR_CONFIG_MIN_SESSION_CONNECTION_INTERVAL 1000
#define UXR_CONFIG_MIN_HEARTBEAT_TIME_INTERVAL     100

/* 单条 UDP 报文上限：以太网 MTU 1500，Agent 默认按 512 分片，保持一致。 */
#define UXR_CONFIG_CUSTOM_TRANSPORT_MTU 512

#endif /* _UXR_CLIENT_CONFIG_H_ */
