# 第 13 章 lwIP 协议栈、iperf 与以太网性能

## 13.1 文档信息

表 13-1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 |
| 对应例程 | `samples/enet/enet_lwip_iperf/` |
| 依赖章节 | 第 12 章（YT8011AN PHY 与 RGMII，本章不再重复 PHY 寄存器细节） |
| 实测日期 | 2026-10-30（首轮 + 8 组上行优化）、2026-11-02（小包/双向/重传复测） |
| 可信度 | 中高（移植代码来自官方 SDK 并上板实测；100BASE-T1 档位因转换盒需人工拨码，列为已知限制） |

## 13.2 本章导览

- 学到什么：把 lwIP 2.x 移植到 HPM5E31 ENET0（RGMII + YT8011AN）的完整分层与最小改动方法；
  零拷贝 TX/RX 的数据通路与 Cache 维护；iperf 吞吐测试的正确工具选择（**iperf2 而非 iperf3**）；
  如何用一条串口命令链从"上电"走到"主机 ping 通 + iperf 打流"。
- 前置章节：第 12 章（PHY/RGMII）、第 02 章（构建/烧写）、第 03 章（shell）。
- 承接章节：第 24 章（总线吞吐与实时性对比，会引用本章数据）。

## 13.3 概述与典型应用

HPM SDK v1.13.0 自带 lwIP 2.x 中间件与一整套 baremetal/FreeRTOS/RT-Thread 移植样例。
本板的差异只有一处：**PHY 是 YT8011AN，不是 SDK 样例默认的 RTL8211**。
因此本章的移植原则是"**尽量不改 SDK，最小化粘连层**"：

- 协议栈、网卡收发的适配层（`ethernetif.c`、`common.c`、`netconf.c`、`sys_arch.c`）**直接复用 SDK 源码**；
- 只在 `samples/enet/enet_lwip_iperf/src/phy_glue.c` 里补 **3 个薄函数**，把 PHY 驱动接进去；
- 不打开 `CONFIG_ENET_PHY`，避免 `board.c` 编译出基于 RTL8211 的同名函数（第 12 章坑 2）。

典型应用场景：

1. 车载以太网节点做 TCP/UDP 数据上报（iperf 可量化链路余量）；
2. 设备联网自检：上电自动 ping 网关 + iperf 打流，作为产线测试项；
3. 作为 lwIP 应用开发基座：HTTP/MQTT/自定义 TCP 服务端均可在此基础上加；
4. 与 100BASE-T1 档位对比，评估降速对协议栈吞吐/延迟的影响。

## 13.4 工作原理

### 13.4.1 分层结构与文件对应关系

![图 13-1 本板 lwIP 移植的分层与文件对应关系](figures/13/fig-13-1-lwip-layers.png)（见表 13-2）

表 13-2 本板 lwIP 移植的分层与文件对应

| 层 | 文件 | 是否修改 | 作用 |
|---|---|---|---|
| 应用 | `samples/enet/enet_lwip_iperf/src/main.c` | 新写 | shell 命令、lwiperf 服务端/客户端 |
| 协议栈 | `hpm_sdk/middleware/lwip/` | 未改 | lwIP 2.x 核心 + lwiperf 应用 |
| 网卡适配 | `sdk/samples/lwip/ports/baremetal/ethernetif.c` | 未改 | `ethernetif_init`/`ethernetif_input` |
| 板级公共 | `sdk/samples/lwip/common/single/common.c` | 未改 | `enet_init`、零拷贝 TX/RX、定时器回调 |
| 网络配置 | `sdk/samples/lwip/common/single/netconf.c` | 未改 | 静态 IP 的 `netif_config` |
| 薄粘连 | `samples/enet/enet_lwip_iperf/src/phy_glue.c` | 新写 | PHY 钩子 3 个函数 |
| 驱动 | `components/enet_phy_yt8011/` | 新写 | YT8011AN 寄存器级驱动 |
| 底层 | `hpm_sdk/drivers/src/hpm_enet_drv.c` | 未改 | ENET MAC/DMA |

### 13.4.2 零拷贝收发与 Cache 维护

![图 13-2 ENET DMA 与 lwIP pbuf 的收发数据通路](figures/13/fig-13-2-zero-copy.png)

SDK 的 baremetal 移植采用**双向零拷贝**：

- **TX**：`enet_lwip_output()` 直接把 pbuf payload 的物理地址写进 TX 描述符 `buffer1`，
  不做 `memcpy`；发送完成后轮询描述符 owner 位再释放 pbuf（`enet_lwip_tx_release()`）。
  `common_cfg.h` 里 `ENET_TX_BUFF_COUNT=30` 与 `PBUF_POOL_SIZE` 对齐，否则 TX 会饿死 pbuf 池。
- **RX**：DMA 把帧写进 30 个 1536 B 的 `rx_buff[i]`，软件为每个缓冲区构造一个 **custom pbuf**
  （`enet_lwip_rx_custom_pbuf_t`），payload 直接指向 DMA 缓冲区，交出 pbuf 时不复制。
- **Cache**：描述符表放在 non-cacheable 段（`ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT`），
  TX 前对 payload 做 `l1c_dc_writeback`，RX 后做 `l1c_dc_invalidate`。**忘记这两步的典型现象是
  "ping 通但 iperf 大量重传/TCP 卡死"**。

### 13.4.3 薄粘连层做了什么

`phy_glue.c` 只做三件事：

