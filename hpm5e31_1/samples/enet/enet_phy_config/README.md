# enet_phy_config —— YT8011AN 寄存器级配置与链路验证

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 1. 功能

通过 ENET0 MDIO 对 YT8011AN 做**寄存器级**配置与验证，覆盖三类寄存器空间：

1. **Clause 22 标准寄存器**（0x00~0x1F）：BMCR/BMSR/PHYID/GBCR/GBSR/PHYSR…
2. **EXT 寄存器**（0x0000~0x9xxx）：间接访问 —— 先写 MII 0x1E = 地址，再读写 MII 0x1F。
   重点是 **0x9001 RGMII Config1**（RXC 2ns 延迟、TXC 延迟档位）和 0x9030（RGMII/SGMII 选择）。
3. **MMD 寄存器**（clause 45）：间接访问 —— 0x0D=devad → 0x0E=reg → 0x0D=0x4000+devad → 0x0E=data。
   重点是 **MMD1 0x0834**（bit14 强制主从、bit[3:0] 1000BASE-T1/100BASE-T1 选择）与 MMD7 0x200（自协商）。

并用 `mode` 命令切换 1000BASE-T1/100BASE-T1、Master/Slave，观察 link 与 0x11 PHYSR 的变化。

## 2. 硬件

同 `enet_mdio_scan`：MDC=PA31、MDIO=PA30、nRST=PB29、nINT=PB28、PHYADDR=1；
线侧 1000BASE-T1 经千兆车载以太网转换盒接主机（转换盒拨码 1000M Master）。

## 3. 运行

```bash
bash scripts/build.sh enet/enet_phy_config
bash scripts/hwrun.sh enet/enet_phy_config --secs 30 --send 'dump;status;mode 100 slave;status;mode 1000 master;status;restore'
```

## 4. shell 命令

| 命令 | 说明 |
|---|---|
| `status` | 打印 link/速率/主从/local_rx_ok/remote_rx_ok 与 BMCR/BMSR/GBCR/GBSR/PHYSR |
| `dump` | 打印 MII + EXT + MMD 关键寄存器 |
| `mode <1000\|100> <master\|slave\|auto>` | 切换速率/主从/自协商，并轮询 link |
| `rgmii [rxen] [txsel] [txclk]` | 写 EXT 0x9001（RXC 2ns 延迟使能、TXC 延迟档位、TXC 反相） |
| `ext <addr> [val]` | 读写任意 EXT 寄存器 |
| `mmd <devad> <reg> [val]` | 读写任意 MMD 寄存器 |
| `rd <reg> [cnt>` / `wr <reg> <val>` | 读/写 Clause 22 寄存器 |
| `poll [secs]` | 每 200ms 打印一次链路状态 |
| `restore` | 恢复板上默认（1000BASE-T1 Slave + RXC 2ns 延迟 + tx_delay_sel=1） |

## 5. 实测现象（2026-10-30）

日志：`logs/20261030-110554-enet_phy_config.log`（初始寄存器 dump + 100M 测试）、
`logs/20261030-110711-enet_phy_config.log`（0xA100 切 100M）、
`logs/20261030-110912-enet_phy_config.log`（Master/Slave 切换）。

### 5.1 寄存器初始值（上电 + 硬复位后）

| 空间 | 寄存器 | 实读 | 说明/出处 |
|---|---|---|---|
| MII | 0x00 BMCR | 0x0140 | 强制 1000BASE-T1、全双工 |
| MII | 0x01 BMSR | 0x000D | link up |
| MII | 0x09 GBCR | 0x0000 | 未手动主从 |
| MII | 0x0A GBSR | 0x3000 | Slave 已解析 + local/remote RX OK |
| MII | 0x11 PHYSR | 0x8400 | 1000M / Slave / link up |
| EXT | 0x9001 RGMII_CFG1 | **0x0110** | bit8=1 RXC 2ns 延迟（strap 默认）、[7:4]=1 TXC 延迟档位（手册 §4.4.2 p.58 默认值一致） |
| EXT | 0x9030 COMMON_CTRL1 | 0x2000 | [0] mii_sel=0 → RGMII |
| EXT | 0x9081/0x9082/0x9083 | 0x000A/0x0004/0x0008 | Link Training Time / Local Receiver Time / Remote Receiver Time |
| MMD | MMD1 0x0834 | **0x8001** | force_config=0(Slave)、phy_type=1(1000BASE-T1) |
| MMD | MMD7 0x0200 | 0x0000 | 自协商关闭 |

这组数据同时证明 **EXT/MMD 间接访问序列正确**（读出的值符合手册默认值），是后续写操作的前提。

### 5.2 速率切换（1000BASE-T1 ↔ 100BASE-T1）

| 操作 | 写入 | 回读 BMCR | PHYSR | link | 结论 |
|---|---|---|---|---|---|
| 切 100M | 写 MII 0x00 = 0xA100 | 0x2100 | 0x4000 | **DOWN** | 速率切换成功（MMD1 0x834 同步变为 0x8000） |
| 切回 1000M | 写 MII 0x00 = 0x8140 | 0x0140 | 0x8400 | UP | 恢复成功 |

