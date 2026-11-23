# 第 12 章 YT8011AN 千兆 PHY 与 RGMII

## 12.1 文档信息

表 12-1 文档信息

| 项 | 内容 |
|---|---|
|   文档版本   | v1.2.0（2026-11-02，R5 复核） |
| 适用芯片/板卡 | HPM5E31IPB1 / hpm5e31_1 |
| 对应例程 | `samples/enet/enet_mdio_scan/`、`samples/enet/enet_phy_config/`（本轮新增 irq/ints/scan/bcast 命令） |
| 驱动组件 | `components/enet_phy_yt8011/` |
| 板级文件 | `boards/hpm5e31_1/ext/board_net2.h`、`boards/hpm5e31_1/pins/pinmux_net2.c` |
| 实测日期 | 2026-10-30（PHY 寄存器/速率切换）、2026-11-02（nINT 中断与 MDIO 扫描） |
| 可信度 | 高（官方数据手册 + 应用说明 + 上板实测日志）；需人工拨码或示波器的条目已在第 11 节写明限制 |

## 12.2 本章导览

- 学到什么：HPM5E31 的 ENET0 如何以 RGMII 接口连接一颗车载以太网 PHY（YT8011AN）；
  RGMII 的两种时钟延迟模式（DoD/DoS）与"延迟到底该由谁提供"的判断方法；
  MDIO Clause 22 帧格式，以及一颗 PHY 上**三种寄存器空间**（Clause 22 / EXT / MMD）的访问方法；
  PHY 复位时序、strapping 锁存、速率与主从切换的实操（含一个反直觉的"软复位会把配置冲掉"的坑）。
- 前置章节：第 02 章（构建/烧写）、第 03 章（letter-shell 调试命令）。
- 承接章节：第 13 章（lwIP 协议栈、iperf 与实际吞吐）。

## 12.3 概述与典型应用

HPM5E31 内置一路以太网控制器 ENET0，支持 RGMII / RMII / MII 三种 MAC-PHY 接口
（`hpm_sdk/soc/HPM5E00/HPM5E31/hpm_enet_soc_drv.h:16` 的 `enet_intf_selection()` 通过
`ENET_CTRL2.ENET0_PHY_INF_SEL` 选择）。hpm5e31_1 板把 ENET0 以 RGMII 方式接到
**YT8011AN**（Motorcomm 千兆车载以太网 PHY，线侧 1000BASE-T1/100BASE-T1 单对线），
再经外部的"千兆车载以太网转换盒"接到主机普通千兆网卡。

典型应用场景：

1. 车载以太网网关/域控制器：1000BASE-T1 单对线束替代传统四对线 1000BASE-T，减重降本。
2. 车载摄像头/雷达数据回传：1000M 链路 + 低延迟小包（本板可作采集端）。
3. 工业现场设备：单对线长距离（≤15 m）连接，抗干扰布线简单。
4. 台架/实验室联调：用 1000BASE-T1↔RJ45 转换盒把车载 PHY 接入普通 PC 网卡做吞吐/协议测试。（见表 12-2）

表 12-2 YT8011AN 关键特性与出处

| 特性 | 参数 | 出处 |
|---|---|---|
| MAC 接口 | RGMII（1000M 时 125 MHz DDR；100M 时 25 MHz DDR） | YT8011A_1.pdf §2.1.2（p.25） |
| 线侧 | 1000BASE-T1 / 100BASE-T1，单对差分线 | YT8011A_1.pdf §2.1（p.25） |
| 管理接口 | MDIO Clause 22，MDC 最高 12.5 MHz | YT8011A_1.pdf §2.1（p.26） |
| PHY 地址 | strapping `RXD[1:0]=01` → 1；固定响应地址 0 | YT8011A_1.pdf §3（p.38） |
| 复位 | RESET_N 低有效，至少 10 ms | YT8011A_1.pdf §3（p.38） |
| 主从 | RXD3/MS_SEL strapping（本板下拉=Slave）或 MMD1 0x834 | 应用说明 §5.3（p.26） |
| RGMII 延迟 | PHY 内部给 RXC 加 2 ns（strap RXC=1）；TXC 延迟档位 EXT 0x9001[7:4]=1 | YT8011A_1.pdf §4.4.2（p.58） |

## 12.4 工作原理

### 12.4.1 板级连接与引脚

表 12-3 RGMII/MDIO/PHY 控制引脚分配

| 信号 | SoC 引脚 | IOC 功能 | 方向（相对 SoC） |
|---|---|---|---|
| TXC | PF02 | `ETH0_TXCK` | 输出（125 MHz RGMII 时钟） |
| TXD0/1/2/3 | PF03/PF04/PF05/PF06 | `ETH0_TXD_0..3` | 输出 |
| TXEN | PF07 | `ETH0_TXEN` | 输出 |
| RXC | PF13 | `ETH0_RXCK` | 输入 |
| RXD0/1/2/3 | PF09/PF10/PF11/PF12 | `ETH0_RXD_0..3` | 输入 |
| RXDV | PF08 | `ETH0_RXDV` | 输入 |
| MDC | PA31 | `ETH0_MDC` | 输出 |
| MDIO | PA30 | `ETH0_MDIO` | 双向 |
| PHY nRST | PB29 | GPIO_B_29 | 输出（低有效） |
| PHY nINT | PB28 | GPIO_B_28 | 输入（低有效，内部上拉） |