1. `board_init_enet_phy()`：校验 PHY ID → 写 EXT 0x9001（RGMII 延迟）→ 写 MMD1 0x834
   （1000BASE-T1 + Slave）→ 带速率编码软复位 → 等待 link up（最多 2 s）。
   之所以不用 SDK 的 `hpm_enet_phy_port_init()`：它只支持 rtl8211/jl1111 等类型，
   且需要 `CONFIG_ENET_PHY=1`，会与 `board.c` 的同名函数冲突。
2. `board_get_enet_phy_status()`：把 `yt8011_get_status()` 的结果翻译成
   `enet_phy_status_t`（link/speed/duplex/speed_valid）。
3. `lwip_enet_phy_adaptive_poll()`：SDK 的 `enet_self_adaptive_port_speed()`（2 s 周期）会调用它；
   我们在这里把链路状态落到 `netif_set_link_up/down()`，不引入 SDK 的 `hpm_enet_phy_adaptive` 组件。

## 13.5 关键配置与 API 对照

表 13-3 关键配置项与取值

| 功能 | 配置项/寄存器 | 位置 | 本板取值 |
|---|---|---|---|
| 协议栈使能 | `set(CONFIG_LWIP 1)` | 例程 CMakeLists | 1 |
| iperf 应用 | `set(CONFIG_LWIP_IPERF 1)` | 例程 CMakeLists | 1（编译 `lwiperf.c`） |
| DHCP | `-DLWIP_DHCP=0` | 例程 CMakeLists | 0（静态 IP） |
| 收发中断 | `-D__ENABLE_ENET_RECEIVE_INTERRUPT=0` | 例程 CMakeLists | 0（轮询） |
| lwIP 自定义选项 | `-DUSE_LWIPOPTS_APP_H=1` | 例程 CMakeLists | 1 |
| custom pbuf | `-DLWIP_SUPPORT_CUSTOM_PBUF=1` | 例程 CMakeLists | 1 |
| 优化等级 | `sdk_compile_options(-O3)` | 例程 CMakeLists | -O3 |
| IP/掩码/网关 | `IP0_CONFIG/NETMASK0_CONFIG/GW0_CONFIG` | `inc/common_cfg.h` | 192.168.2.100 / 255.255.255.0 / 192.168.2.1 |
| TX/RX 描述符数 | `ENET_TX_BUFF_COUNT/ENET_RX_BUFF_COUNT` | `inc/common_cfg.h` | 30 / 30 |
| 单缓冲区大小 | `ENET_TX_BUFF_SIZE/ENET_RX_BUFF_SIZE` | `inc/common_cfg.h` | 1536 B |
| MTU | `netifMTU`（ethernetif.c 默认） | SDK | 1500 |
| 硬件 checksum | `enet_get_default_hw_checksum_config(..., false)` | SDK common.c | 关闭（软件校验） |
| 系统时基 | `LWIP_APP_TIMER_INTERVAL` | `inc/common_cfg.h` | 1 ms（`board_timer_create`） |
| iperf 端口 | `LWIPERF_TCP_PORT_DEFAULT/LWIPERF_UDP_PORT_DEFAULT` | lwIP | 5001 |

## 13.6 例程逐条讲解

### 13.6.1 例程：enet_lwip_iperf

表 13-4 例程 enet_lwip_iperf 概览

| 项 | 内容 |
|---|---|
| 路径 | `samples/enet/enet_lwip_iperf/` |
| 覆盖特性 | ENET0 MAC/DMA + lwIP 2.x + iperf2（TCP/UDP 服务端与客户端）+ PHY 状态查询 |
| 关键配置 | 静态 IP 192.168.2.100/24；MTU 1500；TX/RX 各 30 个描述符（1536 B）；1 ms 时基；-O3 |
| 实测现象 | 上电后 1~2 s link UP（1000M Slave）；netif link up；主机可 ping 通并 iperf 打流 |
| 实测日志 | 见第 5 节各表"日志"列 |
| shell 命令 | `ip`、`link`、`iserver [tcp\|udp]`、`iclient <ip> [tcp\|udp]`、`istop`、`mode <1000\|100>` |

构建与运行：

```bash
cd hpm5e31_1
bash scripts/build.sh enet/enet_lwip_iperf
# 上板并抓串口；--send 里的命令在启动后自动执行
bash scripts/hwrun.sh enet/enet_lwip_iperf --secs 60 --send 'ip;link;iserver tcp'
# 另一个终端（主机侧，千兆网卡 192.168.2.1/24，iperf2）
ping -c 20 -I enp6s0 192.168.2.100
iperf -c 192.168.2.100 -p 5001 -t 10          # TCP 板端作服务端
iperf -c 192.168.2.100 -p 5001 -u -b 100M -t 10
```

关键代码 1（启动序列，摘自 `src/main.c`）：

```c
if (enet_init(ENET) != status_success) {          /* 引脚 + 时钟 + PHY + MAC/DMA */
    printf("Enet initialization fails !!!\r\n");
    while (1) { bsp_shell_poll(); }
}
lwip_init();                                      /* 协议栈初始化 */
netif_config(&gnetif);                            /* 静态 IP + netif_add + link 回调 */
enet_services(&gnetif);                           /* 打印 IP 信息（无 DHCP 时） */
board_timer_create(LWIP_APP_TIMER_INTERVAL, sys_timer_callback);  /* 1 ms 时基 */
while (1) {
    enet_common_handler(&gnetif);                 /* 收包 + sys_check_timeouts */
    bsp_shell_poll();                             /* letter-shell 命令 */
}
```