切换后 link down 是**预期现象**：对端转换盒拨码固定在 1000M Master，两端速率不一致无法建链。
100M 实测吞吐需要人工把转换盒拨码改成 100M 后才能做（见第 8 节）。

### 5.3 Master/Slave 切换

| 操作 | MMD1 0x0834 回读 | PHYSR | link | 结论 |
|---|---|---|---|---|
| 初始 | 0x8001 (Slave) | 0x8400（[13]=0 Slave） | UP | 与转换盒(1000M Master)匹配 |
| 强制 Master | 0xC001 | 0x2000（[13]=1 Master） | **DOWN** | 双 Master 冲突，主从配置生效的直接证据 |
| 恢复 Slave | 0x8001 | 0x8400 | UP | 恢复成功 |

## 6. 关键点与坑（本样例实测得到）

1. **写 MMD1 0x834 后会“回退”**：如果按手册 §5.3 写完 0x834 再执行`BMCR=0x8000`软复位，
   速率会回到上电默认 1000BASE-T1（实测 MMD1 0x834 又从 0x8000 变回 0x8001）。
   正确做法：软复位要与速率选择**合并成一次 BMCR 写** —— 1000BASE-T1 写 **0x8140**，
   100BASE-T1 写 **0xA100**（应用说明 §5.4 p.27），本驱动的 `yt8011_soft_reset_cfg()` 即如此实现。
2. **不要用寄存器切主从（除非确实需要）**：应用说明 §5.3 明确建议“有明确固定工作模式时用
   strapping pin 配置，不应再通过寄存器切换”。本板 RXD3/MS_SEL 下拉 = Slave，转换盒为 Master，
   正好匹配；强行切 Master 会双 Master 冲突（实测 link down）。
3. **PHYSR(0x11) 才是运行时状态的真值**：速率为 [15:14]（10=1000M、01=100M）、主从为 [13]、
   实时 link 为 [10]。BMCR 里的 bit13/bit6 在手册中标注为 RO，不能靠读 BMCR 判断当前速率。
4. **BMCR bit8 不是“双工”**：YT8011 手册把 [8:7] 标为保留且默认 0x2，读回 BMCR=0x0140 的 bit8=1
   不代表半/全双工配置；1000/100BASE-T1 恒为全双工。
5. **EXT 0x0000 与手册描述不一致**：手册 §3（p.38）称“扩展寄存器地址 0x0 的 [4:0] 是广播地址、
   默认 0x1F、[5] 使能=1”，但实测该 EXT 地址读回 0x0000。待确认（可能 0x0000 不是该广播寄存器，
   或需在特定 page 下访问）。验证方法：用 `ext 0x0000` 结合 MDIO 广播地址扫描（0x1F 是否应答应答）对比。
6. **RGMII 延迟来源要分清**：本板 RGMII 的 RXC 2ns 延迟由 **PHY 内部**提供（strap RXDLY_EN=1，
   EXT 0x9001[8]=1），ENET 侧 `BOARD_ENET_RGMII_RX_DLY/TX_DLY` 目前都为 0（board.h）。
   若出现丢包/CRC 错，第一步先把 `rgmii 1 1 0` 读回确认 PHY 侧延迟没被改掉，再调 ENET 侧 delay。

## 7. 驱动 API（components/enet_phy_yt8011）

| API | 作用 |
|---|---|
| `yt8011_check_id()` | 校验 ID1=0x4F51、ID2[15:10]=0x3A |
| `yt8011_reset()` | BMCR 软复位（不带速率编码，会把速率恢复默认） |
| `yt8011_soft_reset_cfg()` | **推荐**：按速率写 0x8140/0xA100 的软复位 |
| `yt8011_read_ext()/write_ext()` | EXT 寄存器间接访问 |
| `yt8011_read_mmd()/write_mmd()` | MMD 寄存器间接访问（0x0D/0x0E 序列） |
| `yt8011_set_mode()` | 写 MMD1 0x834 + 带速率编码软复位 |
| `yt8011_set_rgmii_delay()/get_rgmii_delay()` | EXT 0x9001 读写 |
| `yt8011_get_status()` | 解析 BMSR/GBCR/GBSR/PHYSR → link/速率/主从/local_rx_ok |
| `yt8011_wait_link_up()` | 轮询直到 link up 或超时 |

## 8. 待确认项

- **100BASE-T1 实测吞吐**：需把转换盒拨码从 1000M Master 改为 100M（人工操作），
  然后 `mode 100 slave`，再做 iperf 对比。当前只验证了寄存器配置与 link 行为。
- **Master 模式吞吐/稳定性**：需把转换盒拨码改为 1000M Slave 后，板端 `mode 1000 master` 复测。
- **EXT 0x0000 广播地址寄存器**：见第 6 节第 5 条。

## 9. 参考

- YT8011A_1.pdf V1.4 §4.1/§4.4/§4.5/§4.4（p.39-58），§3（p.38）
- YT8011Ax_应用说明_V2.2.pdf §2 寄存器访问（p.10-12）、§5.2 Link（p.25）、§5.3 主从（p.26）、§5.4 速率（p.27）
- 实测日志：`logs/20261030-110554-enet_phy_config.log`、`logs/20261030-110711-enet_phy_config.log`、`logs/20261030-110912-enet_phy_config.log`