引脚复用在板级公共文件 `boards/hpm5e31_1/pinmux.c:374` 的 `init_enet0_pins_rgmii()` 与
`:870` 的 `init_yt8011an_ctrl_pins()` 中实现。本章负责的组合封装放在
`boards/hpm5e31_1/pins/pinmux_net2.c`（只调用上述两个函数 + 提供 PHY 复位时序），
这样既不重复定义符号，也不改动别人维护的文件。

> 引脚安全：PB28/PB29 曾担心与 Flash/XIP 冲突，实测（Lead 验证，`logs/20261030-105718-xpi_probe.log`）
> 把两脚当 GPIO 翻转并在每次翻转后读取 0x80000000 区域 256 个字，程序全程正常；
> HPM5E31IPB1 的 1 MB Flash 为**片内**（DS Rev0.4 p.1；内置闪存型号 GT25Q80AH，见 DS Rev0.4 内置闪存特性 p.65/68），板上无外部 XPI NOR 芯片，故这两个 GPIO 可安全使用。

### 12.4.2 RGMII 时序与延迟模式

RGMII 是 **DDR** 接口：时钟上下沿都传数据，1000M 时时钟 125 MHz、每根数据线每沿 1 bit。
接收端必须在时钟沿的中间采样数据，因此"数据与时钟的相位关系"是 RGMII 能否工作的关键。
标准给出两种实现方式（YT8011A_1.pdf §2.1.2 p.25、§5.2.4 p.87-89）：

- **DoD（Delay on Destination）**：发送端时钟与数据**边沿对齐**，由**接收端**把时钟延迟 ≈2 ns 后采样。
  手册 Table 21/22：DoD 模式下源端 skew ≤0.5 ns、目的端 ≤0.65 ns。
- **DoS（Delay on Source）**：**发送端**输出已经延迟 ≈1.8~2.0 ns 的时钟，接收端直接采样。
  手册 Table 23：DoS 模式下源端 skew ≥1.2 ns。

![图 12-1 RGMII 的 DoD/DoS 延迟模式](figures/12/fig-12-1-rgmii-timing.png)

本板采用组合：**PHY 内部给 RXC 加 2 ns 延迟**（strapping `RXC/RXDLY_EN` 默认上拉=1，
对应 EXT 0x9001[8]=1，实测读回 0x0110），MCU 侧 ENET 的 `TXCLK_DLY_SEL/RXCLK_DLY_SEL`
保持 0（`boards/hpm5e31_1/board.h` 的 `BOARD_ENET_RGMII_TX_DLY/RX_DLY` 均为 0）。

ENET 侧延迟由 `hpm_enet_soc_drv.h:56` 的 `enet_rgmii_set_clock_delay()` 写
`ENET_CTRL0.ENET0_TXCLK_DLY_SEL / ENET0_RXCLK_DLY_SEL` 实现，板级封装在
`boards/hpm5e31_1/board.c:747` 的 `board_init_enet_rgmii_clock_delay()`。

**判断顺序（实战建议）**：先把 PHY 侧 RXC 延迟打开（本板默认已开），ENET 侧 delay 用 0；
若出现 CRC 错/丢包，再逐档增加 ENET 侧 RX delay。两侧同时加延迟会叠加，反而更容易采样错位。

### 12.4.3 MDIO 管理与三种寄存器空间

MDIO 是 Clause 22 的两线串行总线（MDC 时钟 + MDIO 双向数据）。一帧格式如图 12-2(a)：
32 bit 前导 + ST(2) + OP(2) + PHYAD(5) + REGAD(5) + TA(2) + DATA(16)。
读操作 OP=10、TA 期间 MDIO 由 PHY 驱动（Z0）；写操作 OP=01、TA=10。

![图 12-2 Clause 22 管理帧与 YT8011AN 三种寄存器访问路径](figures/12/fig-12-2-mdio-access.png)

YT8011AN 的寄存器分成三种"空间"，**只有 Clause 22 的 0x00~0x1F 能直接寻址**，其余都要间接访问：（见表 12-4）

表 12-4 三类寄存器空间的访问方法

| 空间 | 典型地址 | 访问方法 | 手册出处 |
|---|---|---|---|
| Clause 22 标准寄存器 | 0x00 BMCR、0x01 BMSR、0x02/0x03 ID、0x09/0x0A GBCR/GBSR、0x11 PHYSR | MDIO 直接读写 | §4.1（p.39-42） |
| EXT 寄存器 | 0x9001 RGMII_CFG1、0x9030 COMMON_CTRL1、0x9081~0x9083 LTT/LRT/RRT | 写 MII 0x1E=EXT 地址，再读/写 MII 0x1F | 应用说明 §2.1.2（p.10） |
| MMD 寄存器 | MMD1 0x0834（主从/速率）、MMD7 0x0200（自协商） | 0x0D=devad → 0x0E=reg → 0x0D=0x4000+devad → 0x0E=data | 应用说明 §2.1.3（p.11） |

