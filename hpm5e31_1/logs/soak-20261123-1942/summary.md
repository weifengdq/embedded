# 千兆车载以太网耐久测试报告

- 日志目录：`logs/soak-20261123-1942`
- 开始：2026-11-23T19:40:54.925000+08:00
- 结束：2026-11-24T10:44:11.765000+08:00
- 有效时长：**15.05 h**（采样 1807 条，阶段 142 条）
- 被测链路：板 192.168.2.100 (HPM5E31 + YT8011AN 1000BASE-T1) ⇄ 转换盒 ⇄ 主机 enp6s0 192.168.2.1

## 1 判定：**FAIL**

不通过/需关注项：

- 板子复位 3 次（banner 3 / uptime 回退 3）

| 判据 | 结果 |
|---|---|
| ping（非 UDP 压测段）/ 丢包 | 210079 / 209632 / **0.213%** |
| ping（UDP 900M 压测段）/ 丢包 | 50546 / 1761 / 96.52%（容量性，不计入判定） |
| RTT min/avg/p99/max（全部） | 0.073 / 0.288 / 0.595 / 489.000 ms |
| 板子复位次数（banner 3 / uptime 回退 3） | **3** |
| 失败阶段数 | 0 / 142 |

## 2 流量汇总

| 阶段 | 次数 | 成功 | 平均速率 | 最小/最大速率 | 累计字节 | 备注 |
|---|---|---|---|---|---|---|
| tcp_h2b | 36 | 36 | 249.0 Mbit/s | 227.6 / 251.7 | 672.30 GB |  |
| udp_h2b | 35 | 35 | 943.7 Mbit/s | 943.7 / 943.7 | 1238.63 GB |  |
| b2h_tcp | 36 | 36 | 271.6 Mbit/s | 252.2 / 294.1 | 244.56 GB | burst 720/720 成功 |
| idle | 35 | 35 | - | - | 0.00 GB |  |

## 3 每小时趋势

| 小时 | TCP 主机->板 | UDP 主机->板 | 板->主机 burst | 板->主机平均 | ping 丢包(非UDP段) | ping RTT avg/p99 |
|---|---|---|---|---|---|---|
| H00 | 251.6 | 943.7 | 40 | 266.5 | 0.173% | 0.295 / 0.593 ms |
| H01 | 243.6 | 943.7 | 60 | 275.9 | 0.144% | 0.284 / 0.546 ms |
| H02 | 251.6 | 943.7 | 40 | 272.9 | 0.120% | 0.285 / 0.536 ms |
| H03 | 251.7 | 943.7 | 40 | 266.8 | 0.159% | 0.288 / 0.539 ms |
| H04 | 251.7 | 943.7 | 60 | 267.9 | 0.378% | 0.267 / 0.397 ms |
| H05 | 251.6 | 943.7 | 40 | 261.5 | 0.367% | 0.279 / 0.539 ms |
| H06 | 243.6 | 943.7 | 40 | 263.4 | 0.368% | 0.255 / 0.409 ms |
| H07 | 251.6 | 943.7 | 60 | 269.2 | 0.239% | 0.254 / 0.399 ms |
| H08 | 251.7 | 943.7 | 40 | 273.2 | 0.319% | 0.293 / 0.524 ms |
| H09 | 251.7 | 943.7 | 60 | 279.9 | 0.036% | 0.278 / 0.502 ms |
| H10 | 251.7 | 943.7 | 40 | 280.7 | 0.060% | 0.296 / 0.559 ms |
| H11 | 243.7 | 943.7 | 40 | 277.3 | 0.159% | 0.274 / 0.453 ms |
| H12 | 251.7 | 943.7 | 60 | 264.9 | 0.161% | 0.265 / 0.425 ms |
| H13 | 239.7 | 943.7 | 40 | 268.8 | 0.278% | 0.274 / 0.588 ms |
| H14 | 251.7 | 943.7 | 40 | 281.1 | 0.243% | 0.319 / 0.558 ms |
| H15 | - | - | 20 | 279.4 | 0.000% | 0.221 / 0.404 ms |

## 4 物理层误码（板 -> 主机方向，主机网卡 CRC）

- host rx_crc_errors：40328 → 1045264，增量 **1004936**
- 板端发送帧数：45 → 65915340，增量 65915295
- 折算误码帧比例：**15245.9 ppm（1.5246%）**（CRC 错帧 / 板端发送帧）

> 说明：R9 及以前的 1.5% 误码根因是 YT8011AN **RGMII TXC 延迟档位 1**，R10 起固定在 4（EXT 0x9001=0x0140），
> 本段用于长期复核，期望增量为 0；若换盒/换线后出现非零，按第 12 章 §12.7.5 重扫档位。

