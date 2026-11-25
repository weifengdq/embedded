# R10：板→主机 CRC 误码定向测量与修复（2026-11-24）

结论：**根因是 YT8011AN 的 RGMII TXC 延迟档位 1（EXT 0x9001[7:4]）采样余量不足**。
档位 1 时板→主机约 1% 的帧 FCS 错；档位 2~15 合计约 478 万帧零误码；档位 0 直接掉链。
板级配置已固定 `TxDelaySel = 4`，修复后 ACK/数据/回包全部 0 错。

## 1 测量方法

- 固件：`eth phy`（读 EXT 0x9001/BMCR/BMSR/PHYSR）、`eth txdly <0..15>`（运行时改档并回读）。
- 主机：`host/soak/crc_probe.py ack --secs N --txdly K`——板作 iperf2 server，主机打 TCP，
  板只回 60 B ACK（约 10.5 k 帧/s），同时取主机 `/sys/class/net/enp6s0/statistics/rx_crc_errors`
  与板端 `hw_tx_frames`，折算 ppm。
- 环境：板 192.168.2.100 ⇄ 转换盒 ⇄ 主机 enp6s0 192.168.2.1（1000M/Full，板 PHY SLAVE）。

## 2 档位扫描（ACK 段，60 B 帧）

| 档位 | 时长 | 板端发帧 | 主机 CRC 错帧 | ppm |
|---|---|---|---|---|
| 0 | 25 s | 16 | 7 | 链路不可用 |
| 1（R8/R9 默认） | 12.6 s / 20.6 s | 127 663 / 122 349 | 1 509 / 1 118 | 11 820 / 9 138 |
| 2 | 12+30+60 s | 1 043 461 | 0 | 0 |
| 3~7 | 各 12~30 s | ≈1 913 000 | 0 | 0 |
| 8~15 | 各 20 s | ≈1 702 000 | 0 | 0 |

原始 JSON：本目录 `txdly_*.json`、`w_*.json`、`tx0.json`、`tx2_60.json`。

## 3 修复后复核

| 场景 | 时长 | 板端发帧 | 主机 CRC 错帧 |
|---|---|---|---|
| ACK（主机→板 TCP，60 s） | 60.6 s | 638 041 | 0 |
| ACK（40 s） | 40.6 s | 425 313 | 0 |
| 板→主机数据（1518 B） | 40.6 s | 843 311（253 Mbit/s） | 0 |

原始 JSON：`tx2_60.json`、`v_ack.json`、`v_data.json`。

## 4 复跑

```bash
python3 host/soak/crc_probe.py ack  --secs 30 --txdly 1   # 复现，约 1e4 ppm
python3 host/soak/crc_probe.py ack  --secs 30 --txdly 4   # 修复后，0
python3 host/soak/crc_probe.py data --secs 30             # 板->主机 1518 B，0
```

修复点：`mcal/EthTrcv/cfg/EthTrcv_PBcfg.c` 的 `TxDelaySel` 1→4；同时 `components/enet_phy_yt8011`
的默认配置改为 4。文档：第 12 章 §12.7.5、第 49 章 §49.9.16 / §49.12 #12。