关键代码 2（PHY 钩子，摘自 `src/phy_glue.c`）：

```c
hpm_stat_t board_init_enet_phy(ENET_Type *ptr)
{
    yt8011_config_t cfg;
    yt8011_status_t st;
    if (!yt8011_check_id(ptr, BOARD_NET2_PHY_ADDR)) {   /* 0x4F51 / OUI 0x3A */
        return status_fail;
    }
    yt8011_basic_mode_default_config(ptr, &cfg);       /* 1000BASE-T1 + Slave */
    if (yt8011_basic_mode_init(ptr, BOARD_NET2_PHY_ADDR, &cfg) != status_success) {
        return status_fail;
    }
    (void) yt8011_wait_link_up(ptr, BOARD_NET2_PHY_ADDR, 2000U, &st);
    return status_success;
}
```

### 13.6.2 主机侧准备

表 13-5 主机侧准备步骤

| 步骤 | 命令 | 说明 |
|---|---|---|
| 找到连接转换盒的网卡 | `ethtool <if> \| grep -i "link detected"\|Speed` | 本机为 `enp6s0`（1000 Mb/s Full，carrier=1）；`enx001ec0d1c337` 是 10BASE-T1S 的 USB 网卡（LAN8651 用），不要动 |
| 配置 IP | `ip addr add 192.168.2.1/24 dev enp6s0` | 保留原有 192.168.0.1/24，避免影响别人 |
| 清 ARP 缓存 | `ip neigh flush dev enp6s0` | 板子被重新烧写后 MAC/IP 映射可能残留 |
| 工具 | `iperf`（v2.1+） | **不是 iperf3**，见第 7 节坑 1 |

> 注意：主机 `enp3s0` 是 SSH 上行（<host>/24），**任何情况下不要动**；
> `enp4s0` 无 carrier（未接）；`enx001ec0d1c337` 是 tp-net1 的 10BASE-T1S USB 网卡。

## 13.7 上板实测数据

### 13.7.1 测试条件

表 13-6 测试条件

| 项 | 值 | 说明 |
|---|---|---|
| 板卡 | hpm5e31_1（HPM5E31IPB1，CPU 480 MHz） | 本章例程 |
| PHY | YT8011AN，1000BASE-T1，**Slave** | 第 12 章配置 |
| MAC 接口 | RGMII，PHY 侧 RXC 2 ns 延迟，ENET 侧 delay 0 | EXT 0x9001=0x0110 |
| 协议栈 | lwIP 2.1.2，baremetal（`NO_SYS=1`），轮询模式 | `-DLWIP_DHCP=0` |
| 收发 | 零拷贝 TX/RX，TX/RX 各 30 个描述符，单缓冲 1536 B | `inc/…` 见 3 节表 |
| MTU | 1500 | `ethernetif.c` 默认 |
| 优化 | `-O3` | 例程 CMakeLists |
| 硬件 checksum | 关闭（软件校验） | SDK `common.c` |
| 板端 IP / MAC | 192.168.2.100/24 / 00:00:00:00:D0:0E（OTP 无有效 MAC，走默认/衍生值） | 串口 `ip` 命令 |
| netif | `en`（netif index = 1） | 串口 `ip` 命令 |
| 主机网卡 | enp6s0，1000 Mb/s Full，192.168.2.1/24 | `ethtool enp6s0` |
| 主机工具 | **iperf2 2.2.1**（`/usr/bin/iperf`），内核 7.0.0-38-generic | 不用 iperf3（见第 7 节坑 1） |
| 方向约定 | `iperf -c` = 主机→板（下行）；主机 `iperf -s` + 板端 `iclient` = 板→主机（上行）；**未使用 `-R`** | |
| 对端 | 1000BASE-T1↔RJ45 转换盒，拨码 1000M Master | |

串口自证（`logs/20261030-113501-enet_lwip_iperf.log`）：

```text
=== enet_lwip_iperf: YT8011AN(1000BASE-T1) + lwIP + iperf2 ===
lwIP 2.1.2
[phy] YT8011AN link up: 1000M SLAVE
Enet phy init passed !
[net] netif  : en (index=1)
[net] MAC    : 00:00:00:00:D0:0E
[net] IP     : 192.168.2.100
[phy] link=UP speed=1000M role=SLAVE PHYSR=8400 GBSR=3000 BMSR=000D thread=0
[iperf] TCP server started on port 5001
```

### 13.7.2 连通性（ping）

表 13-7 ping 连通性

| 项目 | 结果 | 日志 |
|---|---|---|
| `ping -c 20 -I enp6s0 192.168.2.100`（56 B） | **20/20 收到，0% 丢包**，总耗时 19.4 s | `logs/20261030-113501-enet_lwip_iperf-host.log` |

### 13.7.3 TCP 吞吐

表 13-8 TCP 吞吐

| 方向 | 主机侧 | 板端统计 | 相对线速（1000 Mbit/s） | 日志 |
|---|---|---|---|---|
| 下行（主机→板，`iperf -c`） | **935 Mbit/s**（1.09 GB / 10 s） | 934.7 Mbit/s（1,168,769,048 B / 10.003 s） | **93.5%** | host log + `logs/20261030-113501-enet_lwip_iperf.log` |
| 上行（板→主机，板端 `iclient`） | **415 Mbit/s**（494 MiB / 10 s） | 414.5 Mbit/s（518,158,404 B / 10 s） | **41.5%** | host log + `logs/20261030-114622-enet_lwip_iperf.log` |

