# 第 11 章 LAN8651 PLCA 主从、协议栈与性能

## 11.1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片 / 板卡 | HPM5E31IPB1 / hpm5e31_1 rev.1 |
| 对应例程 | `samples/lan8651/lan8651_raw_frame/`、`lan8651_plca_master/`、`lan8651_plca_slave/`、`lan8651_spi_probe/`、`lan8651_lwip_iperf/` |
| 对应驱动 | `components/oa_tc6/`（TC6 数据通道 + PLCA 配置） |
| 实测日期 | 2026-10-30（裸帧/PLCA）、2026-11-02（lwIP 端到端） |
| 可信度 | PLCA 寄存器配置/主从状态、裸帧收发：已上板实测（11.6）；lwIP 端到端：已联调，首轮暴露 RX 自锁问题并修复（11.9、11.12） |
| 前置章节 | 第 10 章（TC6 控制通道与芯片 bring-up） |

## 11.2 本章导览

第 10 章把 LAN8651 的**控制通道**打通了（能读写寄存器、能读到 PHY ID）。本章往前推两步：

1. **数据通道**：把 TC6 的 68 字节数据块跑起来，真的从板子发一帧以太网出去、从对端收一帧回来；
2. **PLCA**：把 10BASE-T1S 的多点仲裁打开，用主机 USB 适配器当协调者（节点 0）、板子当跟随者（节点 1），并回读 PLCA 状态位证明协商成功。

最后讨论 lwIP 怎么接、性能天花板在哪、为什么在 hpm5e31_1 rev.1 上"SPI 传输方式"会成为主要瓶颈。

读完你应该能回答三个问题：PLCA 的几个参数到底影响什么？TC6 的 tx_credits 怎么决定发送节奏？为什么同样 10 Mbps 的线，实测吞吐会差 30% 以上？

## 11.3 概述与典型应用

10BASE-T1S 的多点混合段是**共享介质**：所有节点在同一对双绞线上收发。IEEE 802.3cg 提供两种工作方式：

- **CSMA/CD**：传统以太网冲突检测。10 Mbps 下冲突窗口很短，节点一多吞吐崩得很快；
- **PLCA（Physical Layer Collision Avoidance）**：由**协调者（节点 0）**周期性发 BEACON，各节点按 node_id 顺序在分配的时隙里发送，**从物理层消除冲突**。