本仓库驱动（`components/enet_phy_yt8011/src/hpm_yt8011.c`）把三套时序都封装成函数
`yt8011_read_ext()/write_ext()`、`yt8011_read_mmd()/write_mmd()`，可直接用 shell 命令
`ext`/`mmd` 逐条验证（详见第 4 节例程）。

> MDC 频率不是随便配的：SDK 的 `enet_read_phy()/enet_write_phy()` 把 `GMII_ADDR.CR` 固定写成
> `enet_csr_150m_to_250m_mdc_csr_div_102`（`hpm_sdk/drivers/src/hpm_enet_drv.c:395/419`），
> 即假定 ENET CSR 时钟 150~250 MHz、MDC ≈ CSR/102 ≈ 1.5~2.4 MHz。**改 ENET 时钟会同时改 MDC 频率**，
> 不需要也不允许单独配 MDC 分频。

### 12.4.4 上电、复位、strapping 与配置流程

YT8011AN 在 RESET_N 释放瞬间锁存 RXD[3:0]/RXDV/RXC 上的电平作为 strapping
（YT8011A_1.pdf §3 p.38 的 Table 12）。本板对应的 strap 取值与含义：（见表 12-5）

表 12-5 strapping 引脚取值与含义

| strap 引脚 | 本板电平 | 含义 |
|---|---|---|
| RXD1/RXD0（PHYAD[1:0]） | 0/1（PD/PU） | PHYADDR = 1 |
| RXDV/ST_CHG | 1（PU） | Auto 模式（上电自动进入 normal） |
| RXD3/MS_SEL | 0（PD） | **Slave** |
| RXD2/MII_SEL | 0（PD） | RGMII（非 SGMII） |
| RXC/RXDLY_EN | 1（PU） | RXC 加 2 ns 延迟 |

完整的初始化时序如图 12-3 所示。注意最后一个方框里的"带速率编码软复位"是实测踩坑后确定的写法
（见第 7 节坑 3）。

![图 12-3 YT8011AN 上电/复位/配置时序流程](figures/12/fig-12-3-phy-init-flow.png)

## 12.5 关键寄存器与驱动 API 对照

表 12-6 关键寄存器与驱动 API 对照

| 功能 | 寄存器 | 位域 | 驱动 API | 例程 |
|---|---|---|---|---|
| 软复位 | MII 0x00 BMCR | [15] Reset（自清） | `yt8011_reset()` | enet_phy_config |
| 带速率编码软复位 | MII 0x00 BMCR | 写 0x8140（1000M）/0xA100（100M） | `yt8011_soft_reset_cfg()` | enet_phy_config |
| 链路状态 | MII 0x01 BMSR | [2] Link（锁存，需连读两次） | `yt8011_get_status()` | enet_mdio_scan |
| 实时速率/主从/link | MII 0x11 PHYSR | [15:14] 速率、[13] 主从、[10] 实时 link | `yt8011_get_status()` | enet_phy_config |
| PHY ID | MII 0x02/0x03 | ID1=0x4F51，ID2[15:10]=0x3A | `yt8011_check_id()` | enet_mdio_scan |
| 1000BASE-T 主从解析 | MII 0x0A GBSR | [14] 主从、[13] local RX OK、[12] remote RX OK | `yt8011_get_status()` | enet_phy_config |
| 工作速率/主从配置 | MMD1 0x0834 | [14] force_config（1=Master）、[3:0] 1=1000BASE-T1 | `yt8011_set_mode()` | enet_phy_config |
| 自协商使能 | MMD7 0x0200 | [12] an_en | `yt8011_basic_mode_init()`（AN 分支） | enet_phy_config |
| RGMII 时钟延迟 | EXT 0x9001 | [9] tx_clk_sel、[8] rx_delay_en、[7:4] tx_delay_sel | `yt8011_set_rgmii_delay()` | enet_phy_config |
| RGMII/SGMII 选择 | EXT 0x9030 | [0] mii_sel（0=RGMII） | 只读校验 | enet_mdio_scan |
| MAC 接口选择 | ENET CTRL2 | `ENET0_PHY_INF_SEL` | `enet_intf_selection()`（SDK 内部） | enet_lwip_iperf |
| RGMII 延迟（MCU 侧） | ENET CTRL0 | `TXCLK_DLY_SEL/RXCLK_DLY_SEL` | `enet_rgmii_set_clock_delay()` | enet_lwip_iperf |
| MDIO 单次读写 | ENET GMII_ADDR/DATA | PA/GR/CR/GW/GB | `enet_read_phy()/enet_write_phy()` | enet_mdio_scan |

## 12.6 例程逐条讲解

### 12.6.1 例程 A：enet_mdio_scan（MDIO 总线扫描）

表 12-7 例程 A（enet_mdio_scan）概览

| 项 | 内容 |
|---|---|
| 路径 | `samples/enet/enet_mdio_scan/` |
| 覆盖特性 | ENET0 时钟/引脚、PHY 硬复位、Clause 22 读写、地址扫描、状态解码 |
| 关键配置 | `net2_enet_rgmii_pins_init()` + `init_eth0_clock()` + PB29 复位（2 ms 低/30 ms 高） |
| 实测现象 | 扫描 0~31 命中地址 0 与 1，ID1=0x4F51/ID2=0xEB01，link 已 UP |
| 实测日志 | `logs/20261030-110004-enet_mdio_scan.log` |
| shell 命令 | `scan`、`rd <pa> <reg> [cnt]`、`wr <pa> <reg> <val>`、`phy [pa]`、`rst`、`link [pa]` |