![图 13-3 TCP/UDP 实测吞吐（1000BASE-T1，下行）](figures/13/fig-13-3-throughput.png)

TCP 逐秒速率（下行，摘自主机日志）：

```text
[  1] 0.0000-1.0000 sec   106 MBytes   892 Mbits/sec
[  1] 1.0000-2.0000 sec   111 MBytes   929 Mbits/sec
...
[  1] 9.0000-10.0000 sec   113 MBytes   948 Mbits/sec
[  1] 0.0000-10.0039 sec  1.09 GBytes   935 Mbits/sec
```

> 注：本节是第一轮（优化前）的基线数据；发送方向优化后的对比见 6.3 节（TCP 上行 466、UDP 上行 641 Mbit/s）。

### 13.7.4 UDP 吞吐与丢包（下行）

板端以 `iserver udp` 做服务端，主机以 `iperf -c ... -u -b N` 发送 10 s；
丢包率 = 1 − 板端收到字节 / 主机发送字节（主机日志的 MBytes 为 1024 进制，含 3 位有效数字显示误差）。（见表 13-9）

表 13-9 UDP 吞吐与丢包

| 主机发包速率 | 主机发送量 | 板端收到 | 板端统计速率 | 丢包率 | 日志 |
|---|---|---|---|---|---|
| `-b 100M`（实际 105 Mbit/s） | 125 MiB | 131,072,550 B | 104.75 Mbit/s | ≈0% | `logs/20261030-113919-enet_lwip_iperf.log` |
| `-b 300M`（实际 315 Mbit/s） | 375 MiB | 392,243,040 B | 314.23 Mbit/s | ≈0.25% | 同上 |
| `-b 600M`（实际 629 Mbit/s） | 750 MiB | 786,370,620 B | 628.40 Mbit/s | ≈0.01% | 同上 |
| `-b 900M`（实际 944 Mbit/s） | 1.10 GiB | 1,179,635,310 B | 942.67 Mbit/s | ≈0.13% | 同上 |

结论：**即使在 944 Mbit/s 的 UDP 冲击下，丢包仍 <0.2%**，说明该链路的物理层与板端收包路径
（零拷贝 RX + 30 个 1536 B 缓冲）在 1000BASE-T1 线速附近没有明显瓶颈。

![图 13-4 UDP 发送带宽与实际到达带宽/丢包率（1000BASE-T1）](figures/13/fig-13-4-udp-loss.png)

### 13.7.5 CPU 占用

表 13-10 CPU 占用

| 场景 | CPU 占用 | 测量方法 |
|---|---|---|
| 空闲（刚启动，0.78 s 窗口） | 20.3% | 主循环累计 `mcycle`，`[cpu]` 命令 |
| **TCP 满载下行**（34 s 窗口） | **24.7%** | iperf 期间每 5 s 自动打印 |
| **TCP 满载上行**（12 s 窗口） | **99.5%** | 同上（板端 iclient 发送，主循环饱和） |

> 空闲 20% 主要来自 1 ms 定时器回调 + 轮询收包 + shell；TCP 满载只增加约 4 个百分点，
> 说明瓶颈更可能在 lwIP 单线程处理链（pbuf/checksum/拷贝）而不是 ENET DMA。

### 13.7.6 上行（板→主机）与 1000M/100M 对比

表 13-11 上行与 1000M/100M 对比状态

| 项 | 状态 | 说明 |
|---|---|---|
| TCP 上行（板 `iclient` → 主机 `iperf -s`） | **已完成：415 Mbit/s**，板端 CPU 99.5% | 主机服务端在板端连接前启动；等价于 iperf2 的 `-R` 方向 |
| 100BASE-T1 档位对比 | 已知限制（需人工拨码） | 转换盒拨码固定在 1000M Master，软件切 100M 后 link down（第 12 章 5.3 表已验证），无法在无人值守下测速 |

## 13.8 性能与调优

### 13.8.1 数据解读

1. **TCP 单流 935 Mbit/s ≈ 线速 93.5%**：扣除 Ethernet 头（14 B）+ IP（20 B）+ TCP（20 B）
   与 ACK 反向流量后，已接近 lwIP 单线程在 480 MHz RISC-V 上的常见水平（通常 90%~96%）。
   同一硬件换 FreeRTOS 双线程通常不会更高，反而因切换开销略低。
2. **UDP 944 Mbit/s 下丢包 <0.2%**：说明收包路径不是瓶颈；UDP 无重传/ACK，更接近"裸链路"能力。
3. **上行只有 415 Mbit/s（41.5% 线速），CPU 却接近 100%** —— 这是**发送路径 CPU 受限**的直接证据：
   同一硬件、同一链路，接收方向能到 935 Mbit/s 且只占 24.7% CPU，发送方向却把主循环打满。
   lwIP 发送路径要做 pbuf 分配、TCP 分段、校验和、逐段 `tcp_output` 触发 DMA 描述符填充，
   而板端 `lwiperf` 客户端按固定 buflen 逐段写入，难以凑满 30 个 TX 描述符的流水。
   优化方向（待验证）：增大 `TCP_SND_BUF`/合并写、提高 TX 描述符数、把发送并入中断/多缓冲。
4. **CPU 只增加约 4 个百分点（20.3% → 24.7%）**：说明 ENET DMA + 零拷贝已经把搬运开销降到很低，
   剩余开销集中在协议栈处理（校验和、pbuf 管理、TCP 状态机）。
