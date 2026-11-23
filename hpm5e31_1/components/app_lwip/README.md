# components/app_lwip —— 应用层 lwIP 组件

> 第二阶段共享底座（tp-infra）：把 SDK `samples/lwip` 的初始化收敛成 `app_lwip_init()`，
> 主循环只调 `app_lwip_poll()`。默认静态 IP **192.168.2.100/24，网关 192.168.2.1**（主机 `enp6s0`）。
> 对应书稿：第 28 章 §28.4（bench 架构 C）、第 12/13 章（YT8011AN / lwIP 原理与吞吐）。

## 1. 软件栈与硬件

```
ENET0 (RGMII, PF02~PF13)  <-->  YT8011AN (1000BASE-T1 PHY, MDIO addr 1, PHYSR=0x8400)
        |
   HPM ENET DMA (TX/RX 描述符环)
        |
   lwIP 2.1.2 baremetal port (NO_SYS=1, 主循环轮询)
        |
   应用: UDP socket / TCP / ICMP
```

- PHY 驱动复用 `components/enet_phy_yt8011`；
- lwIP 移植层复用 SDK `samples/lwip/ports/baremetal`（`ethernetif.c` / `sys_arch.c`）与
  `samples/lwip/common/single/{common,netconf}.c` —— 不拷贝、不修改 SDK；
- 组件自带 `lwipopts_app.h` + `common_cfg.h`（内存预算见 §5）。

## 2. 三行起网络

```c
#include "app_lwip.h"

int main(void)
{
    bsp_init();
    if (app_lwip_init(NULL) != 0) {          /* 192.168.2.100/24, GW .1, 等 link */
        printf("[lwip] init FAILED\r\n");
    }
    while (1) {
        app_lwip_poll();                     /* 收包 + lwIP 定时器 + link 自适应 */
        bsp_shell_poll();
    }
}
```

上电串口（本板实测，见第 28 章实测表）：

```
[lwip] netif=en idx=1 MAC=98:2C:BC:B1:9F:27
[lwip] IP=192.168.2.100 MASK=255.255.255.0 GW=192.168.2.1 link=UP
[lwip] PHY link=UP speed=1000M role=SLAVE PHYSR=8400 GBSR=3000 BMSR=000D
[lwip] link UP after 128 ms
```

主机侧：`ping 192.168.2.100`（主机 `enp6s0 = 192.168.2.1/24`）。

## 3. API

| 函数 | 说明 |
|---|---|
| `int app_lwip_init(const app_lwip_cfg_t *cfg)` | 初始化 ENET0+YT8011+lwIP+静态 IP+1 ms 时基并等 link。`NULL` = 默认。返回 0 = 成功且 link up；1 = netif 已起但 link down（可继续轮询）；<0 失败（-1 PHY/IP 配置，-2 ENET 控制器，-3 netif） |
| `struct netif *app_lwip_netif(void)` | 取 lwIP netif（即 SDK `gnetif`） |
| `uint32_t app_lwip_now_ms(void)` | 毫秒时基（= `sys_now()`，1 ms 硬件定时器驱动） |
| `void app_lwip_poll(void)` | 主循环轮询；内部 = `ethernetif_input` + `sys_check_timeouts`，**不要再单独调 `sys_check_timeouts`** |
| `struct udp_pcb *app_udp_socket(const ip_addr_t *local, uint16_t port)` | 新建并 bind 的 UDP socket，失败返回 NULL。**接收回调由使用者 `udp_recv()` 注册**；支持多次调用建多个 socket；用完 `udp_remove()` |

便利 API：`app_lwip_link_up()`、`app_lwip_print()`、`app_lwip_link_poll()`、`app_lwip_parse_ip()`。

> 契约差异说明：`docs/应用层开发约定.md` §4.2 把 `app_udp_socket` 的返回写成 `int`，
> 本组件返回 `struct udp_pcb *`（"socket 句柄"的直接形式，上层拿到就能 `udp_recv/udp_send`，
> 省掉一次查表）。语义是 §4.4 想要的"建好并 bind 的 socket"，已在团队内同步。

### 配置结构

```c
typedef struct {
    const char *ip;             /* 缺省 192.168.2.100 */
    const char *netmask;        /* 缺省 255.255.255.0 */
    const char *gw;             /* 缺省 192.168.2.1 */
    uint32_t link_timeout_ms;   /* 等 link up 上限，缺省 3000；0 = 不等 */
    bool verbose;               /* 打印 netif/PHY 详情，缺省 true */
} app_lwip_cfg_t;
```

### UDP 用法（网关 / SOME/IP / XRCE 都是这个模式）

```c
static void on_sd(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                  const ip_addr_t *addr, u16_t port)
{
    /* 回调要短：只搬运算/置标志，禁止 printf / 阻塞 */
    if (p != NULL) { pbuf_free(p); }
}

struct udp_pcb *sd = app_udp_socket(NULL, 30490);       /* NULL = 0.0.0.0 */
if (sd != NULL) { udp_recv(sd, on_sd, NULL); }

/* 发送 */
struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
memcpy(p->payload, buf, len);
udp_sendto(pcb, p, &dst, dst_port);
pbuf_free(p);
```

## 4. 接入 CMake（顺序很重要）

