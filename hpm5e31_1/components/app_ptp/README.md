# components/app_ptp —— IEEE 1588 PTP / 802.1AS 时间同步组件（hpm5e31_1）

把 **HPM5E31 ENET MAC 硬件时间戳** + **lwIP 2.1.2** + **PTPd 2.0.1** 封装成板级 PTP 普通时钟（Ordinary Clock），
一行初始化即可作为从钟或主钟运行，并内置串口诊断（状态/offset 统计）。

## 1. 组成

| 路径 | 说明 |
|---|---|
| `inc/app_ptp.h` | 对外 API：`app_ptp_hw_init()` / `app_ptp_start()` / `app_ptp_poll()` / 状态与统计打印 |
| `src/app_ptp.c` | ENET PTP 时钟初始化、1 ms 时基、offset 统计、letter-shell 命令 |
| `src/phy_glue.c` | YT8011AN 的 lwIP 板级钩子（`board_init_enet_phy` / `board_get_enet_phy_status` / adaptive poll） |
| `ptpd_v2/` | PTPd 2.0.1 移植版（含 1 处上游修补，见其 README） |

依赖（由使用方 `CMakeLists.txt` 提供，样例已写好）：

- SDK：`samples/lwip/common/single/{common,netconf}.c`、`samples/lwip/ports/baremetal/{arch/sys_arch.c,ethernetif.c}`、`samples/lwip/common/utils/utils.c`；
- 编译宏：`-DLWIP_PTP=1 -DUSE_LWIPOPTS_APP_H=1 -DLWIP_SUPPORT_CUSTOM_PBUF=1`，
  以及 `SLAVE_ONLY`、`DEFAULT_DELAY_MECHANISM`、`DEFAULT_DELAYREQ_INTERVAL`、`DEFAULT_SYNC_INTERVAL`；
- 板级：`components/enet_phy_yt8011`（PHY 驱动）。

## 2. 用法

```c
#include "bsp.h"
#include "common.h"
#include "netconf.h"
#include "lwip/init.h"
#include "app_ptp.h"

int main(void)
{
    bsp_init();
    if (enet_init(ENET) != status_success) { /* 错误处理 */ }

    app_ptp_hw_init(NULL);      /* ENET PTP 硬件时钟：SSINC/TSADDEND/TSINIT + 可选 PPS0 */
    lwip_init();
    netif_config(&gnetif);
    enet_services(&gnetif);
    app_ptp_start();            /* ptpd_Init() + 1 ms 时基 */

    while (1) {
        enet_common_handler(&gnetif);
        app_ptp_poll();         /* 推进协议状态机 + 每秒采样 offset */
        bsp_shell_poll();
    }
}
```

顺序不能颠倒：`app_ptp_hw_init()` 必须在 `enet_init()` 之后、`lwip_init()` 之前；
`app_ptp_start()` 必须在 lwIP netif 建好之后（它要打开 UDP 319/320 的 socket 并加入组播组）。

## 3. shell 命令

| 命令 | 作用 |
|---|---|
| `ptp` | 打印状态机状态、PTP 域/延迟机制、硬件时钟、offset、路径延迟、观测漂移、GM 身份 |
| `ptpstats` | 打印板上累计的 offset 统计（n/均值/σ/min/max/last），每秒采样一次 |
| `ptpstats reset` | 清空统计窗口 |
| `ptpbench [secs]` | 阻塞式测量窗口：内部持续轮询网络与 PTP，每 5 s 打一次统计（默认 30 s） |
| `ptpreset <sec>` | 直接改写 ENET 硬件 PTP 时钟（秒），用于主钟设定起始时刻 |
| `sleep <ms>` | 延时但保持 PTP 轮询（板端 shell 单线程，普通 delay 会冻结协议栈） |

## 4. 时间戳与时钟调整路径（关键）

```text
RX: PHY(YT8011AN) → MAC → DMA 写 RX 描述符 RDES6/RDES7（硬件打戳）
    → ethernetif.c: enet_get_rx_timestamp(ls_rx_desc) → pbuf->time_sec/time_nsec
    → ptpd: netRecv() 取 p->time_sec/nsec 作为报文到达时刻

TX: ptpd 组包 → udp_sendto() → ethernetif → common.c enet_lwip_output()
    → lwip_ptp_tx_ts.h 判定「目的端口 319」→ TDES0.TTSE=1
    → DMA 发送完成后回填 TDES6/TDES7 → enet_get_tx_timestamp() → pbuf->time_sec/time_nsec
    → ptpd: netSend() 取回作为精确发送时刻（two-step 的 Follow_Up / Delay_Req 用）
```

从钟侧伺服：

- 相位：`updateTime()` → `enet_update_ptp_timeoffset()`（写 `SYST_SEC_UPD/SYST_NSEC_UPD` + `TS_CTRL.TSUPDT`）；
- 频率：`adjFreq()` → `enet_adjust_ptp_time_freq()`（写 `TS_ADDEND = adj*0x80000000/(1e9-adj)+0x80000000`）。

## 5. 已知限制

| 项 | 说明 |
|---|---|
| 时间基准 | PTP 时钟源 `clock_ptp0` = PLL1_CLK0 400 MHz / 4 = **100 MHz**，SSINC=10 ns，理论分辨率 10 ns；实测 offset 典型 ±60 ns（受对端与中间转换盒影响） |
| 中间设备 | 板 ↔ 主机之间有 1000BASE-T1↔RJ45 转换盒，单向 13.8 µs、双向不对称未标定 → 绝对时间精度待确认（第 36 章 U-37-1） |
| PPS 输出 | 组件支持 `app_ptp_cfg_t.enable_pps`，但本板 PPS0 引脚未引出（`BOARD_ENET_RGMII_PPS0_PINOUT=0`），物理 PPS 相位测量需飞线 |
| 802.1AS | 只做到 "P2P 延迟机制 + 125 ms Sync" 的 gPTP 风格参数（`gptp_demo`），不是完整 802.1AS（无 Signaling/BMCA 差异/同步域） |

## 6. 实测与文档

- 例程：`samples/app/ptp_slave`、`samples/app/ptp_master`、`samples/app/gptp_demo`
- 上位机：`host/ptp/`（linuxptp ptp4l/pmc + 统计脚本）
- 书稿：`docs/chapters/37-*.md`