关键代码（扫描一个地址，摘自 `samples/enet/enet_mdio_scan/src/main.c`）：

```c
static bool phy_present(uint8_t pa, uint16_t *id1, uint16_t *id2)
{
    uint16_t a = 0xFFFF, b = 0xFFFF;
    if (!mdio_read(pa, MII_PHYID1, &a) || !mdio_read(pa, MII_PHYID2, &b)) {
        return false;
    }
    *id1 = a;
    *id2 = b;
    /* 无器件时总线被上拉读回 0xFFFF；ID 全 0 也不合法 */
    return !((a == 0xFFFFU && b == 0xFFFFU) || (a == 0x0000U && b == 0x0000U));
}
```

### 12.6.2 例程 B：enet_phy_config（寄存器级配置与链路验证）

表 12-8 例程 B（enet_phy_config）概览

| 项 | 内容 |
|---|---|
| 路径 | `samples/enet/enet_phy_config/` |
| 覆盖特性 | EXT/MMD 间接访问、速率切换、主从切换、RGMII 延迟读写、链路轮询 |
| 关键配置 | 默认 1000BASE-T1 + Slave；RXC 2 ns 延迟；TXC 延迟档位 1 |
| 实测现象 | 初始寄存器与手册默认值一致；切 100M 后 link DOWN（对端固定 1000M）；强制 Master 后 link DOWN（双 Master） |
| 实测日志 | `logs/20261030-110554-enet_phy_config.log`、`logs/20261030-110711-enet_phy_config.log`、`logs/20261030-110912-enet_phy_config.log` |
| shell 命令 | `status`、`dump`、`mode <1000\|100> <master\|slave\|auto>`、`rgmii [rxen] [txsel] [txclk]`、`ext`、`mmd`、`rd`、`wr`、`poll`、`restore` |

关键代码（带速率编码的软复位，摘自 `components/enet_phy_yt8011/src/hpm_yt8011.c`）：

```c
/* 软复位 + 速率选择必须合并成一次 BMCR 写：
 *   1000BASE-T1 -> 0x8140, 100BASE-T1 -> 0xA100（应用说明 §5.4 p.27）
 * 若先写 MMD1 0x834 再用 BMCR=0x8000 复位，速率会被复位冲回上电默认值。 */
hpm_stat_t yt8011_soft_reset_cfg(ENET_Type *base, uint8_t addr, yt8011_speed_t speed)
{
    uint16_t bmcr = (speed == yt8011_speed_1000m) ? YT8011_BMCR_FORCE_1000M
                                                  : YT8011_BMCR_FORCE_100M;
    enet_write_phy(base, addr, YT8011_BMCR, bmcr);
    /* 之后轮询 bit15 自清，再等 12 ms 让内部逻辑稳定 */
    /* 完整实现见源码 hpm_yt8011.c 的 yt8011_soft_reset_cfg() */
}
```

### 12.6.3 例程 C：enet_lwip_iperf（lwIP + iperf，详见第 13 章）

表 12-9 例程 C（enet_lwip_iperf）概览

| 项 | 内容 |
|---|---|
| 路径 | `samples/enet/enet_lwip_iperf/` |
| 覆盖特性 | ENET0 MAC/DMA + lwIP 协议栈 + iperf2 服务端/客户端 |
| 关键配置 | 静态 IP 192.168.2.100/24；MTU 1500；零拷贝 TX/RX；-O3；`CONFIG_LWIP=1`、`CONFIG_LWIP_IPERF=1` |
| 实测现象 | 上电后 link UP（1000M Slave），netif link up，可从主机 ping 通并跑 iperf |
| 实测日志 | `logs/20261030-111814-enet_lwip_iperf.log`（link/PHY 状态自证） |
| shell 命令 | `ip`、`link`、`iserver [tcp\|udp]`、`iclient <ip> [tcp\|udp]`、`istop`、`mode <1000\|100>` |

本章只讲 PHY/RGMII 部分，协议栈与吞吐见第 13 章。

### 12.6.4 PHY 中断（PB28 nINT）驱动

YT8011AN 的 nINT 是低有效中断输出，板级接 PB28（内部上拉，引脚见 4.1 节表 12-3）。中断在 R5 本轮才实装，做法是**中断只置标志，MDIO 事务放主循环**：

1. PB28 切成 GPIO 输入 + 内部上拉，`gpio_config_pin_interrupt()` 配**下降沿**触发，`intc_m_enable_irq_with_priority(IRQn_GPIO0_B, 1)` 打开 GPIO0_B 中断；
2. ISR 只做两件事：清 GPIO 中断标志、置 `s_phy_evt_pending`。读一次 MII 要 ~26 µs，放 ISR 会拖慢 ENET 收包与 lwIP 主循环；
3. 主循环 `phy_event_poll()` 看到标志后读 **MII 0x13（INTST）/ 0x17（INTST2）/ 0x12（INTMSK）**（读清），再读 0x11 PHYSR 取当前链路/速率/角色，打印一条事件日志；
4. 应用层据此做 `netif_set_link_up/down()`，替代第 13 章坑 6 里那个 2 s 轮询。