```cmake
cmake_minimum_required(VERSION 3.13)

# ① 必须在 find_package 之前：板级库在 find_package 内部按这几个变量配置 ENET/RGMII
set(CONFIG_LWIP 1)
set(APP_USE_ENET_PORT_COUNT 1)
set(APP_USE_ENET_ITF_RGMII 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(my_lwip_app)

# ② 必须在 find_package 之后：sdk_inc/sdk_app_src 由 SDK 定义
include(${CMAKE_CURRENT_LIST_DIR}/../../../components/app_lwip/app_lwip.cmake)
sdk_app_src(src/main.c)
```

`app_lwip.cmake` 会加入：SDK lwIP 移植源码（`ethernetif.c`/`sys_arch.c`/`common.c`/`netconf.c`/`utils.c`）、
YT8011 驱动、本组件源码、include 路径、`-DIP0_CONFIG=192.168.2.100` 等宏、`-O3`。

**不要**同时编译 `samples/enet/enet_lwip_iperf/src/phy_glue.c`，也**不要**打开 `CONFIG_ENET_PHY`
（本组件已提供 `board_init_enet_phy` / `board_get_enet_phy_status` / `lwip_enet_phy_adaptive_poll` 三个钩子）。

## 5. 内存预算（整板 DLM 128 KB）

| 项 | 配置 | 说明 |
|---|---|---|
| `ENET_TX_BUFF_COUNT` / `ENET_RX_BUFF_COUNT` | 16 / 16 | 每缓冲 1536 B，DMA 区约 49 KB |
| `MEM_SIZE`（lwIP 堆） | 24 KB | socket 控制块 + 拷贝型 pbuf |
| `PBUF_POOL_SIZE` | = TX buff count（16） | 零拷贝 TX 需要每描述符 hold 一个 pbuf |
| `MEMP_NUM_UDP_PCB` | 8 | 网关 1~3 路 + SOME/IP SD/服务多路 |
| `TCP_SND_BUF` / `TCP_WND` | 8 × MSS | 比 iperf 样例（20 MSS）小，避免 `lwip_sanity_check` 报错 |

`samples/app/can_gateway` 实测：**DLM 95 168 B / 128 KB = 72.6%**，AXI_SRAM 15.7%，Flash 136 KB
（比 iperf 样例的 30/30 缓冲省下约 43 KB DLM，给上层协议栈留余量）。

## 6. 坑

| # | 现象 | 原因 | 处理 |
|---|---|---|---|
| 1 | 上电 `link=DOWN`，几百 ms 后自己变 UP | PHY 自适应默认 2 s 轮询一次 | `app_lwip_init()` 前 3 s 每 10 ms 主动查 PHY；仍 DOWN 返回 1（netif 已可用） |
| 2 | `ping` 不通但 `ip` 命令显示 IP 正确 | 主机网口不在 192.168.2.0/24，或车载以太网盒未上电 | `ip -br a` 确认主机 `enp6s0=192.168.2.1/24`；板端 `link` 看 PHYSR 是否 0x8400 |
| 3 | 自己写的字符串转 IP 失败 | lwIP 的 `ip4addr_aton` 只接受点分十进制 | 用 `app_lwip_parse_ip()` |
| 4 | UDP 回调里 `printf` 后大量丢包 | 串口输出阻塞主循环，ENET DMA 环被填满 | 回调只置标志，打印放主循环 |
| 5 | 改大 `LWIP_APP_TIMER_INTERVAL` 后 lwIP 超时异常 | `sys_now()` 直接返回定时器 tick，单位必须是 ms | 保持 1 ms（组件 `common_cfg.h` 已固定） |
| 6 | iperf3 打不通 | SDK lwiperf 实现的是 **iperf2** 协议 | 用 `iperf`（v2）；本组件默认不带 iperf |
| 7 | 加 TCP 大缓冲后编译报 `lwip_sanity_check` | `TCP_WND > PBUF_POOL_SIZE*(PBUF_POOL_BUFSIZE-头部)` | 同步增大 `PBUF_POOL_SIZE` 或按比例减 `TCP_WND`（见 §5） |
| 8 | 想用 iperf 千兆吞吐的 30/30 缓冲配置 | 那是第 13 章的下行吞吐优化配置，占 92 KB DLM | 需要时把 `common_cfg.h` 两个 BUFF_COUNT 改回 30 并同步 `PBUF_POOL_SIZE` |

## 7. 验证

| 验证项 | 方法 | 结果 |
|---|---|---|
| ping | 主机 `ping 192.168.2.100` | 见第 28 章实测表 / `logs/*can_gateway*` |
| UDP 数据通路 | `host/can_bridge/` + 第 28 章 bench B | 见 `host/can_bridge/README.md` |
| 千兆吞吐（背景） | `samples/enet/enet_lwip_iperf`（第 13 章） | TCP 下行 935 Mbit/s、UDP 下行 944 Mbit/s |

## 8. 文件

```
components/app_lwip/
├── inc/app_lwip.h        # 公开 API
├── inc/lwipopts_app.h    # lwIP 选项（内存预算，UDP_PCB=8）
├── inc/common_cfg.h      # 1 ms 时基 + ENET 缓冲数量
├── src/app_lwip.c        # 初始化/轮询/UDP socket + YT8011 板级钩子
├── app_lwip.cmake        # CMake 接入片段
└── README.md
```