# 千兆车载以太网耐久测试报告

- 日志目录：`logs/soak-val-20261124`
- 开始：2026-11-24T12:45:58.459000+08:00
- 结束：2026-11-24T13:33:53.066000+08:00
- 有效时长：**0.79 h**（采样 96 条，阶段 26 条）
- 被测链路：板 192.168.2.100 (HPM5E31 + YT8011AN 1000BASE-T1) ⇄ 转换盒 ⇄ 主机 enp6s0 192.168.2.1

## 1 判定：**PASS**

全部判据满足：ping 丢包 <1%，板端/主机错误计数无增长，无阶段失败，板子未复位。

| 判据 | 结果 |
|---|---|
| ping（非 UDP 压测段）/ 丢包 | 12114 / 12019 / **0.784%** |
| ping（UDP 900M 压测段）/ 丢包 | 1746 / 79 / 95.48%（容量性，不计入判定） |
| RTT min/avg/p99/max（全部） | 0.083 / 0.258 / 0.577 / 10.700 ms |
| 板子复位次数（banner 0 / uptime 回退 0） | **0** |
| 失败阶段数 | 0 / 26 |

## 2 流量汇总

| 阶段 | 次数 | 成功 | 平均速率 | 最小/最大速率 | 累计字节 | 备注 |
|---|---|---|---|---|---|---|
| tcp_h2b | 7 | 7 | 240.8 Mbit/s | 227.4 / 250.8 | 25.29 GB |  |
| udp_h2b | 6 | 6 | 943.7 Mbit/s | 943.7 / 943.7 | 42.47 GB |  |
| b2h_tcp | 7 | 7 | 187.0 Mbit/s | 156.8 / 208.8 | 19.29 GB | burst 82/82 成功 |
| idle | 6 | 6 | - | - | 0.00 GB |  |

## 3 每小时趋势

| 小时 | TCP 主机->板 | UDP 主机->板 | 板->主机 burst | 板->主机平均 | ping 丢包(非UDP段) | ping RTT avg/p99 |
|---|---|---|---|---|---|---|
| H00 | 240.8 | 943.7 | 82 | 187.0 | 0.786% | 0.255 / 0.543 ms |

## 4 物理层误码（板 -> 主机方向，主机网卡 CRC）

- host rx_crc_errors：1313878 → 1356674，增量 **42796**
- 板端发送帧数：6443386 → 28115269，增量 21671883
- 折算误码帧比例：**1974.7 ppm（0.1975%）**（CRC 错帧 / 板端发送帧）

> 说明：R8 已定位该现象在 MAC 之后（板端 TDES0.ES/UF 全 0），疑 RGMII TX 时序/转换盒，属**已知未闭环问题**，
> 不计入本次耐久判定；本次 15 h 连续数据即为该问题的长期量化证据。

| 阶段 | CRC 错帧增量 | 板端发帧增量 | 板端收帧增量 | ppm |
|---|---|---|---|---|
| tcp_h2b | 31320 | 7732456 | 15460705 | 4050.5 |
| b2h_tcp | 11007 | 13089700 | 6012715 | 840.9 |
| udp_h2b | 467 | 848370 | 6719823 | 550.5 |
| idle | 2 | 1357 | 1978531 | 1473.8 |

> 解读：TCP 主机→板阶段板端只发 ACK（小帧），ppm 最高；板→主机数据帧（1518 B）ppm 低约一个量级，
> 说明误码与**小帧/高帧率**强相关，指向 RGMII TX 时序或转换盒对短帧的处理，而不是负载本身。

## 5 错误计数（首末采样差值）

| 计数 | 起始 | 结束 | 增量 |
|---|---|---|---|
| 板端收帧数 | 12888108 | 43059882 | **30171774** |
| 板端收字节 | 2331516750 | 2910512176 | **578995426** |
| 板端发帧数 | 6443386 | 28115269 | **21671883** |
| 板端发字节 | 347944536 | 3316936198 | **2968991662** |
| 板端 RX 错误 | 0 | 0 | **0** |
| 板端 TX 错误 | 0 | 0 | **0** |
| pbuf 分配失败 | 0 | 0 | **0** |
| TX 长度丢帧 | 0 | 0 | **0** |
| 协议栈输入错误 | 0 | 0 | **0** |
| TX 回收失败 | 0 | 0 | **0** |
| 主机 rx_errors | 1327428 | 1371240 | **43812** |
| 主机 rx_dropped | 9437 | 9437 | **0** |
| 主机 tx_errors | 0 | 0 | **0** |
| 主机 rx_crc_errors | 1313878 | 1356674 | **42796** |
| 主机 rx_missed_errors | 0 | 0 | **0** |
| 主机 rx_over_errors | 0 | 0 | **0** |

- 板端 uptime：1042 s → 3492 s（差 2450 s）
- 主机链路速率采样：1000 → 1000 Mbit/s

## 6 事件时间线（异常/复位/链路）

```
2026-11-24T12:45:57.445+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T12:45:58.305+08:00 arm_server       tcp -> already-listening
2026-11-24T12:45:58.457+08:00 arm_server       udp -> started
2026-11-24T12:45:58.761+08:00 arm_server       tcp -> already-listening
2026-11-24T12:51:03.297+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T12:51:04.172+08:00 arm_server       udp -> already-listening
2026-11-24T12:53:04.367+08:00 arm_server       tcp -> already-listening
2026-11-24T12:57:09.703+08:00 b2h_timeout      no client report (no line); istop + restart host server
2026-11-24T12:58:13.202+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T12:58:14.129+08:00 arm_server       udp -> already-listening
2026-11-24T13:00:14.319+08:00 arm_server       tcp -> already-listening
2026-11-24T13:05:20.307+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T13:05:21.213+08:00 arm_server       udp -> already-listening
2026-11-24T13:07:21.405+08:00 arm_server       tcp -> already-listening
2026-11-24T13:12:26.209+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T13:12:27.140+08:00 arm_server       udp -> already-listening
2026-11-24T13:14:29.377+08:00 arm_server       tcp -> already-listening
2026-11-24T13:18:49.494+08:00 b2h_timeout      no client report (no line); istop + restart host server
2026-11-24T13:19:39.577+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T13:19:40.496+08:00 arm_server       udp -> already-listening
2026-11-24T13:21:40.694+08:00 arm_server       tcp -> already-listening
2026-11-24T13:26:47.299+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T13:26:48.239+08:00 arm_server       udp -> already-listening
2026-11-24T13:28:48.416+08:00 arm_server       tcp -> already-listening
2026-11-24T13:33:53.183+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
```

## 附：复跑方法

```bash
cd .
python3 host/soak/soak_runner.py --duration-h 15 --tag <tag>
python3 host/soak/soak_report.py logs/soak-<tag>   # 随时重出报告
```

