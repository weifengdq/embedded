/*
 * common_cfg.h - app_lwip 组件的 lwIP 时间/缓冲预算（自 SDK lwip_iperf/inc 抽取出，
 * 数值与 samples/enet/enet_lwip_iperf 保持一致，第 13 章实测 935 Mbit/s 下行）
 *
 * 注意：本文件会被 lwipopts_app.h #include，必须与 lwipopts_app.h 放在同一目录。
 */
#ifndef COMMON_CFG
#define COMMON_CFG

/* board_timer 周期：1 ms（sys_now() 的单位就是 ms，不能改大，否则 lwIP 超时全部变慢） */
#define LWIP_APP_TIMER_INTERVAL (1) /* 1 ms */

/* ENET DMA 描述符/缓冲数量：TX 30 + RX 30 为 iperf 千兆下行实测配置 */
#define ENET_TX_BUFF_COUNT  (16U)
#define ENET_RX_BUFF_COUNT  (16U)
#define ENET_TX_BUFF_SIZE   (1536U)
#define ENET_RX_BUFF_SIZE   (1536U)

#endif /* COMMON_CFG */