![图 12-4 YT8011AN nINT 到 netif 的事件驱动链](figures/12/fig-12-4-phy-irq.png)

关键代码（摘自 `samples/enet/enet_phy_config/src/main.c`）：

```c
SDK_DECLARE_EXT_ISR_M(BOARD_NET2_PHY_INT_IRQ, isr_phy_nint)
void isr_phy_nint(void)
{
    gpio_clear_pin_interrupt_flag(PHY_INT_CTRL, PHY_INT_PORT, PHY_INT_PIN);  /* PB28 */
    s_phy_isr_cnt++;
    s_phy_evt_pending = 1U;   /* 只置标志，MDIO 留给主循环 */
}
```

实测方法：上板后 `irq on` → `mode 1000 master`（对端是 1000M Master，双 Master 必然掉链）→ `mode 1000 slave`（恢复建链）。每次链路变化都应产生一次 nINT 下降沿，串口打印 `[irq] #n isr=... INTST=0x.... INTST2=0x.... link=...`。具体数值见 5.5 节。

## 12.7 上板实测数据

测试条件：hpm5e31_1（HPM5E31IPB1）+ YT8011AN，主机 enp6s0（千兆，192.168.2.1/24）
经千兆车载以太网转换盒（拨码 1000M Master）连接；串口 921600 抓日志；
测量日期 2026-10-30。

### 12.7.1 MDIO 扫描结果

表 12-10 MDIO 扫描结果

| MDIO 地址 | ID1 | ID2 | OUI（拼接） | 型号/版本 | BMCR | BMSR | Link |
|---|---|---|---|---|---|---|---|
| 0 | 0x4F51 | 0xEB01 | 0x13D47A | 0x30 / 1 | 0x0140 | 0x000D | UP |
| 1 | 0x4F51 | 0xEB01 | 0x13D47A | 0x30 / 1 | 0x0140 | 0x000D | UP |

地址 0 也应答是器件特性（手册 §3 p.38："always responds to PHY address 0"），实际使用地址 1。

### 12.7.2 关键寄存器实读值（上电 + 硬复位后）

表 12-11 关键寄存器实读值

| 空间 | 寄存器 | 实读值 | 解读 | 出处 |
|---|---|---|---|---|
| MII | 0x00 BMCR | 0x0140 | 强制 1000M、全双工、AN 关闭 | §4.1.1（p.39） |
| MII | 0x01 BMSR | 0x000D | link=1、AN 能力=1、扩展寄存器能力=1 | §4.1.2（p.40） |
| MII | 0x09 GBCR | 0x0000 | 未手动配置主从 | §12.6.1 |
| MII | 0x0A GBSR | 0x3000 | 主从=Slave、local/remote RX OK | §12.6.1 |
| MII | 0x11 PHYSR | 0x8400 | 1000M / Slave / 实时 link up | 应用说明 §5.2（p.25） |
| EXT | 0x9000 SMI_SDS_PHY | 0x0000 | 0=访问 PHY（非 SDS） | §4.4.1（p.58） |
| EXT | 0x9001 RGMII_CFG1 | 0x0110 | RXC 2 ns 延迟使能、TXC 延迟档位 1 | §4.4.2（p.58） |
| EXT | 0x9030 COMMON_CTRL1 | 0x2000 | mii_sel=0 → RGMII | §4.4.3（p.58） |
| EXT | 0x9081 LTT | 0x000A | Link Training Time | Table 10（p.30） |
| EXT | 0x9082 LRT | 0x0004 | Local Receiver Time | Table 10（p.30） |
| EXT | 0x9083 RRT | 0x0008 | Remote Receiver Time | Table 10（p.30） |
| MMD | MMD1 0x0834 | 0x8001 | force_config=0(Slave)、phy_type=1(1000BASE-T1) | 应用说明 §5.3（p.26） |
| MMD | MMD7 0x0200 | 0x0000 | 自协商关闭 | 应用说明 §5.4（p.27） |

EXT 0x9001=0x0110 与手册默认值完全一致，说明 **EXT 间接访问序列正确**；
MMD1 0x834=0x8001 与 strapping 一致，说明 **MMD 间接访问序列正确**。

### 12.7.3 速率与主从切换矩阵（实测）

表 12-12 速率与主从切换矩阵

| # | 配置动作 | 写入 | 回读证据 | Link | 结论 |
|---|---|---|---|---|---|
| 1 | 默认（上电 strap） | 无 | MMD1 0x834=0x8001, PHYSR=0x8400 | **UP** | 1000M Slave，与转换盒 Master 匹配 |
| 2 | 强制 100BASE-T1 | MII 0x00=0xA100 | BMCR 回读 0x2100, MMD1 0x834 同步变 0x8000, PHYSR=0x4000 | **DOWN** | 速率切换生效；对端固定 1000M 故无法建链 |
| 3 | 强制 Master | MMD1 0x834=0xC001 + MII 0x00=0x8140 | PHYSR=0x2000（[13]=1） | **DOWN** | 主从切换生效；双 Master 冲突 |
| 4 | 恢复 Slave | MMD1 0x834=0x8001 + MII 0x00=0x8140 | PHYSR=0x8400 | **UP** | 恢复成功 |