| 阶段 | CRC 错帧增量 | 板端发帧增量 | 板端收帧增量 | ppm |
|---|---|---|---|---|
| tcp_h2b | 923383 | 218987503 | 437845156 | 4216.6 |
| b2h_tcp | 20537 | 151982626 | 48245318 | 135.1 |
| udp_h2b | 32 | 1850 | 220466126 | 17297.3 |
| idle | 96 | 45624 | 58213 | 2104.2 |

> 归属说明：只统计前后两条采样同阶段的区间，跨阶段区间丢弃（避免把上一阶段流量算进来）。
> R9 按旧算法得到的 udp_h2b 行（9.2 M 发帧 / 91 ppm）是归属错误，实际该段板端几乎不发帧；
> R10 已把板→主机 CRC 误码的根因定位为 PHY RGMII TXC 延迟档位并修复，见交接 R10。

## 5 错误计数（首末采样差值）

| 计数 | 起始 | 结束 | 增量 |
|---|---|---|---|
| 板端收帧数 | 96 | 119941943 | **119941847** |
| 板端收字节 | 42804 | 1303222540 | **1303179736** |
| 板端发帧数 | 45 | 65915340 | **65915295** |
| 板端发字节 | 4122 | 1064801474 | **1064797352** |
| 板端 RX 错误 | 0 | 0 | **0** |
| 板端 TX 错误 | 0 | 0 | **0** |
| pbuf 分配失败 | 0 | 0 | **0** |
| TX 长度丢帧 | 0 | 0 | **0** |
| 协议栈输入错误 | 0 | 0 | **0** |
| TX 回收失败 | 0 | 0 | **0** |
| 主机 rx_errors | 40950 | 1056764 | **1015814** |
| 主机 rx_dropped | 9437 | 9437 | **0** |
| 主机 tx_errors | 0 | 0 | **0** |
| 主机 rx_crc_errors | 40328 | 1045264 | **1004936** |
| 主机 rx_missed_errors | 0 | 0 | **0** |
| 主机 rx_over_errors | 0 | 0 | **0** |

- 板端 uptime：39 s → 8155 s（差 8116 s）
- 主机链路速率采样：1000 → 1000 Mbit/s

## 6 事件时间线（异常/复位/链路）

