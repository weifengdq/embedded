# 千兆车载以太网耐久测试报告

- 日志目录：`logs/soak-val3-20261125`
- 开始：2026-11-25T09:55:47.321000+08:00
- 结束：2026-11-25T10:32:00.552000+08:00
- 有效时长：**0.60 h**（采样 73 条，阶段 47 条）
- 被测链路：板 192.168.2.100 (HPM5E31 + YT8011AN 1000BASE-T1) ⇄ 转换盒 ⇄ 主机 enp6s0 192.168.2.1

## 1 判定：**PASS**

全部判据满足：ping 丢包 <1%，板端/主机错误计数无增长，无阶段失败，板子未复位。

| 判据 | 结果 |
|---|---|
| ping（非 UDP 压测段）/ 丢包 | 8746 / 8737 / **0.103%** |
| ping（UDP 900M 压测段）/ 丢包 | 1737 / 73 / 95.80%（容量性，不计入判定） |
| RTT min/avg/p99/max（全部） | 0.082 / 0.269 / 0.553 / 5.570 ms |
| 板子复位次数（banner 0 / uptime 回退 0） | **0**（合并 10 min 内重复复位 = 0 次独立故障） |
| 失败阶段数 | 0 / 47 |

## 2 流量汇总

| 阶段 | 次数 | 成功 | 平均速率 | 最小/最大速率 | 累计字节 | 备注 |
|---|---|---|---|---|---|---|
| tcp_h2b | 12 | 12 | 248.9 Mbit/s | 248.9 / 248.9 | 22.40 GB |  |
| udp_h2b | 12 | 12 | 943.7 Mbit/s | 943.7 / 943.7 | 42.47 GB |  |
| b2h_tcp | 12 | 12 | 269.4 Mbit/s | 254.3 / 278.0 | 16.16 GB | burst 48/48 成功 |
| idle | 11 | 11 | - | - | 0.00 GB |  |

## 3 每小时趋势

| 小时 | TCP 主机->板 | UDP 主机->板 | 板->主机 burst | 板->主机平均 | ping 丢包(非UDP段) | ping RTT avg/p99 |
|---|---|---|---|---|---|---|
| H00 | 248.9 | 943.7 | 48 | 269.4 | 0.103% | 0.266 / 0.494 ms |

## 4 物理层误码（板 -> 主机方向，主机网卡 CRC）

- host rx_crc_errors：1366308 → 1366308，增量 **0**
- 板端发送帧数：106518 → 18859787，增量 18753269
- 折算误码帧比例：**0.0 ppm（0.0000%）**（CRC 错帧 / 板端发送帧）

> 说明：R9 及以前的 1.5% 误码根因是 YT8011AN **RGMII TXC 延迟档位 1**，R10 起固定在 4（EXT 0x9001=0x0140），
> 本段用于长期复核，期望增量为 0；若换盒/换线后出现非零，按第 12 章 §12.7.5 重扫档位。

| 阶段 | CRC 错帧增量 | 板端发帧增量 | 板端收帧增量 | ppm |
|---|---|---|---|---|
| tcp_h2b | 0 | 3820749 | 7639112 | 0.0 |
| b2h_tcp | 0 | 5898769 | 1999761 | 0.0 |

> 归属说明：只统计前后两条采样同阶段的区间，跨阶段区间丢弃（避免把上一阶段流量算进来）。
> R9 按旧算法得到的 udp_h2b 行（9.2 M 发帧 / 91 ppm）是归属错误，实际该段板端几乎不发帧；
> R10 已把板→主机 CRC 误码的根因定位为 PHY RGMII TXC 延迟档位并修复，见交接 R10。

## 5 错误计数（首末采样差值）

| 计数 | 起始 | 结束 | 增量 |
|---|---|---|---|
| 板端收帧数 | 213077 | 27273171 | **27060094** |
| 板端收字节 | 322468606 | 1436127414 | **1113658808** |
| 板端发帧数 | 106518 | 18859787 | **18753269** |
| 板端发字节 | 5753536 | 2700340 | **4291914100** |
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

- 板端 uptime：64 s → 2207 s（差 2143 s）
- 主机链路速率采样：1000 → 1000 Mbit/s

## 6 事件时间线（异常/复位/链路）

```
2026-11-25T09:55:46.275+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T09:55:47.169+08:00 arm_server       tcp -> already-listening
2026-11-25T09:55:47.320+08:00 arm_server       udp -> started
2026-11-25T09:55:47.624+08:00 arm_server       tcp -> already-listening
2026-11-25T09:57:50.489+08:00 arm_server       udp -> already-listening
2026-11-25T09:58:50.682+08:00 arm_server       tcp -> started
2026-11-25T10:00:53.575+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T10:00:54.510+08:00 arm_server       udp -> started
2026-11-25T10:01:54.829+08:00 arm_server       tcp -> started
2026-11-25T10:03:57.707+08:00 arm_server       udp -> started
2026-11-25T10:04:57.902+08:00 arm_server       tcp -> started
2026-11-25T10:05:58.083+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T10:07:01.751+08:00 arm_server       udp -> started
2026-11-25T10:08:01.948+08:00 arm_server       tcp -> started
2026-11-25T10:10:04.697+08:00 arm_server       udp -> started
2026-11-25T10:11:04.807+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T10:11:05.679+08:00 arm_server       tcp -> started
2026-11-25T10:13:08.432+08:00 arm_server       udp -> started
2026-11-25T10:14:08.612+08:00 arm_server       tcp -> started
2026-11-25T10:16:11.510+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T10:16:12.337+08:00 arm_server       udp -> started
2026-11-25T10:17:12.532+08:00 arm_server       tcp -> started
2026-11-25T10:19:15.453+08:00 arm_server       udp -> started
2026-11-25T10:20:15.625+08:00 arm_server       tcp -> started
2026-11-25T10:21:15.820+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T10:22:19.371+08:00 arm_server       udp -> started
2026-11-25T10:23:19.555+08:00 arm_server       tcp -> started
2026-11-25T10:25:22.500+08:00 arm_server       udp -> started
2026-11-25T10:26:22.549+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T10:26:23.492+08:00 arm_server       tcp -> started
2026-11-25T10:28:26.448+08:00 arm_server       udp -> started
2026-11-25T10:29:26.772+08:00 arm_server       tcp -> started
2026-11-25T10:31:29.673+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T10:31:30.532+08:00 arm_server       udp -> started
```

## 附：复跑方法

```bash
cd .
python3 host/soak/soak_runner.py --duration-h 15 --tag <tag>
python3 host/soak/soak_report.py logs/soak-<tag>   # 随时重出报告
```

