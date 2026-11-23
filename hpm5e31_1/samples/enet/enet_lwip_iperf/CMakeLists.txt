cmake_minimum_required(VERSION 3.13)

set(CONFIG_LWIP 1)
set(CONFIG_LWIP_IPERF 1)  # 使用 SDK lwiperf（发送缓冲在 Flash，O1 优化正好跳过其回写）
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

set(SDK_DIR $ENV{HPM_SDK_BASE})
set(LWIP_DIR ${SDK_DIR}/samples/lwip)
set(NET2_COMPONENT ${CMAKE_CURRENT_LIST_DIR}/../../../components/enet_phy_yt8011)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_enet_lwip_iperf)

sdk_compile_definitions(-D__DISABLE_AUTO_NEGO=0 -D__ENABLE_ENET_RECEIVE_INTERRUPT=0)
sdk_compile_definitions(-DLWIP_DHCP=0 -DUSE_LWIPOPTS_APP_H=1 -DLWIP_SUPPORT_CUSTOM_PBUF=1)
# 实验 E1：关闭 lwIP 软件 TX 校验和，改由 ENET 硬件插入（common.c 在 CHECKSUM_BY_HARDWARE 下已调用
# enet_set_hw_checksum_config(..., true)）。主机侧会校验 TCP/UDP 校验和，若硬件配置错误会直接不通。
sdk_compile_definitions(-DCHECKSUM_GEN_IP=0 -DCHECKSUM_GEN_TCP=0 -DCHECKSUM_GEN_UDP=0 -DCHECKSUM_GEN_ICMP=0)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
# 注意：SDK 的 lwip_iperf/inc/common_cfg.h 与本样例 inc/ 同名，而 lwipopts_app.h 用
# #include "common_cfg.h" 时会先命中同目录那份，故静态 IP 必须用 -D 注入（netinfo.h 是 #ifndef 保护）
sdk_compile_definitions(-DIP0_CONFIG=192.168.2.100 -DNETMASK0_CONFIG=255.255.255.0 -DGW0_CONFIG=192.168.2.1)
sdk_compile_options(-O3)
sdk_compile_definitions(-DNET2_TX_WB_SKIP_ALL=1)  # 最终配置：write-around 已开(MCACHE_CTL[14:13]=3)，TX 全部跳过 D-cache 回写

sdk_inc(${LWIP_DIR}/ports/baremetal)
sdk_inc(${LWIP_DIR}/ports/baremetal/arch)
sdk_inc(${LWIP_DIR}/common)
sdk_inc(${LWIP_DIR}/common/single)
sdk_inc(${LWIP_DIR}/common/utils)
sdk_inc(${LWIP_DIR}/lwip_iperf/inc)
sdk_inc(${SDK_DIR}/components/enet_phy)
sdk_inc(${NET2_COMPONENT}/inc)

sdk_app_src(${LWIP_DIR}/ports/baremetal/arch/sys_arch.c)
sdk_app_src(${LWIP_DIR}/ports/baremetal/ethernetif.c)
# 用本样例的 common_fast.c 替换 SDK common.c（仅增加发送路径 cache 回写裁剪）
sdk_app_src(src/common_fast.c)
sdk_app_src(${LWIP_DIR}/common/single/netconf.c)
sdk_app_src(${LWIP_DIR}/common/utils/utils.c)
sdk_app_src(${NET2_COMPONENT}/src/hpm_yt8011.c)
sdk_app_src(src/phy_glue.c)
sdk_app_src(src/main.c)
