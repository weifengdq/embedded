# 千兆车载以太网耐久测试报告

- 日志目录：`logs/soak-val2-20261124`
- 开始：2026-11-24T14:35:03.611000+08:00
- 结束：2026-11-24T15:36:51.637000+08:00
- 有效时长：**1.03 h**（采样 124 条，阶段 34 条）
- 被测链路：板 192.168.2.100 (HPM5E31 + YT8011AN 1000BASE-T1) ⇄ 转换盒 ⇄ 主机 enp6s0 192.168.2.1

## 1 判定：**PASS**

全部判据满足：ping 丢包 <1%，板端/主机错误计数无增长，无阶段失败，板子未复位。

| 判据 | 结果 |
|---|---|
| ping（非 UDP 压测段）/ 丢包 | 15556 / 15548 / **0.051%** |
| ping（UDP 900M 压测段）/ 丢包 | 2310 / 87 / 96.23%（容量性，不计入判定） |
| RTT min/avg/p99/max（全部） | 0.081 / 0.273 / 0.588 / 13.400 ms |
| 板子复位次数（banner 0 / uptime 回退 0） | **0** |
| 失败阶段数 | 0 / 34 |

## 2 流量汇总

| 阶段 | 次数 | 成功 | 平均速率 | 最小/最大速率 | 累计字节 | 备注 |
|---|---|---|---|---|---|---|
| tcp_h2b | 9 | 9 | 244.5 Mbit/s | 228.6 / 249.0 | 33.01 GB |  |
| udp_h2b | 8 | 8 | 943.7 Mbit/s | 943.7 / 943.7 | 56.62 GB |  |
| b2h_tcp | 9 | 9 | 267.0 Mbit/s | 258.9 / 271.0 | 36.04 GB | burst 108/108 成功 |
| idle | 8 | 8 | - | - | 0.00 GB |  |

## 3 每小时趋势

| 小时 | TCP 主机->板 | UDP 主机->板 | 板->主机 burst | 板->主机平均 | ping 丢包(非UDP段) | ping RTT avg/p99 |
|---|---|---|---|---|---|---|
| H00 | 244.5 | 943.7 | 96 | 266.6 | 0.053% | 0.271 / 0.563 ms |
| H01 | - | - | 12 | 270.3 | 0.000% | 0.245 / 0.554 ms |

## 4 物理层误码（板 -> 主机方向，主机网卡 CRC）

- host rx_crc_errors：1366308 → 1366308，增量 **0**
- 板端发送帧数：1269286 → 36952231，增量 35682945
- 折算误码帧比例：**0.0 ppm（0.0000%）**（CRC 错帧 / 板端发送帧）

> 说明：R9 及以前的 1.5% 误码根因是 YT8011AN **RGMII TXC 延迟档位 1**，R10 起固定在 4（EXT 0x9001=0x0140），
> 本段用于长期复核，期望增量为 0；若换盒/换线后出现非零，按第 12 章 §12.7.5 重扫档位。

| 阶段 | CRC 错帧增量 | 板端发帧增量 | 板端收帧增量 | ppm |
|---|---|---|---|---|
| tcp_h2b | 0 | 8406710 | 16808628 | 0.0 |
| b2h_tcp | 0 | 20729413 | 6867746 | 0.0 |
| udp_h2b | 0 | 51 | 5469252 | 0.0 |
| idle | 0 | 1131 | 160703 | 0.0 |

> 归属说明：只统计前后两条采样同阶段的区间，跨阶段区间丢弃（避免把上一阶段流量算进来）。
> R9 按旧算法得到的 udp_h2b 行（9.2 M 发帧 / 91 ppm）是归属错误，实际该段板端几乎不发帧；
> R10 已把板→主机 CRC 误码的根因定位为 PHY RGMII TXC 延迟档位并修复，见交接 R10。

## 5 错误计数（首末采样差值）

| 计数 | 起始 | 结束 | 增量 |
|---|---|---|---|
| 板端收帧数 | 1040193 | 42810234 | **41770041** |
| 板端收字节 | 1299353282 | 1308088994 | **8735712** |
| 板端发帧数 | 1269286 | 36952231 | **35682945** |
| 板端发字节 | 1300656116 | 142867998 | **3137179178** |
| 板端 RX 错误 | 0 | 0 | **0** |
| 板端 TX 错误 | 0 | 0 | **0** |
| pbuf 分配失败 | 0 | 0 | **0** |
| TX 长度丢帧 | 0 | 0 | **0** |
| 协议栈输入错误 | 0 | 0 | **0** |
| TX 回收失败 | 0 | 0 | **0** |
| 主机 rx_errors | 1381080 | 1381080 | **0** |
| 主机 rx_dropped | 9437 | 9437 | **0** |
| 主机 tx_errors | 0 | 0 | **0** |
| 主机 rx_crc_errors | 1366308 | 1366308 | **0** |
| 主机 rx_missed_errors | 0 | 0 | **0** |
| 主机 rx_over_errors | 0 | 0 | **0** |

- 板端 uptime：150 s → 3433 s（差 3283 s）
- 主机链路速率采样：1000 → 1000 Mbit/s

## 6 事件时间线（异常/复位/链路）

```
2026-11-24T14:35:02.589+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T14:35:03.459+08:00 arm_server       tcp -> already-listening
2026-11-24T14:35:03.610+08:00 arm_server       udp -> started
2026-11-24T14:35:03.914+08:00 arm_server       tcp -> already-listening
2026-11-24T14:40:07.899+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T14:40:08.826+08:00 arm_server       udp -> already-listening
2026-11-24T14:42:09.022+08:00 arm_server       tcp -> already-listening
2026-11-24T14:47:13.206+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T14:47:14.147+08:00 arm_server       udp -> already-listening
2026-11-24T14:49:14.345+08:00 arm_server       tcp -> already-listening
2026-11-24T14:54:18.730+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T14:54:19.670+08:00 arm_server       udp -> already-listening
2026-11-24T14:56:19.862+08:00 arm_server       tcp -> already-listening
2026-11-24T15:01:24.268+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T15:01:25.109+08:00 arm_server       udp -> already-listening
2026-11-24T15:03:25.294+08:00 arm_server       tcp -> already-listening
2026-11-24T15:08:29.972+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T15:08:30.881+08:00 arm_server       udp -> already-listening
2026-11-24T15:10:31.084+08:00 arm_server       tcp -> already-listening
2026-11-24T15:15:35.294+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T15:15:36.133+08:00 arm_server       udp -> already-listening
2026-11-24T15:17:36.325+08:00 arm_server       tcp -> already-listening
2026-11-24T15:22:40.812+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T15:22:41.723+08:00 arm_server       udp -> already-listening
2026-11-24T15:24:41.919+08:00 arm_server       tcp -> already-listening
2026-11-24T15:29:46.129+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T15:29:47.066+08:00 arm_server       udp -> already-listening
2026-11-24T15:31:47.256+08:00 arm_server       tcp -> already-listening
2026-11-24T15:36:51.665+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
```

## 长稳退化对比（前 1.0 h vs 后 1.0 h）

| 阶段 | 前段平均 | 后段平均 | 变化 |
|---|---|---|---|
| tcp_h2b | 244.5 | 244.5 | +0.0% |
| udp_h2b | 943.7 | 943.7 | +0.0% |
| b2h_tcp | 266.6 | 267.0 | +0.2% |

## 附：复跑方法

```bash
cd .
python3 host/soak/soak_runner.py --duration-h 15 --tag <tag>
python3 host/soak/soak_report.py logs/soak-<tag>   # 随时重出报告
```

