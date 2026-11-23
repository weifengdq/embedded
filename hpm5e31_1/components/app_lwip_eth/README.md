# app_lwip_eth —— 在 AUTOSAR MCAL Eth 之上跑 lwIP 2.1.2

把 lwIP 的 netif 架在 `mcal/Eth` + `mcal/EthTrcv` 上（不碰 SDK enet 驱动），
收发路径按 Eth.h 的"数据通路约定"实现：发送走 `Eth_ProvideTxBuffer()` + `Eth_Transmit(..., PhysAddrPtr=NULL)`，
接收走 `Eth_GetRxBuffer()` + custom pbuf（零拷贝）+ `Eth_ReleaseRxBuffer()`。

- 配套例程：`samples/mcal/eth_lwip`（裸机 NO_SYS=1）、`samples/mcal/eth_freertos_lwip`（FreeRTOS）
- 书稿：第 49 章 §49.6（lwIP 移植）与 §49.8（FreeRTOS 集成）

## 用法（裸机）

```cmake
set(CONFIG_LWIP 1)
set(CONFIG_LWIP_IPERF 1)
set(APP_LWIP_ETH_PORT "baremetal")     # 或 "freertos"（还要 CONFIG_FREERTOS / CONFIG_LWIP_NETCONN_API）
find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE} NO_DEFAULT_PATH)
project(xxx)
include(.../mcal/mcal.cmake)
mcal_add(Eth EthTrcv)
include(.../components/app_lwip_eth/app_lwip_eth.cmake)
sdk_app_src(src/main.c)
```

```c
Eth_Init(&Eth_Config);              /* 顺序不能变 */
EthTrcv_Init(&EthTrcv_Config);
Eth_SetControllerMode(0, ETH_MODE_ACTIVE);
app_lwip_eth_timebase_start();      /* 裸机：1 ms lwIP 时基 */
app_lwip_eth_init(NULL);            /* 静态 IP 192.168.2.100/24 */
while (1) { app_lwip_eth_poll(); bsp_shell_poll(); }
```

FreeRTOS 版：先 `tcpip_init(NULL, NULL)`，再 `app_lwip_eth_init()`，
然后 `xTaskCreate(app_lwip_eth_rx_task, ...)`。

## 对外 API（节选）

| 函数 | 作用 |
|---|---|
| `app_lwip_eth_init(cfg)` | 建 netif、配静态 IP、等链路；返回 0=link up |
| `app_lwip_eth_poll()` | 裸机主循环：收包 + `Eth_MainFunction` + 1 s 链路检测 |
| `app_lwip_eth_rx_drain()` | 把 Rx 环排空（RTOS 收包任务里循环调用） |
| `app_lwip_eth_ping(ip, n, ms, out)` | ICMP echo，输出 RTT 与抖动 |
| `app_lwip_eth_iperf_server/client/stop` | iperf2（lwiperf） |
| `app_lwip_eth_print/print_stats` | netif/PHY/统计（含 loop/tx/rx 耗时画像） |

## 后端接口

`inc/app_lwip_eth_hw.h` 定义的 `app_lwip_eth_hw_ops_t` 是 netif 与具体驱动之间唯一的缝：
`tx_buffer_get/tx_commit/rx_get/rx_release/get_mac/link_get/poll/lock`。
正式实现是 `src/app_lwip_eth_hw_mcal.c`；同一份 netif 代码也可挂到别的驱动后端上做对照实验。

## 坑（详见第 49 章 §49.9 / §49.8.9）

1. `EthTrcv_Init` 之前读 MDIO 全是 0（Eth_Init 才做板级复位/时钟）；
2. `Eth_ReadMii` 的 TrcvIdx 是收发器索引（0），不是 PHY 地址（1）；
3. `lwiperf` 的 UDP 客户端必须每轮调 `lwiperf_poll_udp_client()`；
4. 打流时不要开高频 printf（会顶住主循环，直接掉包）；
5. 不要自己配 PHY（RGMII 延迟/主从/速率归 EthTrcv）。

## 并发与缓冲回收（R8 补充，对应第 49 章 §49.9.13 / §49.9.14）

这层 netif 适配器有两个"看起来没事、打流时致命"的点，改之前 TCP 只有 4.83 Mbit/s：

1. **TX 缓冲要在输出路径上就地回收**：TCP 的 ACK 是在 `netif->input()` 里同步产生的，
   而驱动的 `Eth_TxConfirmation` 只在主循环的 `Eth_MainFunction()` 里跑。
   `app_lwip_eth_rx_drain()` 一次连收 16 帧就可能要 16 次 TX 缓冲，8 个缓冲瞬间占满，
   后面的 ACK 全部 `ERR_MEM` 被丢。所以 `linkoutput` 在 `tx_buffer_get()` 返回 NULL 时，
   **就地调用 `s_ops->poll()`**（= `Eth_MainFunction()`，只做 TxConfirmation 与 RxHeld 统计，
   不碰 RX 环）再重试一次；仍失败才返回 `ERR_MEM` 交给 TCP 重传。
   可观测：`[lwip-eth] tx recycle: retry_ok=N retry_fail=M`，252 Mbit/s 打流时 N≈10 000、M=0。

2. **RX drain 必须设上限**：`APP_LWIP_ETH_RX_DRAIN_MAX`（默认 32 = 2 倍 RX 环深）。
   不设上限时一次 `app_lwip_eth_poll()` 会一直收到环空，1 Gbit/s 打流下实测连跑 2.4 s，
   期间 `sys_check_timeouts()`（RTO / 延迟 ACK）、`Eth_MainFunction()`、shell 全被饿死。

3. **不要在这一层改 TX 发放顺序相关的假设**：驱动保证"按 DMA 环序发放、拿不到就 BUFREQ_E_BUSY"，
   上层要接受 `ERR_MEM` 并用 TCP 重传兜底（第 49 章 §49.9.12 是这一条的反例）。
