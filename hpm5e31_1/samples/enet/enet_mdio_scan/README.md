# enet_mdio_scan —— ENET0 MDIO 总线扫描（YT8011AN 千兆 PHY）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 1. 功能

用 HPM5E31 的 ENET0 MDIO（Clause 22）控制器，扫描 MDIO 地址 0~31，读取每个地址的
PHY 标识（reg 0x02/0x03）、BMCR（0x00）、BMSR（0x01），并解码 OUI / 型号 / 版本；
同时提供单寄存器读写、PHY 状态解码、链路轮询、硬件复位等调试命令。

这是本板以太网（YT8011AN RGMII PHY）**第一层自证**：只要 MDIO 通，就说明
ENET0 时钟、PF/PA 引脚复用、PHY 供电与 nRST 时序都正确。

## 2. 硬件与接线

| 信号 | SoC 引脚 | 说明 |
|---|---|---|
| MDC | PA31 | ENET0_MDC，IOC 选 `IOC_PA31_FUNC_CTL_ETH0_MDC` |
| MDIO | PA30 | ENET0_MDIO，需外部上拉（板上有） |
| RGMII TXC/TXD0-3/TXEN | PF02/PF03/PF04/PF05/PF06/PF07 | 本样例仅做引脚复用，不做收发 |
| RGMII RXC/RXD0-3/RXDV | PF13/PF09/PF10/PF11/PF12/PF08 | 同上 |
| PHY nRST | PB29 | 低有效，软件复位：拉低 2ms → 释放 → 等 30ms |
| PHY nINT | PB28 | 低有效，本样例只配置为输入 |

- PHY：YT8011AN（Motorcomm 千兆车载以太网 PHY，线侧 1000BASE-T1）
- 板端 MDIO 地址：strapping `RXD[1:0]=01` → **PHYADDR = 1**
- 对端：千兆车载以太网转换盒（拨码 1000M Master）+ 主机千兆网卡

## 3. 编译/烧写/运行

```bash
cd hpm5e31_1
bash scripts/build.sh enet/enet_mdio_scan
bash scripts/hwrun.sh enet/enet_mdio_scan --secs 8 --send 'scan;phy 1;rd 1 0 16;link 1'
```

## 4. shell 命令

| 命令 | 说明 |
|---|---|
| `scan` | 扫描 MDIO 地址 0~31，打印 ID1/ID2/OUI/型号/版本/BMCR/BMSR |
| `rd <pa> <reg> [cnt]` | 读 PHY 寄存器（pa 0~31、reg 0~31，可连读 cnt 个） |
| `wr <pa> <reg> <val>` | 写 PHY 寄存器并回读校验 |
| `phy [pa]` | 解码 BMCR/BMSR/GBCR/GBSR/PHYSR |
| `rst` | 通过 PB29 硬复位 PHY，然后打印状态 |
| `link [pa]` | 轮询 BMSR.LINK（20 × 100ms）直到 link up |

## 5. 实测现象（2026-10-30，板 hpm5e31_1）

日志：`logs/20261030-110004-enet_mdio_scan.log`

```
addr  ID1     ID2     OUI     MODEL REV  BMCR   BMSR   LINK
----  ------  ------  ------  ----- ----  -----  -----  ----
   0  4F51    EB01    13D47A  48    1    0140   000D   UP
   1  4F51    EB01    13D47A  48    1    0140   000D   UP
[mdio] 2 device(s) found
```

关键寄存器实读值（PHYADDR=1）：

| 寄存器 | 值 | 解读 |
|---|---|---|
| 0x00 BMCR | 0x0140 | AN 关闭、1000M、全双工（上电 strap 默认：强制 1000BASE-T1 Slave） |
| 0x01 BMSR | 0x000D | link=1、AN 能力=1、扩展寄存器能力=1 |
| 0x02 PHYID1 | 0x4F51 | Motorcomm OUI 高位 |
| 0x03 PHYID2 | 0xEB01 | ID2[15:10]=0x3A（OUI 低位），型号 0x30=48，版本 1 |
| 0x09 GBCR | 0x0000 | 未手动配置主从（Slave） |
| 0x0A GBSR | 0x3000 | 主从已解析=Slave、local RX OK、remote RX OK |
| 0x11 PHYSR | 0x8400 | [15:14]=10 → 1000M，[13]=0 → Slave，[10]=1 → 实时 link up |

> 说明：物理地址 0 与 1 都应答是**正常现象**：YT8011A/AN 数据手册 §3（p.38）写明
> “YT8011A/AN/AR always responds to PHY address 0”，另外还有一个可配置的广播地址。

## 6. 关键点与坑

1. **MDIO 读不到任何器件时先查 nRST**：本板 PHY nRST=PB29，若上电后一直拉低则 MDIO 全部读回 0xFFFF。
   样例在扫描前显式做 2ms 低 + 30ms 高（数据手册 §3 p.38 要求 RESET_N 至少 10ms）。
2. **MDC 分频是固定的**：SDK `hpm_enet_drv.c` 的 `enet_read_phy()/enet_write_phy()` 把
   `GMII_ADDR.CR` 固定写成 `enet_csr_150m_to_250m_mdc_csr_div_102`（CSR 150~250MHz → MDC 约 1.5~2.4MHz），
   不随 `clock_eth0` 实际频率自适应；改以太网 CSR 时钟频率会改 MDC 频率。
3. **地址 0 应答不代表器件地址是 0**：见上，实际使用时用 strapping 得到的 1。
4. **BMSR.LINK 为锁存类型**（应用说明 §5.2 p.25），寄存器级读取需要连读两次；本样例的
   `mdio_read_stable()` 读两次取与。
5. MDIO 与 MAC 收发无关：只初始化时钟 + 引脚 + PHY 复位就能扫描，不需要建立 DMA 描述符。

## 7. 与例程配套的文件

| 文件 | 作用 |
|---|---|
| `src/main.c` | 本样例主程序与 shell 命令 |
| `boards/hpm5e31_1/ext/board_net2.h` | 板级宏（PHY 地址、寄存器位定义、复位时序） |
| `boards/hpm5e31_1/pins/pinmux_net2.c` | 引脚组合封装（调用 pinmux.c 中已有的 RGMII/MDIO 配置）+ PHY 复位 |
| `components/enet_phy_yt8011/` | YT8011AN 驱动（本样例未使用，见 enet_phy_config） |

## 8. 参考

- YT8011A_1.pdf V1.4：§3 Operational Description（p.38，复位/PHY 地址/strapping）、
  §4.3 PHY MII Register（p.39-42）
- YT8011Ax_应用说明_V2.2.pdf：§5.2 Link 状态获取（p.25）
- `hpm_sdk/drivers/src/hpm_enet_drv.c`：`enet_read_phy()` / `enet_write_phy()`
- 实测日志：`logs/20261030-110004-enet_mdio_scan.log`
