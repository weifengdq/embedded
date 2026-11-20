/**
 * @file  s2500_app.h
 * @brief 通用应用层: S2500 + lwIP(NO_SYS) + letter-shell + 测试服务
 */
#ifndef S2500_APP_H
#define S2500_APP_H

#include "s2500.h"
#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const s2500_hal_t *hal;
    const uint8_t *mac;      /* NULL: 使用芯片出厂 MAC */
    uint8_t  plca_id;        /* 0 = 协调者, 1..254 跟随者, 255 不参与 */
    uint8_t  plca_ncnt;
    uint8_t  plca_totmr;
    uint8_t  plca_en;
    uint8_t  promiscuous;
    uint32_t ip;             /* 主机字节序, 例如 0xC0A80A0B = 192.168.10.11 */
    uint32_t mask;
    uint32_t gw;
    const char *board_name;
} s2500_app_cfg_t;

/** 初始化 lwIP / S2500 / netif / shell, 打印启动横幅 */
int  s2500_app_init(const s2500_app_cfg_t *cfg);

/** 主循环调用 (建议 1ms 周期) */
void s2500_app_poll(void);

s2500_dev_t  *s2500_app_dev(void);
struct netif *s2500_app_netif(void);
const char   *s2500_app_board(void);

#ifdef __cplusplus
}
#endif
#endif