PLCA 是 10BASE-T1S 的"灵魂"：Microchip 的配置应用笔记 AN1760 直接写明"正常工作时器件应工作在 PLCA 模式"（[AN1760](https://www.microchip.com/en-us/application-notes/an1760)）。它在 PHY 内部实现，寄存器属于 **OPEN Alliance TC14** 定义的一组 MMD31 寄存器（`0xCA00`–`0xCA05`）。

![图 11-1 PLCA 循环结构（BEACON / TO 定时器 / burst）](figures/11/fig-11-1-plca-cycle.png)

图 11-1 画出一个 PLCA 循环的骨架：协调者在循环开头广播 BEACON，其它节点以它为时间基准；每个节点轮到自己的时隙前要先等一个 **TO（Transmit Opportunity）定时器**，发送完把总线交回。

## 11.4 工作原理

### 11.4.1 PLCA 六个寄存器

PLCA 的全部配置都在 MMD31（MMS = 4）的 `0xCA00`–`0xCA05`，语义见 `components/oa_tc6/lan865x.h`：

表 11-1 PLCA（TC14）寄存器

| 寄存器 | 地址（MMS4） | 位域 | 作用 |
|---|---|---|---|
| PLCA IDVER | 0xCA00 | IDM[15:8]=0x0A、VER[7:0] | 标识这是 TC14 PLCA 映射 |
| PLCA CTRL0 | 0xCA01 | EN(15) RST(14) | 使能 / 复位 PLCA 子层 |
| PLCA CTRL1 | 0xCA02 | NCNT[15:8] ID[7:0] | 节点总数 / 本节点 ID |
| PLCA STATUS | 0xCA03 | PST(15) | 1 = 已同步到协调者的 BEACON（链路"活"） |
| PLCA TOTMR | 0xCA04 | TOT[7:0] | TO 定时器（单位 20 bit 时间） |
| PLCA BURST | 0xCA05 | MAXBC[15:8] BTMR[7:0] | burst 模式下连续发送的最大帧数 / 帧间隔 |

### 11.4.2 配置顺序（本仓库实现）

`lan865x_plca_configure()` 的顺序是：**关 PLCA → 写 RST → 写 CTRL1/TOTMR/BURST → 按需使能**，然后立刻整体回读（`lan865x_plca_read()`）作为自证：

```c
/* 1) disable, 2) reset, 3) parameters, 4) enable */
lan865x_mmd_w(tc6, MDIO_OATC14_PLCA_CTRL0, 0);
lan865x_mmd_w(tc6, MDIO_OATC14_PLCA_CTRL0, PLCA_CTRL0_RST);
lan865x_mmd_w(tc6, MDIO_OATC14_PLCA_CTRL1, (node_cnt << 8) | node_id);
lan865x_mmd_w(tc6, MDIO_OATC14_PLCA_TOTMR, to_tmr);
lan865x_mmd_w(tc6, MDIO_OATC14_PLCA_BURST, (burst_cnt << 8) | burst_tmr);
lan865x_mmd_w(tc6, MDIO_OATC14_PLCA_CTRL0, enabled ? PLCA_CTRL0_EN : 0);
return lan865x_plca_read(tc6, cfg);   /* 回读 = 自证 */
```

**顺序很重要**：先写参数再使能是安全的；如果反过来（先使能再改 node_id），节点会在旧 ID 的时隙里发一帧，容易出现"偶发丢帧、随后恢复"的诡异现象（第 11.8 节易错点 3）。

### 11.4.3 两个角色

- **协调者 / 节点 0（Coordinator）**：产生 BEACON。整条段上**有且只有一个**。它可以是主机侧 USB 适配器（`ethtool --set-plca-cfg ... node-id 0`），也可以是板端。
- **跟随者 / 节点 1..N（Follower）**：听 BEACON 同步自己的时隙。`node_cnt` 必须与协调者一致，`node_id` 不能重复。

本仓库把两种角色都做成了命令（`plca on 0 ...` / `plca on 1 ...`），可以在一块板 + 一个 USB 适配器上把主从都演示一遍。

### 11.4.4 TC6 数据通道如何与 PLCA 配合

PLCA 在 PHY 侧（表 11-1 的寄存器）决定"什么时候能把帧放到线上"，TC6 在 SPI 侧决定"主机的帧怎么塞进 MAC-PHY 的缓冲区"。二者通过 **TX credits** 耦合：

1. 主机读 `OA_BUFSTS`（或从上一个块的尾部）拿到可用块数 `tx_credits`；
2. 主机把以太网帧切成 68 B 块，一个 SPI 事务里最多塞 `tx_credits` 个；
3. MAC-PHY 在 PLCA 给它的时隙里把帧发出去，腾出缓冲区，下一次数据事务的尾部就会报告更多的 `tx_credits`。

所以**吞吐瓶颈有两个**（表 11-5 给出定量估算）：SPI 侧搬块的速度（本板实测见 11.6）和 PLCA 时隙的利用率（`to_tmr`/`burst_cnt` 决定）。

![图 11-2 从 lwIP 到 LAN8651 的软件栈与数据通路](figures/11/fig-11-2-software-stack.png)

## 11.5 关键寄存器与驱动 API 对照

表 11-1 给出 TC14 的 6 个寄存器，表 11-2 把 PLCA/数据通道相关的寄存器、驱动 API 和例程位置对齐。

表 11-2 PLCA / 数据通道寄存器与驱动 API 对照

| 功能 | 寄存器 / API | 本仓库实现 | 出处 |
|---|---|---|---|
| PLCA 全量配置 | MMD4 0xCA00–0xCA05 | `lan865x_plca_configure()` / `lan865x_plca_read()` | TC14、`lan865x.c` |
| PLCA 是否已同步 | MMD4 0xCA03 bit15 (PST) | `lan865x_plca_status()` | TC14 |
| 发送缓冲区信用 | `OA_BUFSTS`(0x000B) / 数据块尾 bits[5:1] | `oa_tc6_update_buffer_status()`、`oa_tc6_process_footer()` | DS §11.1.12 |
| 一次数据事务 | 68 B × N 块，全双工 | `oa_tc6_data_transfer()` | DS §5.2 |
| 发送一帧 | 切块 + 等 credits | `oa_tc6_send_frame()` | `oa_tc6.c` |
| 接收轮询/取帧 | 尾部重组 + 队列 | `oa_tc6_poll_rx()` / `oa_tc6_rx_pop()` | `oa_tc6.c` |
| MAC 过滤 | `MAC_NET_CFG`(MMS1 0x0001) | `lan865x_mac_set_promisc()` | `lan865x.c` |
| PHY 中断状态 | MMD4 0x0018 (STS1) / 0x001C (IMSK1) | `lan865x_phy_sts1()` / `lan865x_phy_irq_enable()` | AN1760 |
| 零对齐接收帧（errata s3） | `OA_CONFIG0` bit12 (ZARFE) | `oa_tc6_zarfe_enable()` | LAN8650/1 Errata DS80001075 |

## 11.6 例程逐条讲解

### 11.6.1 `lan8651_raw_frame` —— 裸以太网帧收发

| 项 | 内容 |
|---|---|
| 路径 | `samples/lan8651/lan8651_raw_frame/` |
| 覆盖特性 | TC6 数据块、TX credits、RX 重组、MAC 过滤、与主机适配器对联、PLCA 配置 |
| 关键配置 | EtherType `0x88B5`，帧长 58 B，板 MAC `02:00:00:5E:31:01`，默认 CSMA（PLCA off） |
| 串口命令 | `send [n]` `poll` `stats` `plca <on\|off> [id] [cnt] [to] [bc] [bt]` `trans <bb\|hw>` |
| 主机侧 | `sudo python3 host/lan8651/t1s_peer.py --loop --rate 5`（raw socket，AF_PACKET） |
| 自证输出 | 每收到一帧打印 dst/src/type + 前 32 字节负载，并立即回一帧；`stats` 打印收发计数 |

主循环里最关键的三行（摘自 `src/main.c`）：

```c
while (1) {
    bsp_shell_poll();
    if (now_ms() != last_poll) {          /* 至少每 1 ms 服务一次接收 */
        last_poll = now_ms();
        (void) oa_tc6_poll_rx(&s_tc6);    /* rx_chunks_available > 0 时才发一次空块事务 */
        handle_rx();                      /* 从队列里取帧、打印、回帧 */
    }
}
```

> 注意：`oa_tc6_poll_rx()` 只在 `rx_chunks_available > 0` 时真正发 SPI 事务，所以主循环里 1 ms 轮询一次**不会**把 SPI 打满；这是没有中断时最简单可靠的收包方式。有中断时把 nINT 接到 GPIO 中断，在 ISR 里置标志、主循环里做 SPI（SPI 事务不能放在 ISR 里，因为一次数据事务可能上百微秒）。

### 11.6.2 `lan8651_plca_master` —— PLCA 协调者（node 0）

| 项 | 内容 |
|---|---|
| 路径 | `samples/lan8651/lan8651_plca_master/` |
| 覆盖特性 | 协调者角色、BEACON 产生、PLCA 寄存器回读自证（PST）、PLCA 模式下的帧收发 |
| 关键配置 | `node_id=0`、`node_cnt=8`、`to_tmr=32`、`burst_cnt=0`、`burst_tmr=128` |
| 主机侧 | `t1s_host.sh plca1 8 32 0 128`（主机适配器作**跟随者 node 1**）+ 可选 `t1s_peer.py --loop` |
| 串口命令 | `status` `send [n]` `plca <on\|off> ...` `trans <bb\|hw>` `levels` |
| 自证输出 | `[status] PLCA en=1 node_id=0 ... status=0x8000 (PST=1)` + 收发计数 |
| 实测日志 | `logs/20261030-122453-lan8651_plca_master.log` |

### 11.6.3 `lan8651_plca_slave` —— PLCA 跟随者（node 1）

| 项 | 内容 |
|---|---|
| 路径 | `samples/lan8651/lan8651_plca_slave/` |
| 覆盖特性 | 跟随者角色、BEACON 同步（PST）、PLCA 模式下的帧收发 |
| 关键配置 | `node_id=1`、`node_cnt=8`、`to_tmr=32`、`burst_cnt=0`、`burst_tmr=128` |
| 主机侧 | `t1s_host.sh plca0 8 32 0 128`（主机适配器作**协调者 node 0**）+ 可选 `t1s_peer.py --loop` |
| 串口命令 | 同 master |
| 自证输出 | `[status] PLCA en=1 node_id=1 ... status=0x8000 (PST=1)` + 收发计数 |
| 实测日志 | `logs/20261030-122741-lan8651_plca_slave.log` |

两个例程共用 `samples/lan8651/common/plca_demo.c`（约 270 行），只有角色常量不同；这样"主/从"在代码里是**编译期确定**的，避免演示时敲错 `node_id`。

**通用自证点**：`status` 回读的 `PLCA STATUS` bit15（PST）应为 1；主机侧 `ethtool --get-plca-status` 显示 `Status: on`；`send` 之后 `rx/tx/replies` 计数都应增长。

### 11.6.4 一条命令跑两种角色（`lan8651_raw_frame`）

## 11.7 上板实测数据

### 11.7.1 传输层实测（关键前提）

表 11-3 是本板两种 TC6 传输方式的实测对比（日志 `logs/20261030-114140-lan8651_spi_probe.log` 与 `logs/20261030-113834-lan8651_spi_probe.log`）：

表 11-3 两种 TC6 传输方式实测对比

表 11-3 传输层实测（2026-10-30，修复后）

| 传输方式 | 控制事务 echo | 实测 SCLK | 说明 |
|---|---|---|---|
| **SPI2 硬件控制器 + GPIO CS** | ✅ 每档 32 次 0 错误（1/2/5/10/20 MHz） | 1~20 MHz（第 10 章表 10-5） | **当前默认传输层**；根因修复见第 10 章易错点 2（SCLK pad 补 LOOP_BACK 等四步） |
| GPIO 位翻转（10 nops） | ✅ 0 错误 | ≈1.59 MHz | 保留为兜底/调试通道（`trans bb`），慢 5~6 倍 |

> 本节所有吞吐数字都是在 **SPI2 硬件控制器**上测的；20 MHz 下已经跑满 10BASE-T1S 的 10 Mbps 线速率（表 11-11）。位翻转通道只在硬件 SPI 异常时用来隔离问题。

### 11.7.2 裸帧收发

对端为主机 USB-10BASE-T1S 适配器（MAC `00:1e:c0:d1:c3:37`），EtherType `0x88B5`，帧长 58 B，两端 PLCA 均关闭（CSMA/CD）。下面是**硬件 SPI 修复前**用位翻转传输（≈1.40 MHz）测到的第一版数据；修复后同一流程用 SPI2 控制器 10/20 MHz 复测的结果见 11.6.2.1（吞吐提升约 13 倍）。

**结论：双向收发成功。** 表 11-4 是本次实测的原始数字，逐条可在日志里核对。

| 项目 | 实测值 | 证据（日志行/命令） |
|---|---|---|
| 板 → 主机 | 主机 raw socket 收到 **7 帧**，`len=60`、`src=02:00:00:5e:31:01`、payload 以 `a5 5a` 开头 | `[host rx] len=60 src=02:00:00:5e:31:01 payload=a55a0019...` |
| 主机 → 板 | 板端收到 **2 帧**，`len=64`（含 4 B FCS）、`src=00:1e:c0:d1:c3:37`、`type=0x88b5` | `[rx] len=64 dst=ff:ff... src=00:1e:c0:d1:c3:37 type=0x88b5` |
| 板端应答 | 每收到一帧立即回 58 B，`replies=2` | `replied with 58 bytes (total replies 2)` |
| TC6 数据事务 | `data xfers=7`，每次 1 块（68 B） | `stats` 输出 |
| 帧统计 | `tx_frames=7 tx_bytes=406`、`rx_frames=2 rx_bytes=128`、`dropped=1` | `stats` 输出 |
| 初始 TX credits | 31 | `[tx] #0 ... tx_credits=31` |
| 控制事务 | 78 次，echo 错误 1 次 | `stats` 输出 |
| 传输层 | 位翻转，SCLK ≈1.40 MHz | `[init] LAN8651: ok (0), bit-bang SCLK 1400565 Hz` |

日志：`logs/20261030-114617-lan8651_raw_frame.log`（板端，位翻转传输）与主机侧 `/tmp/t1s_peer.log`（raw socket 对端）。
**硬件 SPI 修复后同一流程复测**：`logs/20261030-150912-lan8651_raw_frame.log`（10 MHz，`ctrl err=0`）与 `logs/20261030-151134-lan8651_raw_frame.log`（20 MHz）。

### 11.7.3 11.6.2.1 TC6 数据通道吞吐（bench）

`bench <帧数> [帧长]` 命令连续发帧、用内核周期计数器计时，测得的是**从 MCU 到 LAN8651 缓冲区的数据通道能力**（不含 10BASE-T1S 线上的 PLCA 仲裁时间），结果见表 11-11：

表 11-11 TC6 数据通道吞吐实测（2026-10-30）

| SCLK | 帧长 | 帧数 | 总耗时 | 帧率 | 有效载荷速率 | 日志 |
|---|---|---|---|---|---|---|
| 10 MHz | 60 B | 200 | 12.62 ms | 15 849 帧/s | 7.61 Mbit/s | `logs/20261030-150912-lan8651_raw_frame.log` |
| 10 MHz | 1518 B | 100 | 133.6 ms | 748 帧/s | **9.08 Mbit/s** | 同上 |
| **20 MHz** | 60 B | 200 | 10.82 ms | **18 489 帧/s** | **8.87 Mbit/s** | `logs/20261030-151134-lan8651_raw_frame.log` |

> 1518 B 帧在 10 MHz 下就达到 **9.08 Mbit/s**，已经贴着 10BASE-T1S 的 10 Mbps 线速率；20 MHz 时小帧到 8.87 Mbit/s。对比第 10 章修复前的位翻转传输（≈1.4 MHz，1518 B 帧约 0.7 Mbit/s），**吞吐提升约 13 倍**，也追平了同款芯片在 RK3506 Linux 上 8.9 Mbps 的参考值。

> **两处值得记录的细节**：
> 1. 接收帧长度是 **64 B**（= 60 B 最小帧 + 4 B FCS）。LAN8651 会把 FCS 一起交给主机，接 lwIP 时要在 `low_level_input` 里 `len -= 4`（Linux 驱动按 `ETH_FCS_LEN` 处理，本仓库 raw 示例不裁）。
> 2. `dropped=1` 是因为接收队列只有 2 个槽位、而命令一次发 5 帧时主机帧到达较快；示例代码里这是刻意保留的最小实现（第 11.8 节易错点 5）。

### 11.7.4 PLCA 主从实测

**跟随者（Follower）场景**：主机 USB 适配器当协调者（node-id 0），板端当跟随者（node-id 1），
两端 `node_cnt=8 / to_tmr=32 / burst_cnt=0 / burst_tmr=128`。

板端日志 `logs/20261030-115606-lan8651_raw_frame.log`：

```text
plca on 1 8 32 0 128
PLCA: en=1 node_id=1 node_cnt=8 to_tmr=32 burst_cnt=0 burst_tmr=128 status=0x8000 (PST=1)
...
[rx] len=64 dst=ff:ff:ff:ff:ff:ff src=00:1e:c0:d1:c3:37 type=0x88b5
      replied with 58 bytes (total replies 1)
[stats] data xfers=5 | tx_frames=5 tx_bytes=290 | rx_frames=3 rx_bytes=192 dropped=1
```

主机侧同时显示 `Status: on`。**PST=1 表示板端已经收到并同步到协调者的 BEACON**，这是 PLCA 真的在工作的
硬证据（不是"寄存器写进去了"）。并且在 PLCA 打开的状态下，双向帧收发仍然正常（表 11-9）。

**协调者（Coordinator）场景**：把角色反过来——板端 `plca on 0 8 32 0 128`、主机适配器 `node-id 1`（`t1s_host.sh plca1`）。
板端回读同样是 `status=0x8000 (PST=1)`，并且帧收发正常（收 4 帧 / 发 6 帧 / 应答 4 帧）。
表 11-9 汇总两种角色的实测结果：**两种角色都实测通过**，不是只验证了寄存器写入。

表 11-9 PLCA 主从实测（2026-10-30）

| 角色组合 | 板端命令 | 板端回读 | 帧收发 | 日志 |
|---|---|---|---|---|
| 主机=协调者(0)，板=跟随者(1) | `plca on 1 8 32 0 128` | `en=1 node_id=1 node_cnt=8 to_tmr=32 burst=0/128 status=0x8000 (PST=1)` | 收 3 帧 / 发 5 帧，PST=1 | `logs/20261030-115606-lan8651_raw_frame.log`、硬件 SPI 复测 `logs/20261030-151459-lan8651_plca_slave.log` |
| 主机=跟随者(1)，板=协调者(0) | `plca on 0 8 32 0 128` | `en=1 node_id=0 node_cnt=8 to_tmr=32 burst=0/128 status=0x8000 (PST=1)` | 收 4 帧 / 发 6 帧，replies=4 | `logs/20261030-120336-lan8651_raw_frame.log`、硬件 SPI 复测 `logs/20261030-151332-lan8651_plca_master.log` |

> 说明：PST（PLCA STATUS bit15）在 IEEE/TC14 语义里是"PLCA 状态正常（已同步到 BEACON）"。跟随者置 1 表示它听到了协调者的 BEACON；协调者自身置 1 表示 PLCA 子层工作正常。两端都读 1 才算链路真正建立在 PLCA 之上。

### 11.7.5 两个独立例程的实测（本轮复测）

表 11-10 是 `lan8651_plca_master` / `lan8651_plca_slave` 两个独立例程各自上板跑一次的结果（每次 `hwrun --secs 14`，命令序列 `status;send 2;status`）。

| 例程 | 板端角色 | 主机适配器角色 | 板端回读 | 帧收发 | 日志 |
|---|---|---|---|---|---|
| `lan8651_plca_master` | 协调者（node 0） | 跟随者（node 1） | `en=1 node_id=0 node_cnt=8 to_tmr=32 burst=0/128 status=0x8000 (PST=1)`，`ctrl err=0` | 发 5 帧 / 收 3 帧 / 应答 3 帧 | **`logs/20261030-151332-lan8651_plca_master.log`（硬件 SPI）**；位翻转版 `logs/20261030-122453-*.log` |
| `lan8651_plca_slave` | 跟随者（node 1） | 协调者（node 0） | `en=1 node_id=1 node_cnt=8 to_tmr=32 burst=0/128 status=0x8000 (PST=1)`，`ctrl err=0` | 发 4 帧 / 收 2 帧 / 应答 2 帧 | **`logs/20261030-151459-lan8651_plca_slave.log`（硬件 SPI）**；位翻转版 `logs/20261030-122741-*.log` |

两次实测都确认：**PST=1 且 PLCA 模式下双向帧收发正常**，说明 PLCA 不是"只把寄存器写进去"，而是真正完成了 BEACON 同步。

### 11.7.6 主机侧遗留配置（交接说明）

测试结束时主机 `enx001ec0d1c337` 的状态（下一位接手的人不需要重配，也不用担心影响 SSH 上行 —— 上行是 `enp3s0`）：

| 项 | 值 |
|---|---|
| 链路 | `UP`、`LOWER_UP`，10 Mbps |
| IP | `192.168.1.1/24` |
| PLCA | **off**（CSMA/CD），`node-id 255 (unconfigured)`、`node_cnt 8`、`to_tmr 32` |
| 对端脚本 | `host/lan8651/t1s_peer.py`（raw socket 收发）、`t1s_host.sh`（PLCA/CSMA 配置） |

没有改动 `enp3s0`（SSH 上行走的网口），也没有留任何常驻进程。

## 11.8 性能与调优

### 11.8.1 吞吐天花板（实测）

| 环节 | 数值 | 出处 |
|---|---|---|
| LAN8651 SPI 上限 | 25 MHz（本板实测可用 20 MHz，80 MHz 源整分频限制） | DS Table 9-9；第 10 章表 10-5 |
| 一个 68 B 数据块 | 544 bit | DS §5.2 |
| 20 MHz 下单块传输时间 | 544 / 20 M ≈ 27 µs | 计算 |
| 60 B 帧实测 | 18 489 帧/s、8.87 Mbit/s（20 MHz） | 表 11-11 |
| 1518 B 帧实测 | 9.08 Mbit/s（10 MHz，已贴线速率） | 表 11-11 |

结论：**数据通道不再是瓶颈**。10BASE-T1S 的线速率是 10 Mbps（半双工），实测 9.08 Mbit/s 有效载荷 ≈ 92% 线效率，与同款芯片在 RK3506 Linux 上 8.9 Mbps 的参考值一致。真正限制小帧的是线上的帧间隔（前导 8 B + IFG 12 B），不是 SPI。

### 11.8.2 修复前后的对比

| 阶段 | 传输层 | 1518 B 帧吞吐 | 60 B 帧吞吐 |
|---|---|---|---|
| 硬件 SPI 修复前 | GPIO 位翻转 ≈1.4 MHz | ≈0.7 Mbit/s（计算） | 受帧率限制更低 |
| 硬件 SPI 修复后 | SPI2 控制器 10 MHz | **9.08 Mbit/s** | 7.61 Mbit/s |
| 硬件 SPI 修复后 | SPI2 控制器 20 MHz | 9.0 Mbit/s（线速率封顶） | **8.87 Mbit/s** |

吞吐提升约 **13 倍**，根因修复过程见第 10 章易错点 2（时钟门控 → 真 GPIO CS → SCLK LOOP_BACK → FIFO 感知长事务）。

### 11.8.3 PLCA 参数怎么调

表 11-6 是参考实现（同一颗芯片、同一个 USB 适配器）在 RK3506 上做的参数扫描，给出了明确方向（来源：同一颗芯片在 RK3506 上的 Linux 侧实测记录，**本板待复测**）：

表 11-6 PLCA 参数扫描（来源为同款芯片的 Linux 实测，本板待复测）

| to_tmr | burst_cnt | TCP 吞吐 | 结论 |
|---|---|---|---|
| 16 | 0 | 5.74 Mbps | ❌ 太小时隙不够，冲突/重传多 |
| 32 | 0 | 8.87 Mbps | ✅ 推荐 |
| 32 | 2 | **8.94 Mbps** | ✅ 最佳，burst 减少 BEACON 开销 |
| 64 | 0 | 8.76 Mbps | ✅ 可用，时隙浪费略多 |

本仓库把这三个参数做成了运行时命令（`plca`），复测时只需改参数重跑同一套脚本。

## 11.9 易错点与注意事项

1. **现象**：PLCA 打开后 `PST` 一直是 0，ping 不通。
   **原因**：段上没有协调者（两端都不是 node 0），或两端 `node_cnt` 不一致导致 BEACON 被判定无效。
   **解决**：先确认恰好一端 `node-id 0`；两端 `node_cnt/to_tmr/burst_*` 必须完全一致（本仓库默认 8/32/0/128）。

2. **现象**：两端都配 node 0（或都配 node 1），偶发丢帧。
   **原因**：node_id 冲突，两个节点抢同一个时隙。
   **解决**：用 `ethtool --get-plca-cfg` 和板端 `plca on ...` 的打印核对两端 node id。

3. **现象**：改完 `node_id` 后前几帧丢失、之后正常。
   **原因**：先使能 PLCA 再改 ID，节点在旧时隙里发帧。
   **解决**：按 11.3.2 的顺序（关 → RST → 参数 → 使能）。

4. **现象**：板端收到自己发的帧。
   **原因**：10BASE-T1S 半双工 + PMA 回环，或对端把帧广播回送；本仓库 `lan8651_raw_frame` 用 **源 MAC 过滤**（只处理源 MAC ≠ 本机 MAC 的帧）避免误判。
   **解决**：见 `handle_rx()` 的过滤逻辑；调试时把 promiscuous 打开更方便看全流量。

5. **现象**：`rbuff` 里出现半帧/粘帧。
   **原因**：没有按数据尾的 `START_VALID/END_VALID/START_WORD_OFFSET/END_BYTE_OFFSET` 重组，而是"按块当帧"。
   **解决**：用 `oa_tc6_process_rx_payload()` 的四分支（整帧 / 帧开始 / 帧结束 / 帧中间 + "结束+开始同块"）；同时打开 **ZARFE**（零对齐接收帧），这是 LAN8650/1 errata 的 s3 要求。

6. **现象**：大帧（>1000 B）发送超时。
   **原因**：忽略 `tx_credits`，一次事务塞超过可用块数。
   **解决**：`oa_tc6_send_frame()` 按 `min(需要块数, tx_credits)` 分多次事务；credits 为 0 时发一个"空块"事务刷尾部信用（不能直接死等）。

## 11.10 lwIP 集成：从 netif 到 ping/iperf

[TC6 数据通道打通之后，剩下的工作就是把 68 B 块里的以太网帧喂给 lwIP。本轮把这件事做实了，例程是 `samples/lan8651/lan8651_lwip_iperf/`：`src/oa_tc6_netif.c` 是 netif 层，`src/main.c` 负责 bring-up、静态 IP 和 iperf 命令。

表 11-8 lwIP netif 的 5 个函数（本轮已实现）

| 函数 | 实现位置 | 要点 |
|---|---|---|
| `oa_tc6_netif_init()` | `oa_tc6_netif.c` | 设 MAC/MTU/ETHARP，`linkoutput = oa_tc6_netif_linkoutput` |
| `oa_tc6_netif_linkoutput()` | 同上 | pbuf 拼成连续帧（最多 1514 B）→ `oa_tc6_send_frame()`；MAC 负责补 FCS |
| `oa_tc6_netif_poll()` | 同上 | 发一次空块事务取 RX chunk 数 → `oa_tc6_rx_pop()` → 剥 4 B FCS → `netif->input()` |
| `oa_tc6_netif_update_link()` | 同上 | 读 Clause 22 BMSR 两次取与（LINK 位锁存）→ `netif_set_link_up/down()` |
| `oa_tc6_netif_stats()` | 同上 | TC6 与 netif 计数器，`stats` 命令打印 |

整条通路的数据流是：主机 10BASE-T1S 适配器（192.168.1.1）↔ LAN8651（TRX）↔ TC6/SPI2（20 MHz）
↔ HPM5E31（lwIP 2.1.2，NO_SYS=1，轮询）。图 11-3 左边是各层的能力上限，右边是首轮上板时 TC6 的收发统计。

![图 11-3 LAN8651 数据通路能力与 lwIP 首轮实测的 TC6 统计](figures/11/fig-11-3-lwip-throughput.png)

### 11.10.1 三个工程细节（都是遇到过的问题）

**① 接收帧要剥掉 4 B FCS。** LAN8651 把 FCS 一起交给主机（60 B 最小帧收到 64 B，见 11.6.2），
所以 `low_level_input` 里必须 `len -= 4`，否则 lwIP 会因为 IP 总长度对不上丢掉整个帧。

**② RX 不能"等有 chunk 再读"（本轮最大的易错点）。** TC6 的待收块数只在**一次数据事务的尾字**里返回，
纯收包时没有任何发送事件去刷新这个计数。第一版 `oa_tc6_poll_rx()` 写的是
`if (rx_chunks_available == 0) return 0;`，结果就是"板子不发包就永远收不到包"：

- 主机 ping 全超时（板端没有 TX 事件，RX 计数恒为 0，永远不去读）；
- TCP 只有板端主动发 SYN 那一侧能走，收 ACK 时靠下一次发送带出来的尾字，RTO 之后才能再前进。

修法：每次数据事务都发（哪怕只有 1 个空块），由调用方限速。netif 侧每 1 ms 至少发一次空块事务，
`rx_chunks_available > 0` 时立刻连续排空。

**③ RX 软件队列从 2 格加到 8 格。** 2 格时 TCP ACK 突发会丢帧（`stat_rx_dropped` 上涨）。
8 格 × 1524 B 的缓冲在 AXI_SRAM（`rx_q` 是 bss），不占 DLM。

关键代码（摘自 `src/oa_tc6_netif.c`）：

```c
/* 每 1 ms 至少发一次空块事务：尾字里带回 RX chunk 数；有 pending 时连续排空 */
uint32_t now = bsp_uptime_ms();
if ((now != s_last_rx_service_ms) || (s_tc6->rx_chunks_available > 0U)) {
    s_last_rx_service_ms = now;
    (void) oa_tc6_poll_rx(s_tc6);
}
while ((len = oa_tc6_rx_pop(s_tc6, s_rx_buf, sizeof(s_rx_buf))) > 0U) {
    len -= 4U;                       /* LAN8651 追加的 FCS */
    p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    pbuf_take(p, s_rx_buf, len);
    netif->input(p, netif);
}
```

### 11.10.2 上板结果（2026-11-02）

表 11-12 lwIP 端到端实测

| 项目 | 修复前（12:25） | 修复后（12:44） | 证据/命令 |
|---|---|---|---|
| 板端 netif | IP 192.168.1.100/24、link up | 同左，`ip` 打印 IP/mask/gw 正确 | 修复前 `logs/20261102-122541-lan8651_lwip_iperf.log`、修复后 `logs/20261102-124125-lan8651_lwip_iperf.log` |
| PHY/链路 | PHYID=0x0007c1b3、BMSR=0x0805（link=1） | 同左，PLCA off | `link` 命令 |
| 主机 ping（56 B） | 全超时 | **5/5 收到，0% 丢包，RTT 0.70~2.42 ms** | `ping -c 5 -I enx001ec0d1c337 192.168.1.100` |
| TCP 下行（主机→板，板 `iserver tcp`） | 未建链 | **5.69 Mbit/s**（5.5 MB / 8.1 s，57% 线速率） | 主机 `iperf -c 192.168.1.100 -t 8` |
| TCP 上行（板→主机，板 `iclient`） | 1.79 kbit/s | **49.9 kbit/s**（67 KB / 11 s），仍偏低 | 主机 `iperf -s` + 板端 `iclient 192.168.1.1 tcp` |
| TC6 计数（下行 8 s 后） | tx 22 / rx 14 / dropped 12 | tx 3989 / rx 4059 / **dropped 0**，status0=0x40（RESETC） | 板端 `stats` |
| 控制事务错误 | 0 | 0（ctrl xfers=178 err=0） | 同上 |

三点结论：

1. **U-11-1 的核心目标达成**：ping 5/5、TCP 下行 5.69 Mbit/s，说明 TC6 → lwIP → ARP/IP/TCP 整条链路是通的；
   5.69 Mbit/s ≈ 10 Mbps 线速率的 57%，与 10BASE-T1S 半双工 + TCP ACK 反向流量相符。
2. **修复的关键是 11.9.1 的易错点②**：RX 计数只在数据事务尾字里返回，不能"等有 chunk 再读"。
   修复后 `rx_dropped` 从 12 降到 0，ping 从全超时变成 0% 丢包。
3. **上行仍只有 49.9 kbit/s**，与下行差两个数量级。候选原因三条（都给了复现命令）：
   ① 本示例 `TCP_SND_BUF/TCP_WND` 只有 4×MSS，而 lwiperf 客户端按 MSS 逐段写，
   与主机延迟 ACK 叠加后退化成"一段一停"；
   ② 发送路径是阻塞式 `oa_tc6_send_frame()`，一次最多塞 31 个 chunk，突发时会把主循环占住；
   ③ lwIP 客户端发送由 `tcp_sent` 回调驱动，NO_SYS 轮询下没有独立的发送泵。
   复现：`iperf -s` + 板端 `iclient 192.168.1.1 tcp`；下一步可把 SND_BUF 调到 16×MSS、打开
   `TCP_WRITE_FLAG_MORE` 批量写，或在主循环里加发送泵。

## 11.11 优劣评价与同类对比

表 11-7 数据通路方案对比

| 方案 | 数据通路实现 | 10 Mbps 下的实际吞吐 | 多节点 | 备注 |
|---|---|---|---|---|
| MCU 内置 MAC + 10BASE-T1S PHY（如 HPM6E00 + 外置 PHY） | ENET DMA | ≈9 Mbps | 依赖 PHY PLCA | 最省 CPU |
| **MCU + LAN8651 MAC-PHY（本章）** | SPI + TC6 | 受 SPI 带宽限制（表 11-3） | ✅ 硬件 PLCA | 引脚少、可挂任意 MCU |
| MCU + LAN8670（PHY only，需 MAC） | 内置 ENET | ≈9 Mbps | ✅ | 需要 MCU 自带 MAC |
| CAN FD（第 08 章） | MCAN | 2~8 Mbps | ✅ 仲裁 | 生态最成熟 |

## 11.12 参考来源与扩展阅读

1. LAN8650/1 Data Sheet DS60001734E §5（SPI/TC6）、§7（PLCA）：<https://www.microchip.com/en-us/product/LAN8651>（核对日期 2026-11-02）。
2. AN1760 Configuration Application Note rev F（LAN865x Rev.B0/B1），同上产品页。
3. LAN8650/1 Errata DS80001075（s3：零对齐接收帧），同上产品页。
4. OPEN Alliance TC6 V1.1：<https://opensig.org/download/document/OPEN_Alliance_10BASET1x_MAC-PHY_Serial_Interface_V1.1.pdf>；TC14（PLCA 寄存器映射）见 OPEN Alliance 规范页。
5. Linux 内核 OA-TC6 实现：<https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/oa_tc6.c>（RX 尾字/credits 语义的权威参考）。
6. 同款芯片在 RK3506 Linux 上的 8.9 Mbps/PLCA 参数扫描来自内网工程（未公开），只引用结论。
7. 本仓库第 10 章（TC6 控制通道与 bring-up）、第 13 章（ENET + lwIP 的对比数据）。
8. [lwIP 官方文档](https://www.nongnu.org/lwip/2_1_x/index.html)、SDK `samples/lwip/lwip_iperf/`（<https://github.com/hpmicro/hpm_sdk>）。

## 11.13 待确认项与风险

表 11-13 列出本章未闭环的条目，**已逐条复核（2026-11-02）**：每条要么有实测数据，要么写明已知限制与验证命令，不做"看起来跑通了"的推断。

| 编号 | 待确认项 | 现状 | 验证方法 |
|---|---|---|---|
| U-11-1 | lwIP ping/iperf 端到端 | **已实测（2026-11-02）**：ping 5/5（RTT 0.7~2.4 ms）、TCP 下行 5.69 Mbit/s（57% 线速率）、RX 丢帧 0；上行仍 49.9 kbit/s，瓶颈定位到发送节奏（3 条候选见 11.9.2） | 复测命令：板端 `ip;link;iserver tcp`，主机 `ping -c 5` / `iperf -c 192.168.1.100 -t 8`；上行 `iperf -s` + 板端 `iclient 192.168.1.1 tcp` |
| U-11-2 | 硬件 SPI2 为何 CS/SCLK 不到引脚 | **已实测**（2026-10-30，见第 10 章 U-10-1 易错点 2）：时钟门控 + 软件 CS + SCLK LOOP_BACK + FIFO 感知长事务，四步后 1~20 MHz 各 32 次读 0 错误 | 已不需要再测；留 `hwchk`/`hwtx` 作为回归自证 |
| U-11-3 | PLCA 主从在 10BASE-T1S 段上的**吞吐** | **已知限制**：PLCA 主从与裸帧收发已验证（表 11-7），但 PLCA + lwIP 组合未联调（lwIP 例程默认 CSMA/CD，主机适配器 PLCA 脚本 `t1s_host.sh plca0/plca1` 可切换）；吞吐数据因此仍缺 | 现场按 11.7.3 节参数扫描：主机 `t1s_host.sh plca0 8 <to> <bc>` + 板端 `plca on 1 ...`，再跑 iperf 对比 to_tmr=16/32/64、burst=0/2 |
| U-11-4 | 25 MHz SPI 稳定性 | **已实测（2026-10-30 表 10-5）**：80 MHz 源无法整分频出 25 MHz，20 MHz 是上限；20 MHz 下 32 次读 0 错误，带宽已够跑满 10 Mbps 线速率 | 复核命令 `speed 25000000;scan`（返回仍约 20 MHz） |
| U-11-5 | 多节点（>2）PLCA | **已知限制**：只有 1 块板 + 1 个 USB 适配器，无法构造 4/8 节点 | 增加 LAN8651 节点后：各节点唯一 `node_id`、`node_cnt` 一致，读 PLCA STATUS 的 PST=1，再扫吞吐（参考表 11-6） |