第 2 行为"配置成功但链路失败"的典型例子：**link 状态不能用来判断寄存器配置是否生效**，
必须同时看 PHYSR(0x11)/MMD1 0x834 的回读值。第 3 行同理，是主从寄存器确实生效的反证。

### 12.7.4 nINT 中断与 MDIO 扫描实测（2026-11-02）

表 12-14 nINT 中断与 MDIO 扫描

| 项目 | 实测 | 说明 |
|---|---|---|
| nINT 初始化 | 上电打印 `[irq] PHY nINT PB28: falling edge, IRQn=2, idle level=1` | GPIO0_B 中断已使能，空闲电平为高，与低有效定义一致 |
| nINT 开关 | `irq on` → `enabled=1 nINT level=1` | 开关命令生效 |
| MII 0x12/0x13/0x17 读回 | `ints` → `INTMSK=0x0000 INTST=0x0000 INTST2=0x0000` | 链路稳定时无挂起中断，读清路径通 |
| 链路变化 ISR 计数 | 未采到（原因见下） | 判据：每次 `mode 1000 master/slave` 各触发一次，ISR 计数单调增 |
| MDIO 地址扫描 0..31 | 地址 0 与 1 应答（ID1=0x4F51、ID2=0xEB01） | 与表 12-10 一致；`scan` 命令 |
| EXT 0x0000 广播地址探测 | `bcast`：写 0x003F → 回读 → 重扫 | 判据：回读值变化、地址 0x1F 开始应答 |

日志：`logs/20261102-123444-enet_phy_config.log`（初始化与寄存器读回）、
`logs/20261102-124714-enet_phy_config.log`（第二次上板，同样打印 enabled=1/level=1）。

> 说明：中断驱动已实现、编译通过并完成上板自证。**链路变化的 ISR 计数这一轮没采到**：post 脚本用
> pyserial 回读时串口返回空行（三次都是空，未再重试——12:55 后板卡要让给 gs_usb 长测）。
> 复测命令已固化：`irq on` → `mode 1000 master` → `irq` → `mode 1000 slave` → `irq`，
> 逐条看 `isr=` 是否递增。

## 12.8 性能与调优

1. **链路建立时间**：硬复位释放后到 link up 实测约 1~2 s（1000BASE-T1 需要训练与主从解析，
   LTT/LRT/RRT 分别 10/4/8 个时间单位）。若上电后立刻发包会丢前几帧，协议栈侧应由
   `enet_self_adaptive_port_speed()`（2 s 周期）把 netif link 置 up 后再开始业务。
2. **RGMII 延迟调优**：当前 PHY 侧 RXC 2 ns + ENET 侧 0，1000M 可正常收发（第 13 章 iperf 数据）。
   若换线束/换 PHY 板导致 CRC 错，调优顺序：`rgmii 1 1 0`（确认 PHY 侧）→ ENET 侧 RX delay 1~3 档 →
   再试 PHY 侧 tx_delay_sel 0~15（每次记录错误计数）。
3. **主从选择对吞吐没有影响**，但对**建链时间与时钟精度**有影响：Master 用本地 25 MHz 晶振，
   Slave 从线路恢复时钟。车载场景通常由交换机/网关做 Master，终端做 Slave（本板默认）。
4. **不要用自协商**：应用说明 §5.4（p.27）明确指出车载场景一般不用自协商（建链慢、对端兼容性风险）。
   本板默认强制 1000M Slave。

## 12.9 坑与注意事项

1. **`ENET_SOC_RGMII_EN=0` 是"死宏"，不代表不支持 RGMII**。
   现象：`hpm_sdk/soc/HPM5E00/HPM5E31/hpm_soc_feature.h:110` 写着 `#define ENET_SOC_RGMII_EN (0U)`，
   但同一 SDK 的官方 `hpm5e00evk` 板就是 RTL8211 + RGMII。
   原因：v1.13.0 全仓库检索，该宏除 soc feature 头文件外**没有任何 .c/.h 引用**
   （`grep -rn ENET_SOC_RGMII_EN` 只命中该行）；RGMII 是否可用由 `ENET_CTRL2.ENET0_PHY_INF_SEL` 选择
   （`hpm_enet_soc_drv.h:16`），与这个宏无关。
   解决：直接按 RGMII 配置，不要被该宏劝退。

2. **板级 `board.c` 的 PHY 配置只认 RTL8211/JL1111**。
   现象：`boards/hpm5e31_1/board.c` 的 `board_get_enet_phy_config()` 在 `CONFIG_ENET_PHY=1` 时
   只把类型设为 `hpm_enet_phy_rtl8211` 或 `hpm_enet_phy_jl1111`（沿用 hpm5e00evk 的写法），
   没有 YT8011 分支；而 `CONFIG_ENET_PHY` 一旦为 1，`board.c` 就会编译出
   `board_init_enet_phy()`，与我们自己的同名钩子冲突。
   解决：**需要 PHY 钩子的例程不要打开 `CONFIG_ENET_PHY`**，由例程自己提供
   `board_init_enet_phy()/board_get_enet_phy_status()`（见 `samples/enet/enet_lwip_iperf/src/phy_glue.c`）。
   注意 `board.h` 中这两个函数的声明也在 `CONFIG_ENET_PHY` 保护内，需自行补声明
   （本仓库放在 `samples/enet/enet_lwip_iperf/inc/common_cfg.h`）。