5. **未观察到链路层错误**：`link` 命令期间 PHYSR 恒为 0x8400、GBSR 恒为 0x3000，
   说明 RGMII 时序（PHY 侧 2 ns 延迟 + ENET 侧 0）在 1000M 下稳定。

### 13.8.2 可调参数与预期影响

表 13-12 可调参数与预期影响

| 参数 | 当前值 | 调整方向 | 预期影响 |
|---|---|---|---|
| `ENET_TX_BUFF_COUNT/RX_BUFF_COUNT` | 30 / 30 | ↑ 到 40+ | 突发吸收能力略增；DLM 先爆（现 97.2%） |
| `TCP_SND_BUF` | 20 × MSS | ↑ 到 40 × MSS | 高 BDP 链路吞吐略增；本链路 RTT 极小，收益有限 |
| `PBUF_POOL_SIZE` | 30 | 必须 ≥ TX 描述符数 | 否则 TX 饿死 |
| `MEM_SIZE` | 32 KB | ↑ | 更多并发连接；DLM 余量不足 |
| `-O3` | 开启 | 必开 | 关闭后吞吐下降（未量化） |
| ENET 侧 RGMII RX delay | 0 | 0~7 档 | 仅影响误码率；当前无错包，无需调 |
| 接收中断模式 | 关闭（轮询） | 打开 `__ENABLE_ENET_RECEIVE_INTERRUPT=1` | 小包延迟更低；吞吐可能略降（中断开销） |


### 13.8.3 上行（板→主机）吞吐优化专项

**问题**：第一轮实测中 TCP 上行只有 415 Mbit/s 且 CPU 99.5%，而同一链路的
TCP 下行 935 Mbit/s（CPU 24.7%）、UDP 下行 944 Mbit/s（丢包 ≤0.25%），说明物理层余量充足，
瓶颈在**发送方向的软件路径**。本节记录针对该问题的系统性优化实验（2026-10-30 第二轮）。

**实验方法**：每改一组参数 → `bash scripts/build.sh enet/enet_lwip_iperf` →
`bash scripts/hwrun.sh enet/enet_lwip_iperf --send 'cacheinfo;link;sleep 12000;iclient 192.168.2.1 tcp|udp'`
→ 主机侧 `iperf -s` / `iperf -s -u` 收流 → 记录板端统计 Mbit/s、主机 Mbit/s 与 `[cpu]` 占用。
为定位瓶颈，例程新增两个诊断命令（关键证据见表 13-15，逐项收益见表 13-16，最终对比见表 13-17）：

| 命令 | 作用 |
|---|---|
| `txbench [frames] [len]` | 绕过 lwIP，把同一个 pbuf 直接反复交给 `enet_lwip_output()`，测 ENET TX 驱动+DMA 的纯发送能力与每帧 CPU 周期 |
| `cacheinfo` | 打印 `MCACHE_CTL`，确认 L1 D-cache 是否开启以及是否处于 write-around 模式 |

表 13-15 诊断测量结果（决定优化方向的关键证据）

| 测量 | 结果 | 解读 |
|---|---|---|
| `txbench 20000 64` | 634,875 pkt/s，**756 cycles/帧**，325 Mbit/s | 未受线速限制 → ENET 驱动固定 CPU 开销约 756 cycles/帧 |
| `txbench 20000 512` | 192,270 pkt/s，2,496 cycles/帧，787.5 Mbit/s | 含数据搬运与 cache 回写 |
| `txbench 20000 1460` | 74,460 pkt/s，6,446 cycles/帧，**869.7 Mbit/s** | 受 DMA/线速限制 → 驱动+DMA 上限 ≈870 Mbit/s，**不是驱动能力问题** |
| `cacheinfo` | `MCACHE_CTL=0x00007F03`：DC=1、IC=1、**write-around=0b11** | 写直达内存，TX 的 D-cache 回写可以省略 |

表 13-16 优化项与逐项收益（TCP/UDP 上行，板端统计）

| 编号 | 优化内容 | 关键改动 | TCP 上行 | UDP 上行 | 结论 |
|---|---|---|---|---|---|
| 基线 | 原样（SND_BUF 20×MSS、TX/RX 30、软件校验和） | — | 415.3 | 493（另一轮） | 参考点 |
| E1 | 关闭 lwIP 软件 TX 校验和，改由 ENET 硬件插入 | CMake 加 `-DCHECKSUM_GEN_*=0` | 415.3 | — | **无变化**，校验和不是瓶颈 |
| E3 | lwiperf 发送缓冲由 Flash(const) 改到 RAM | 复制 lwiperf.c 改 1 行 | 390.4 | — | **无变化**，排除 XIP DMA 带宽假设 |
| E4 | 加大发送窗口但把 RX 缓冲降到 12 | SND_BUF 40×MSS、RX 12 | 266 | 479 | 回退：RX 太小拖垮 TCP ACK 接收 |
| E5 | 恢复 RX 30，TX 环 30→64 | TX 64 | 367 | 494 | 参数本身无明显收益 |
| O1 | 跳过 Flash/non-cacheable 数据的 TX 回写 | `src/common_fast.c` | **437** | 539 | 有效（TCP 最好值） |
| O2（最终） | 跳过全部 TX 回写（write-around 已开，安全） | 同上 | **466** | **641** | UDP 大幅提升，TCP 也小幅提升 |
| E6 | RX 24 + 堆 28 KB | — | 315 | 555 | 回退 |

表 13-17 优化前后最终对比

