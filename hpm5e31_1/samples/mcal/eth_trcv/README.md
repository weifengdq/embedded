# eth_trcv —— AUTOSAR EthTrcv（YT8011AN）链路 / 速率 / 主从切换实测

## 1. 这是什么

只用 EthTrcv 与 Eth 的 MDIO 服务，验证 YT8011AN 的链路状态、1000M/100M 速率切换、Master/Slave 切换、建链耗时与链路轮询。

## 2. 硬件与接线

| 项 | 说明 |
|---|---|
| 板卡 | hpm5e31_1，YT8011AN（MDIO 地址 1，strap Slave） |
| 对端 | 千兆车载以太网转换盒（Master） |
| 关键引脚 | MDC PA31 / MDIO PA30；nRST PB29；RGMII PF02~PF13 |
| 依赖 | mcal/Eth（提供 Eth_ReadMii/WriteMii/ReadMmd/WriteMmd） |

## 3. 编译与运行

```bash
bash scripts/build.sh samples/mcal/eth_trcv
```

```bash
bash scripts/hwrun.sh mcal/eth_trcv --secs 40 --send "trcv info;trcv regs;trcv s100;trcv s1000;trcv master;trcv slave;trcv poll 3"
```

## 4. 命令表

| 命令 | 作用 |
|---|---|
| trcv info | 链路/速率/双工/主从/OUI/Model/Rev/SQI/PHYSR |
| trcv regs | Clause 22 0x00~0x0A/0x11 + EXT 0x9001 + MMD1 0x834 |
| trcv link [ms] | 等链路 up，打印耗时 |
| trcv s1000 / s100 | 切 1000BASE-T1 / 100BASE-T1（带速率编码的软复位） |
| trcv master / slave | 切主从 |
| trcv mode down/active | 收发器掉电/上电 |
| trcv poll <secs> | 跑 EthTrcv_MainFunction，记录链路翻转事件 |

## 5. 实测现象与日志

- 实测输出示例（本板）：`TRCV link=UP baud=1000M mac=RGMII oui=0x13D47A model=48 rev=1 sqi=0x0007`
- 日志：`logs/<时间戳>-eth_trcv.log`（hwrun 生成，正文第 49 章表 49-13/图 49-3 引用）
- 复跑：`bash scripts/hwrun.sh mcal/eth_trcv --secs 40 --send "trcv info;trcv s100;trcv s1000"`

## 6. 关键点与易错点

1. **1000BASE-T1 强制模式必须一端 Master 一端 Slave**；两端同角色建不起链（PHYSR[10] 恒 0）。
2. **软复位必须带速率编码**：写 BMCR=0x8000 会把速率/主从退回上电 strap 默认；1000M 用 0x8140、100M 用 0xA100。
3. **BMSR[2] 是锁存位**，要连读两次；本实现用 PHYSR[10] 实时位判断链路。
4. 速率切换后要等 BMCR[15] 自清 + ≥12 ms 稳定，再等链路重新建立（通常 200~800 ms）。