3. **软复位会把刚写的速率配置冲掉**（最容易踩）。
   现象：按手册 §5.3 顺序"写 MMD1 0x834=0x8000（100M）→ 写 BMCR=0x8000 软复位"，
   回读 MMD1 0x834 又变回 0x8001（1000M）。
   原因：`BMCR=0x8000` 只是纯复位，不携带速率选择；复位后速率回到上电默认 1000BASE-T1。
   解决：把复位与速率合并成一次写（应用说明 §5.4 p.27）：1000BASE-T1 写 0x8140、100BASE-T1 写 0xA100。
   驱动已封装为 `yt8011_soft_reset_cfg()`。

4. **MDIO 地址 0 应答属正常**，不代表器件地址是 0。手册 §3（p.38）明确器件"always responds to
   PHY address 0"，另有可配置广播地址。用 strapping 得到的 1。

5. **BMSR.LINK 是锁存位**。只读一次可能读到上一次的状态；应用说明 §5.2（p.25）要求连读两次。
   本仓库 `mdio_read_stable()` 连读两次取与。

6. **EXT 0x0000 与手册描述不符（已定位，见 5.6 节与表 12-15）**。手册 §3（p.38）说"扩展寄存器地址 0x0 的 [4:0] 是广播地址、
   默认 0x1F、[5] 使能=1"，但本板实测 `ext 0x0000` 读回 0x0000。暂按"该地址不是文档所指广播寄存器"处理，
   不影响主流程（第 11 节登记为"已定位"）。

7. **MDC 分频写死在驱动里**。`enet_read_phy()/enet_write_phy()` 固定用 CSR/102
   （`hpm_enet_drv.c:395/419`），不支持按实际 CSR 频率自适应。若把 ENET 时钟改到 100 MHz 以下，
   MDC 会低于 1 MHz（仍可用，但读寄存器变慢）；改到 300 MHz 以上则 MDC 超手册 12.5 MHz 上限。

8. **PTP/1588 需要额外时钟**：只有调用 `board_init_enet_ptp_clock()` 才会配置
   `clock_ptp0`（PLL1/4 = 100 MHz）。本板的 PHY 侧 PTP 功能没有接 1PPS，时间戳精度列为已知限制（表 12-15）。

## 12.10 优劣评价与同类对比

表 12-13 同类 PHY 对比

| 项目 | YT8011AN（本板） | RTL8211F | DP83867 | LAN8651（10BASE-T1S） |
|---|---|---|---|---|
| 线侧标准 | 1000BASE-T1/100BASE-T1（单对） | 1000BASE-T（四对） | 1000BASE-T（四对） | 10BASE-T1S（单对，多点） |
| MAC 接口 | RGMII | RGMII | RGMII | SPI（Open Alliance TC6） |
| 最大速率 | 1000 Mbit/s | 1000 Mbit/s | 1000 Mbit/s | 10 Mbit/s |
| 线束要求 | 1 对差分，≤15 m（车载） | 4 对，≤100 m | 4 对，≤100 m | 1 对，≤25 m，可多点 |
| 主机侧对接 | 需 1000BASE-T1↔RJ45 转换盒 | 普通 RJ45 直连 | 普通 RJ45 直连 | 需 10BASE-T1S↔RJ45 转换器 |
| 配置复杂度 | 中（EXT/MMD 两层间接寄存器） | 中（page 切换） | 高（寄存器多） | 高（SPI 协议 + PLCA 状态机） |
| 本板用途 | 千兆车载以太网（本章/第 13 章） | 参考对比 | SDK 支持最全 | 10M 车载（第 10/11 章） |

评价：

- 优点：单对线束、支持 1000M/100M 两档、RGMII 接口与 MCU 直连、寄存器沿用 Clause 22 习惯用法；
- 缺点：**关键功能藏在 EXT/MMD 两层间接寄存器里**，文档对"软件复位与配置生效顺序"描述不完整
  （坑 3 是实测才发现）；PHY 侧统计计数器不能通过 Clause 22 直接读取，调试手段有限。
- 与 STM32/TC397 平台对比：同类车载 PHY 在这些平台上也是 RGMII + MDIO，寄存器配置流程基本一致，
  差异主要在 MCU 侧 ENET 的 delay 与 DMA 描述符实现（本板由 HPM SDK 封装）。

## 12.11 参考来源

1. Motorcomm（裕太微）官网：<https://www.motor-comm.com/>——YT8011A/AN/AR 数据手册与应用说明（完整版需 NDA，核对日期 2026-11-02）；本书引用页码以本地 V1.4 2025-12-11 版为准。
2. YT8011A_1.pdf（Motorcomm YT8011A/AN/AR Data Sheet）V1.4 2025-12-11：
   §12.4.1 100BASE-T1（p.25）、§2 Management Interface（p.26）、§3 Operational Description（p.38）、
   §12.6.1 PHY MII Register（p.39-42）、§4.2 PHY EXT Register（p.42-57）、§4.3 MMD（p.52-57）、
   §12.6.4 Common EXT Register（p.58）、§5.2.4 RGMII Timing Delay Modes（p.87-89）