| 项目 | 优化前 | 优化后（最终配置） | 变化 | 日志 |
|---|---|---|---|---|
| TCP 上行（板→主机） | 415.3 Mbit/s | **466.3 Mbit/s** | **+12.3%** | `logs/20261030-153645-enet_lwip_iperf.log` |
| UDP 上行（板→主机） | 493 Mbit/s | **641.5 Mbit/s** | **+30.1%** | `logs/20261030-153733-enet_lwip_iperf.log` |
| TCP 下行（主机→板） | 935 Mbit/s | 902.3 Mbit/s | −3.5%（主机负载波动范围内） | `logs/20261030-153556-enet_lwip_iperf.log` |
| 上行 CPU 占用 | 99.5% | 99.7% | 仍饱和（瓶颈未消失，只是每包成本下降） | 串口 `[cpu]` |

![图 13-5 上行发送路径优化前后对比](figures/13/fig-13-5-tx-opt.png)

把表 13-15 的周期数据按"驱动 / D-cache 回写 / 协议栈"三段画出来就是图 13-6。它解释了两件事：为什么关掉 TX 回写能立刻涨一截（约占每段成本的 35%），以及为什么再把 SND_BUF/描述符调大也到不了 700 Mbit/s——剩下的大头在 `tcp_write/tcp_output` 与 pbuf 管理。

![图 13-6 TCP 上行 1460 B 段的 CPU 周期分解与上行吞吐](figures/13/fig-13-6-tcp-bottleneck.png)

**瓶颈结论（证据链）**：

1. **物理层/链路无问题**：下行 935 Mbit/s、UDP 下行 944 Mbit/s、UDP 上行 639 Mbit/s 均远超 TCP 上行。
2. **ENET 驱动+DMA 不是瓶颈**：`txbench` 单帧 1460 B 可达 869.7 Mbit/s；TX 描述符 30→64 无收益。
3. **校验和不是瓶颈**：关闭软件校验和（改硬件插入）后吞吐不变。
4. **数据源位置不是瓶颈**：Flash const 缓冲与 RAM 缓冲结果一致，排除 XIP DMA 假设。
5. **D-cache 回写是单项最大软件开销**：`l1c_dc_writeback()` 走 CCTL 命令接口，实测约 109 cycles/cacheline，
   1460 B 帧约 46 行 ≈ 5000 cycles；跳过回写后 UDP 上行提升 17~30%。
6. **TCP 上行的剩余瓶颈是 lwIP 每段处理**：跳回写后 TCP 仍只有约 415~437 Mbit/s、CPU 99.5%，
   折算每 1460 B 段约 12,000 cycles（驱动仅 ~750），主要花在 `tcp_write`/`tcp_output`/pbuf 管理与 ACK 处理上。

**最终配置**（`samples/enet/enet_lwip_iperf/CMakeLists.txt` 与 `src/common_fast.c`）：
1. SDK lwipopts 默认参数（SND_BUF 20×MSS、SEG 40、MEM 32 KB、TX/RX 30）；
2. `src/common_fast.c` 替换 SDK common.c，跳过 TX D-cache 回写（`-DNET2_TX_WB_SKIP_ALL=1`，前提是 `cacheinfo` 显示 write-around=3）；
3. `-DCHECKSUM_GEN_*=0`，TX 校验和交给 ENET 硬件；
4. 修复 UDP 客户端缺少 `lwiperf_poll_udp_client()` 的 bug。

**为什么没有达到 700 Mbit/s**：要把每段成本从 ~12,000 cycles 压到 ~8,000 以下，
仅靠参数调优（窗口/描述符/校验和/缓存）已经到顶。可行方向都超出本章例程范围：

| 方向 | 预期收益 | 障碍 |
|---|---|---|
| Jumbo Frame（MTU 3000~9000） | 每包成本摊薄 2~6 倍，最有希望过 700 | 128 KB DLM 装不下按 MTU 放大的 RX 缓冲 + pbuf pool；需改链接脚本/换更大 RAM 区域 |
| TCP 发送泵批处理/零拷贝 | 减少每段 pbuf 分配与 tcp_output 次数 | 需要替换 lwiperf 发送逻辑或改写 lwIP TX 路径 |
| 硬件 TSO/LSO | 一次提交多段 | HPM5E31 ENET 不支持 |
| 双连接并行 | 单核 CPU 仍饱和，收益有限 | 不是 iperf 单流指标 |

### 13.8.4 小包、双向打流与重传（2026-11-02 复测）

这一节补的是第 11 节表 13-14 里几个原先没数据的项。测试条件与 5.1 节相同（1000BASE-T1 Slave、RGMII delay 0），
主机用 `iperf` v2 2.2.1，板端固件为 6.3 节最终配置，一次上板连续跑完（日志 `logs/20261102-124252-enet_lwip_iperf.log`）。

表 13-18 小包、双向与重传实测（2026-11-02）

| 项目 | 命令/口径 | 结果 |
|---|---|---|
| ping（56 B） | `ping -c 5 -I enp6s0 192.168.2.100` | **5/5，0% 丢包，RTT 0.15~0.49 ms** |
| TCP 下行（主机→板） | `iperf -c 192.168.2.100 -t 10` | **847 Mbit/s**（1010 MB / 10.0 s） |
| TCP 下行 64 B 小包 | `iperf -c 192.168.2.100 -t 5 -l 64` | **310 Mbit/s，约 605 kpps**（185 MB / 5.0 s） |
| TCP 上行（板→主机） | 板 `iclient 192.168.2.1 tcp` + 主机 `iperf -s` | **462 Mbit/s**（551 MB / 10.0 s） |
| 双向同时 | 主机 `iperf -c` 与 `iperf -s` 同时，板端 `iclient` | 下行 **369**、上行 **283** Mbit/s（合计 652） |
| TCP 重传（上行 10 s 窗口） | `nstat -az` 前后差：TcpRetransSegs / TcpOutSegs | **0 / 54,408 = 0%** |
| TCP 超时 | TcpExtTCPTimeouts 前后差 | **0** |
| 乱序队列 | TcpExtTCPOFOQueue 前后差 / TcpInSegs 前后差 | 1,279 / 396,479 = **0.32%** |

