cmake_minimum_required(VERSION 3.13)

# ---------------- 工程级配置 ----------------
set(CONFIG_LWIP 1)
set(CONFIG_PTPD_V2 0)          # 不用 SDK middleware/ptpd/v2，改用本仓库 components/app_ptp/ptpd_v2（见该目录 README）
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

set(SDK_DIR $ENV{HPM_SDK_BASE})
set(LWIP_DIR ${SDK_DIR}/samples/lwip)
set(NET2_COMPONENT ${CMAKE_CURRENT_LIST_DIR}/../../../components/enet_phy_yt8011)
set(PTP_COMPONENT ${CMAKE_CURRENT_LIST_DIR}/../../../components/app_ptp)
set(PTPD_DIR ${PTP_COMPONENT}/ptpd_v2/src)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(hpm5e31_1_ptp_slave)

# ---------------- 编译宏 ----------------
sdk_compile_definitions(-D__DISABLE_AUTO_NEGO=0 -D__ENABLE_ENET_RECEIVE_INTERRUPT=0)
sdk_compile_definitions(-DLWIP_DHCP=0 -DUSE_LWIPOPTS_APP_H=1 -DLWIP_SUPPORT_CUSTOM_PBUF=1)
# R5 U-37-6: -DPTP_SW_TS=ON 编译"软件打戳"对照版（LWIP_PTP=0：ptpd 在协议栈里用 getTime() 取时，
# 而不是用 MAC 在收发时刻写进描述符的硬件时戳）
if(PTP_SW_TS)
  sdk_compile_definitions(-DLWIP_PTP=0 -DENET_PTP_CLK=BOARD_ENET_RGMII_PTP_CLOCK)
  message(STATUS "ptp_slave: software timestamp variant (LWIP_PTP=0)")
else()
  sdk_compile_definitions(-DLWIP_PTP=1)              # pbuf 带 time_sec/time_nsec；TX 按 UDP 319 置 TTSE
endif()
sdk_compile_definitions(-DDEFAULT_DELAYREQ_INTERVAL=0)  # 延迟测量 1~2 s 一次，收敛更快
sdk_compile_definitions(-DSLAVE_ONLY=TRUE)  # ptpd v2 角色
sdk_compile_definitions(-DDEFAULT_DELAY_MECHANISM=1)
sdk_compile_definitions(-DBOARD_SHOW_CLOCK=0 -DBOARD_SHOW_BANNER=0)
# 静态 IP（静态注入是因为 inc/common_cfg.h 与 SDK lwip_iperf 的同名头会互相遮蔽）
sdk_compile_definitions(-DIP0_CONFIG=192.168.2.100 -DNETMASK0_CONFIG=255.255.255.0 -DGW0_CONFIG=192.168.2.1)
sdk_compile_options(-O2)

# ---------------- 头文件路径 ----------------
sdk_inc(${LWIP_DIR}/ports/baremetal)
sdk_inc(${LWIP_DIR}/ports/baremetal/arch)
sdk_inc(${LWIP_DIR}/common)
sdk_inc(${LWIP_DIR}/common/single)
sdk_inc(${SDK_DIR}/components/enet_phy)
sdk_inc(${NET2_COMPONENT}/inc)
sdk_inc(${PTP_COMPONENT}/inc)
sdk_inc(${PTPD_DIR})
sdk_inc(${PTPD_DIR}/dep)
sdk_inc(inc)

# ---------------- 源文件 ----------------
sdk_app_src(${LWIP_DIR}/ports/baremetal/arch/sys_arch.c)
sdk_app_src(${LWIP_DIR}/ports/baremetal/ethernetif.c)
sdk_app_src(${LWIP_DIR}/common/single/common.c)
sdk_app_src(${LWIP_DIR}/common/single/netconf.c)
sdk_app_src(${LWIP_DIR}/common/utils/utils.c)
sdk_app_src(${NET2_COMPONENT}/src/hpm_yt8011.c)
sdk_app_src(${PTP_COMPONENT}/src/app_ptp.c)
sdk_app_src(${PTP_COMPONENT}/src/phy_glue.c)
# PTPd 2.0.1（移植版，见 components/app_ptp/ptpd_v2/README.md）
sdk_app_src(${PTPD_DIR}/arith.c)
sdk_app_src(${PTPD_DIR}/bmc.c)
sdk_app_src(${PTPD_DIR}/protocol.c)
sdk_app_src(${PTPD_DIR}/ptpd.c)
sdk_app_src(${PTPD_DIR}/dep/msg.c)
sdk_app_src(${PTPD_DIR}/dep/net.c)
sdk_app_src(${PTPD_DIR}/dep/servo.c)
sdk_app_src(${PTPD_DIR}/dep/startup.c)
sdk_app_src(${PTPD_DIR}/dep/time.c)
sdk_app_src(${PTPD_DIR}/dep/timer.c)
sdk_app_src(src/main.c)

generate_ide_projects()