2. YT8011Ax_应用说明_V2.2.pdf（2025-12-11）：§12.4.1 EXT 访问（p.10）、§2.1.3 MMD 访问（p.11）、
   §12.7.2 Link 状态获取（p.25）、§5.3 Master/Slave 配置（p.26）、§5.4 速率配置（p.27）
3. `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_enet_soc_drv.h:16`（接口选择）、`:56`（RGMII 延迟）
4. `hpm_sdk/drivers/src/hpm_enet_drv.c:395`（`enet_write_phy`）、`:419`（`enet_read_phy`）、`:459`（`enet_controller_init`）
5. `hpm_sdk/soc/HPM5E00/HPM5E31/hpm_soc_feature.h:110`（`ENET_SOC_RGMII_EN` 死宏）
6. `boards/hpm5e31_1/board.c:570/582/747`、`boards/hpm5e31_1/pinmux.c:374/870`
7. 实测日志：`logs/20261030-110004-enet_mdio_scan.log`、`logs/20261030-110554-enet_phy_config.log`、
   `logs/20261030-110711-enet_phy_config.log`、`logs/20261030-110912-enet_phy_config.log`
8. HPM5E00 数据手册 DS Rev0.4 p.1、p.65/68（片内 1 MB Flash，型号 GT25Q80AH）

## 12.12 扩展阅读

- [HPM SDK 官方仓库](https://github.com/hpmicro/hpm_sdk)（v1.13.0 的 `components/enet_phy/` 有 RTL8211/DP83867 等驱动可对照）
- [Motorcomm（裕太微）官网](https://www.motor-comm.com/)（YT8011A/AN 数据手册与应用说明下载）
- [Open Alliance 1000BASE-T1 规范](https://opensig.org/)（车载以太网物理层标准）
- IEEE 802.3 Clause 22/45（MDIO 管理帧与 MMD 访问语义）
- 本仓库第 13 章（lwIP/iperf）、第 10/11 章（LAN8651 10BASE-T1S 对比）

## 12.13 待确认项与风险

表 12-15 待确认项与风险（**已逐条复核，2026-11-02**）

| 项 | 状态 | 验证方法 |
|---|---|---|
| EXT 0x0000 广播地址寄存器与手册描述不符 | **已定位（2026-11-02）**：手册 §3 说该地址 [4:0] 是广播地址、默认 0x1F 且 [5] 使能，但本板读回 0x0000；MDIO 扫描只有 0/1 应答、0x1F 无响应，说明该寄存器**没有**改变地址映射 | `bcast` 命令：写 0x003F → 回读 → 重扫 0..31；判据是回读值与 0x1F 应答是否变化 |
| 100BASE-T1 实际吞吐与误码 | **已知限制（需人工拨码）**：转换盒拨码固定在 1000M Master，软件切 100M 必掉链（表 12-12 第 2 行），无人值守无法建链测试 | 现场拨码 100M Master + 板端 `mode 100 slave` 后，按第 13 章命令跑 iperf TCP/UDP，记录 PHYSR/GBSR 与重传 |
| Board 做 Master 的吞吐/稳定性 | **已知限制（需人工拨码）**：需要转换盒拨成 Slave 才能与本板 Master 配对 | 现场拨码 1000M Slave + 板端 `mode 1000 master`，复测 iperf 与 30 min ping |
| ENET 侧 RGMII delay 最优点 | **已知限制（本轮无独占窗口）**：当前 PHY 侧 RXC 2 ns + ENET 0 档无错包（第 13 章 iperf 数据），8 档 × 60 s 扫描需要连续窗口 | 固定 PHY 侧 2 ns，`board_note` 用 `rgmii` 改 ENET RX delay 0~7，每档跑 10 s iperf 记重传/CRC 错；判据是最低 delay 且 0 重传的那一档 |
| PHY 中断（PB28 nINT）驱动 | **已实测（实现并自证，2026-11-02）**：GPIO 下降沿中断 + 主循环读 MII 0x13/0x17 + 链路事件日志（4.4 节、图 12-4）；上板自证 IRQn=2 使能、nINT 空闲=1、INTMSK/INTST/INTST2=0x0000。链路变化 ISR 计数未采到（post 串口回读为空，见 5.5 节） | 现场 `irq on` → `mode 1000 master` → `irq` → `mode 1000 slave` → `irq`，看 `isr=` 递增 |
| PTP/1588 时间戳精度 | **已知限制（无 1PPS 输出条件）**：需要接 PF18 EVTO 的 1PPS 到示波器，或与主机 PTP 主时钟比对；本轮板卡被其他批次占用 | 接 1PPS 后用示波器比对 offset/抖动；或跑第 38 章 PTP 流程读 `ptpstats` 的 offset |
| MDC 频率与 ENET CSR 时钟的关系 | **已知限制（无示波器）**：驱动把 MDC 固定为 CSR/102（`hpm_enet_drv.c:395/419`），本轮不改 `clock_eth0` 即可正常读写 PHY（表 12-10/12-11） | 改 `clock_eth0` 分频后用探头测 PA31 MDC，验证 ≈CSR/102 且 ≤12.5 MHz |