结论：

1. **TCP 上行 462 Mbit/s** 与 R4 的 466 Mbit/s 一致，说明 6.3 节的优化结果可复现；本轮没有再改发送路径。
2. **64 B 小包 605 kpps（310 Mbit/s）**：小包吞吐由 PPS 决定，310 Mbit/s ÷ 64 B ≈ 605 kpps，
   对应每包约 790 ns，接近 1000BASE-T1 最小帧间隔下的理论上限（64 B 帧线上时间约 0.67 µs）。
3. **双向同时打流合计 652 Mbit/s**（369 + 283），低于单向 847/462，但方向间没有出现链路错误或重传风暴。
4. 上行窗口内**重传 0、超时 0**，说明 RGMII 时序（PHY 侧 2 ns + ENET 0 档）与 lwIP TX 路径稳定。
5. UDP 下行这一轮只起了 TCP 服务端，主机 `iperf -u` 的板端计数没有采到（发送 944 Mbit/s 但无 ack）；
   UDP 数据仍以 5.4 节 2026-10-30 的实测为准，复测时板端要先 `iserver udp`。

## 13.9 坑与注意事项

1. **lwIP 的 lwiperf 是 iperf2 协议，不能用 iperf3 打流**。
   现象：`iperf3 -c 192.168.2.100` 连接被拒或立刻断开；`iperf -c` 正常。
   原因：lwIP `apps/lwiperf` 实现的是 iperf2.0.x 的控制/数据协议（默认 TCP/UDP 5001），
   iperf3 换了控制通道（默认 5201）与 JSON 握手，两者不兼容。
   解决：主机用 `iperf`（v2）。本机 `/usr/bin/iperf` 可用；也可用 `iperf -s -p 5001`
   反向往板端 `iclient` 打流。**这也是为什么本仓库没有采用"iperf3 端口 5202"**。

2. **不打开 `CONFIG_ENET_PHY`，否则 `board_init_enet_phy` 符号冲突**。
   现象：链接报 `multiple definition of board_init_enet_phy`，或链接到 RTL8211 的实现导致 PHY 初始化失败。
   原因：`CONFIG_ENET_PHY=1` 会让 `components/enet_phy/CMakeLists.txt` 定义
   `-DCONFIG_ENET_PHY=1`，从而 `boards/hpm5e31_1/board.c` 也编译出该函数（只支持 rtl8211/jl1111）。
   解决：不设该变量，由 `src/phy_glue.c` 提供同名函数；同时在 `inc/common_cfg.h` 里补声明
   （`board.h` 的声明在 `CONFIG_ENET_PHY` 保护内，否则 `common.c` 会报 implicit declaration 警告）。

3. **`-include xxx.h` 这种全局强制包含会弄坏汇编文件**。
   现象：给所有源文件加 `-include phy_hooks.h` 后，`hpm_tfa_drv.S` 编译报
   "unrecognized opcode 'typedef signed char __int8_t'"。
   原因：`sdk_compile_options` 对 .S 汇编文件同样生效，头文件被当成汇编解析。
   解决：把声明放进已被 C 源文件包含的公共头（本项目放在 `inc/common_cfg.h`），不要用 `-include`。

4. **`PBUF_POOL_SIZE` 必须 ≥ `ENET_TX_BUFF_COUNT`**。
   原因：零拷贝 TX 在 DMA 完成前一直持有 pbuf；池子小于在飞窗口时会立即饿死，
   表现为 TCP 吞吐骤降或 `ERR_MEM`。
   本板取值：两者都 30（`inc/common_cfg.h`）。

5. **DLM 占用接近上限**。
   现象：`ENET_TX/RX_BUFF_COUNT=30` + `MEM_SIZE=32 KB` 时链接报告 DLM 使用 127424 B / 128 KB（97.2%）。
   原因：`rx_buff/dma 描述符/协议栈堆` 都在 DLM。若再加缓冲区会链接失败。
   解决：需要更大缓冲时优先减小 `PBUF_POOL_SIZE`/增大 non-cacheable 段，而不是继续加 DLM 变量。

6. **link 状态依赖 2 s 周期的自适应轮询，不是中断**。
   现象：上电后 netif 已经 up 但 `netif_is_link_up()` 要等 1~2 s 才变 1；
   刚上电立刻 ping 会丢前几个包。
   原因：`sys_timer_callback` 里 `sys_tick % (2000 * LWIP_APP_TIMER_INTERVAL) == 0` 才调用
   `enet_self_adaptive_port_speed()`（`sdk/samples/lwip/ports/baremetal/arch/sys_arch.c`）。
   解决：测试脚本先 ping 探活再打流；产品中可在 PHY 中断里主动置 link。

7. **主机 IP 叠加而不是替换**。
   本机 `enp6s0` 原有 192.168.0.1/24（别人测试用），加 192.168.2.1/24 时用 `ip addr add`，
   不要 `ip addr flush`/`ip addr del`，避免破坏他人的在跑测试。