```
2026-11-24T03:26:16.311+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T03:31:22.244+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T03:31:22.901+08:00 arm_server       udp -> already-listening
2026-11-24T03:36:23.099+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T03:41:23.722+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T03:41:24.335+08:00 arm_server       tcp -> already-listening
2026-11-24T03:51:24.414+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T03:56:31.561+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T03:56:32.221+08:00 arm_server       udp -> already-listening
2026-11-24T04:01:32.427+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T04:06:33.028+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T04:06:33.660+08:00 arm_server       tcp -> already-listening
2026-11-24T04:08:53.617+08:00 phase_recover    tcp_h2b attempt=1 tcp_h2b: iperf listener lost and cannot restart
2026-11-24T04:08:53.617+08:00 recover_begin    tcp_h2b: iperf listener lost and cannot restart
2026-11-24T04:08:53.771+08:00 recover_shell    shell alive, istop + continue
2026-11-24T04:08:57.580+08:00 arm_server       tcp -> already-listening
2026-11-24T04:09:03.129+08:00 board_boot       === mcal_eth_lwip: AUTOSAR MCAL Eth + lwIP 2.1.2 (baremetal NO_SYS=1) ===
2026-11-24T04:09:04.333+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T04:19:05.701+08:00 phase_done       {"phase": "tcp_h2b", "dur_plan": 600, "cycle": 21, "recovered": true, "bytes": 18872664128, "secs": 600.0, "mbit": 251.632317, "ok": true, "dur_actual": 752.2, "ts": "2026-11-24T04:19:05.700+08:00"}
2026-11-24T04:19:05.820+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T04:19:06.357+08:00 reset_detected   uptime went backwards: 15096.7 -> 602.9
2026-11-24T04:24:12.332+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T04:24:13.044+08:00 arm_server       udp -> started
2026-11-24T04:29:13.174+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T04:34:13.959+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T04:34:14.627+08:00 arm_server       tcp -> already-listening
2026-11-24T04:44:14.859+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T04:49:20.991+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T04:49:21.713+08:00 arm_server       udp -> already-listening
2026-11-24T04:54:21.851+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T04:59:22.664+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T04:59:23.284+08:00 arm_server       tcp -> already-listening
2026-11-24T05:09:23.361+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T05:14:30.097+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T05:14:30.846+08:00 arm_server       udp -> already-listening
2026-11-24T05:19:30.954+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T05:24:31.554+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T05:24:32.272+08:00 arm_server       tcp -> already-listening
2026-11-24T05:34:32.446+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T05:39:38.580+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T05:39:39.250+08:00 arm_server       udp -> already-listening
2026-11-24T05:44:39.448+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T05:49:40.067+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T05:49:40.693+08:00 arm_server       tcp -> already-listening
2026-11-24T05:59:40.924+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T06:04:46.465+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T06:04:47.081+08:00 arm_server       udp -> already-listening
2026-11-24T06:09:47.133+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T06:14:47.757+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T06:14:48.371+08:00 arm_server       tcp -> already-listening
2026-11-24T06:24:48.468+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T06:29:55.209+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T06:29:55.958+08:00 arm_server       udp -> already-listening
2026-11-24T06:34:56.061+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T06:39:56.688+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T06:39:57.393+08:00 arm_server       tcp -> already-listening
2026-11-24T06:49:57.572+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T06:55:04.703+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T06:55:05.313+08:00 arm_server       udp -> already-listening
2026-11-24T07:00:05.537+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T07:05:06.153+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T07:05:06.896+08:00 arm_server       tcp -> already-listening
2026-11-24T07:15:07.052+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T07:20:13.783+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T07:20:14.432+08:00 arm_server       udp -> already-listening
2026-11-24T07:25:14.643+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T07:30:15.239+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T07:30:15.859+08:00 arm_server       tcp -> already-listening
2026-11-24T07:40:15.969+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T07:45:22.310+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T07:45:22.982+08:00 arm_server       udp -> already-listening
2026-11-24T07:50:23.122+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T07:55:23.936+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T07:55:24.573+08:00 arm_server       tcp -> already-listening
2026-11-24T08:05:24.640+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T08:10:30.982+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T08:10:31.710+08:00 arm_server       udp -> already-listening
2026-11-24T08:15:31.834+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T08:20:32.457+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T08:20:33.144+08:00 arm_server       tcp -> already-listening
2026-11-24T08:22:54.837+08:00 phase_recover    tcp_h2b attempt=1 tcp_h2b: iperf listener lost and cannot restart
2026-11-24T08:22:54.838+08:00 recover_begin    tcp_h2b: iperf listener lost and cannot restart
2026-11-24T08:22:54.992+08:00 recover_shell    shell alive, istop + continue
2026-11-24T08:22:58.801+08:00 arm_server       tcp -> already-listening
2026-11-24T08:23:09.785+08:00 board_boot       === mcal_eth_lwip: AUTOSAR MCAL Eth + lwIP 2.1.2 (baremetal NO_SYS=1) ===
2026-11-24T08:23:10.989+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T08:33:19.186+08:00 phase_done       {"phase": "tcp_h2b", "dur_plan": 600, "cycle": 31, "recovered": true, "bytes": 18877120576, "secs": 600.0, "mbit": 251.691838, "ok": true, "dur_actual": 766.2, "ts": "2026-11-24T08:33:19.186+08:00"}
2026-11-24T08:33:19.246+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T08:33:19.694+08:00 reset_detected   uptime went backwards: 15089.3 -> 609.8
2026-11-24T08:38:24.982+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T08:38:25.708+08:00 arm_server       udp -> started
2026-11-24T08:43:27.814+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T08:48:28.417+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T08:48:29.024+08:00 arm_server       tcp -> already-listening
2026-11-24T08:58:29.125+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T09:03:35.249+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T09:03:35.900+08:00 arm_server       udp -> already-listening
2026-11-24T09:08:36.707+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T09:13:37.316+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T09:13:38.016+08:00 arm_server       tcp -> already-listening
2026-11-24T09:23:38.218+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T09:28:44.361+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T09:28:45.030+08:00 arm_server       udp -> already-listening
2026-11-24T09:33:45.206+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T09:38:45.825+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T09:38:46.477+08:00 arm_server       tcp -> already-listening
2026-11-24T09:48:46.533+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T09:53:52.466+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T09:53:53.195+08:00 arm_server       udp -> already-listening
2026-11-24T09:58:53.331+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T10:03:54.150+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T10:03:54.768+08:00 arm_server       tcp -> already-listening
2026-11-24T10:13:55.838+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T10:19:02.768+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T10:19:03.374+08:00 arm_server       udp -> already-listening
2026-11-24T10:24:03.581+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T10:29:04.208+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T10:29:04.818+08:00 arm_server       tcp -> already-listening
2026-11-24T10:39:04.930+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T10:44:11.885+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
```

## 长稳退化对比（前 1.5 h vs 后 1.5 h）

| 阶段 | 前段平均 | 后段平均 | 变化 |
|---|---|---|---|
| tcp_h2b | 245.6 | 245.7 | +0.0% |
| udp_h2b | 943.7 | 943.7 | +0.0% |
| b2h_tcp | 270.1 | 279.0 | +3.3% |

## 附：复跑方法

```bash
cd .
python3 host/soak/soak_runner.py --duration-h 15 --tag <tag>
python3 host/soak/soak_report.py logs/soak-<tag>   # 随时重出报告
```