8. **iperf 结果要区分方向与协议**。
   `iperf -c`（板端 `iserver`）= 主机→板（下行/接收方向）；
   `iperf -s`（板端 `iclient <host_ip> tcp`）= 板→主机（上行/发送方向）。
   UDP 还要记录发送带宽、实际到达带宽、丢包率与 jitter，三者缺一不可。

## 13.10 优劣评价与同类对比

表 13-13 几种协议栈方案对比

| 方案 | 吞吐上限（理论） | CPU 占用 | 移植工作量 | 适用场景 |
|---|---|---|---|---|
| 本机 lwIP baremetal + 零拷贝（本章） | 受 1000BASE-T1 与 MEM 带宽限制 | 中（协议栈在中断/主循环） | 小（复用 SDK 适配层） | 裸机、成本敏感、协议简单 |
| lwIP + FreeRTOS（SDK 样例） | 略低于 baremetal（任务切换开销） | 中高 | 中 | 多任务、需要 socket 阻塞语义 |
| 硬件协议栈/MAC 卸载 | 更高 | 低 | 大 | 大流量网关 |
| 裸机自写 TCP（无协议栈） | 高（无 ARP/重传） | 低 | 大 | 专有链路、点对点 |

评价：

- 优点：**改动面积极小**（只加 3 个函数 + 4 个配置宏），完全复用 SDK 的零拷贝收发；
  调试命令齐全，串口即可完成 ping/iperf 全流程自证。
- 缺点：baremetal 轮询模式下 CPU 全被协议栈与主循环占用，没有低功耗；
  iperf2 工具链较老；DLM 余量偏紧。

## 13.11 参考来源

1. HPM SDK v1.13.0（<https://github.com/hpmicro/hpm_sdk>，核对日期 2026-11-02）：`samples/lwip/lwip_iperf/`、`samples/lwip/ports/baremetal/`、`samples/lwip/common/single/`
2. `hpm_sdk/middleware/lwip/src/apps/lwiperf/lwiperf.c`（iperf2 协议实现，端口 5001）
3. 第 12 章（YT8011AN PHY/RGMII 寄存器与实测）
4. 本仓库实测日志（见第 5 节各表与第 6 节）
5. `samples/enet/enet_lwip_iperf/README.md`（命令与现象）

## 13.12 扩展阅读

- [lwIP 官方文档](https://www.nongnu.org/lwip/2_1_x/index.html)
- [iperf2 项目](https://sourceforge.net/projects/iperf2/)（与 lwIP lwiperf 兼容；本机实测版本 2.2.1）
- [lwIP lwiperf 应用说明](https://www.nongnu.org/lwip/2_1_x/group__lwiperf.html)（iperf2 协议、端口 5001 的依据）
- [HPM SDK 官方仓库](https://github.com/hpmicro/hpm_sdk)（`samples/lwip/` 各移植样例）
- 本仓库第 24 章（总线吞吐与实时性对比，会引用本章数据）

## 13.13 待确认项与风险

表 13-14 待确认项与风险（**已逐条复核，2026-11-02**）

| 项 | 状态 | 验证方法 |
|---|---|---|
| 100BASE-T1 档位吞吐 | **已知限制（需人工拨码）**：转换盒拨码固定在 1000M Master，软件切 100M 后 link down（第 12 章表 12-12 第 2 行已证），无人值守无法测 | 现场把拨码改 100M Master、板端 `mode 100 slave` 后按 5.3/5.4 节同一套命令复测 iperf TCP/UDP |
| 双向同时打流（full-duplex 压力） | **已实测（2026-11-02）**：下行 369、上行 283 Mbit/s，合计 652（表 13-18） | 见 5.7 节；判据是双向都无重传/超时 |
| 发送方向吞吐优化 | **已实测（8 组实验，2026-10-30）**：UDP 上行 +17~30%，TCP 上行 415→466；2026-11-02 复测 462（未再优化） | 继续优化需要 Jumbo Frame（受 DLM 限制）或改 lwIP TX 路径，量化分析见 6.3 节 |
| TCP 上行每段 ~12,000 cycles 的具体分解 | **已定位（三段量化估算，未做函数级打点）** | 需要时用 mcycle 在 `tcp_write/tcp_output/enet_lwip_output` 三处打点，按图 13-6 的口径核对 |
| 小包（64 B）PPS 上限 | **已实测（2026-11-02）**：TCP 下行 64 B 为 310 Mbit/s ≈ **605 kpps**（表 13-18） | `iperf -c 192.168.2.100 -t 5 -l 64`；UDP 64 B 需板端先 `iserver udp` |
| CPU 占用率 | **已实测（2026-10-30，5.5 节）**：空闲 20.3%、TCP 下行 24.7%、上行 99.5% | 复测用板端 `cpu` 命令，或按 GPTMR 计主循环空转 |
| TCP 重传/乱序 | **已实测（2026-11-02）**：上行 10 s 窗口内重传 0、超时 0、乱序 0.32%（表 13-18） | 主机 `nstat -az` 前后差值 + `ss -ti` |
| 长时间稳定性（>1 h） | **已知限制（本轮无独占板窗口）**：13:00 后板卡交给 Lead 做全量回归，>1 h 长跑需要 `board_lease.sh` 分段 | 用 `board_lease.sh 110` 分段跑 10 min×6 + `hwrun --no-flash` 复用固件，统计 ping 丢包与 PHYSR 变化 |
