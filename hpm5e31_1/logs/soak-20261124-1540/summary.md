# 千兆车载以太网耐久测试报告

- 日志目录：`logs/soak-20261124-1540`
- 开始：2026-11-24T15:37:52.636000+08:00
- 结束：2026-11-25T06:46:34.703000+08:00
- 有效时长：**15.14 h**（采样 1818 条，阶段 145 条）
- 被测链路：板 192.168.2.100 (HPM5E31 + YT8011AN 1000BASE-T1) ⇄ 转换盒 ⇄ 主机 enp6s0 192.168.2.1

## 1 判定：**FAIL**

不通过/需关注项：

- 板子复位 2 次（banner 2 / uptime 回退 1；合并 10 min 内重复复位 = 1 次独立故障）

| 判据 | 结果 |
|---|---|
| ping（非 UDP 压测段）/ 丢包 | 210245 / 210203 / **0.020%** |
| ping（UDP 900M 压测段）/ 丢包 | 51945 / 1833 / 96.47%（容量性，不计入判定） |
| RTT min/avg/p99/max（全部） | 0.074 / 0.283 / 0.640 / 153.000 ms |
| 板子复位次数（banner 2 / uptime 回退 1） | **2**（合并 10 min 内重复复位 = 1 次独立故障） |
| 失败阶段数 | 0 / 145 |

## 2 流量汇总

| 阶段 | 次数 | 成功 | 平均速率 | 最小/最大速率 | 累计字节 | 备注 |
|---|---|---|---|---|---|---|
| tcp_h2b | 37 | 37 | 242.8 Mbit/s | 228.5 / 249.0 | 658.86 GB |  |
| udp_h2b | 36 | 36 | 943.7 Mbit/s | 943.7 / 943.7 | 1274.02 GB |  |
| b2h_tcp | 36 | 36 | 271.5 Mbit/s | 261.2 / 282.1 | 244.33 GB | burst 720/720 成功 |
| idle | 36 | 36 | - | - | 0.00 GB |  |

## 3 每小时趋势

| 小时 | TCP 主机->板 | UDP 主机->板 | 板->主机 burst | 板->主机平均 | ping 丢包(非UDP段) | ping RTT avg/p99 |
|---|---|---|---|---|---|---|
| H00 | 249.0 | 943.7 | 40 | 263.2 | 0.014% | 0.282 / 0.459 ms |
| H01 | 249.0 | 943.7 | 60 | 273.5 | 0.015% | 0.295 / 0.556 ms |
| H02 | 238.9 | 943.7 | 40 | 267.4 | 0.021% | 0.262 / 0.530 ms |
| H03 | 242.2 | 943.7 | 40 | 272.6 | 0.014% | 0.274 / 0.527 ms |
| H04 | 249.0 | 943.7 | 60 | 273.3 | 0.023% | 0.297 / 0.563 ms |
| H05 | 241.0 | 943.7 | 40 | 272.2 | 0.090% | 0.287 / 0.595 ms |
| H06 | 238.8 | 943.7 | 60 | 272.5 | 0.023% | 0.272 / 0.601 ms |
| H07 | 238.8 | 943.7 | 40 | 277.7 | 0.014% | 0.274 / 0.461 ms |
| H08 | 249.0 | 943.7 | 60 | 276.4 | 0.007% | 0.267 / 0.442 ms |
| H09 | 249.0 | 943.7 | 40 | 268.7 | 0.007% | 0.284 / 0.545 ms |
| H10 | 235.4 | 943.7 | 40 | 269.9 | 0.007% | 0.272 / 0.544 ms |
| H11 | 238.8 | 943.7 | 60 | 267.0 | 0.015% | 0.270 / 0.598 ms |
| H12 | 249.0 | 943.7 | 40 | 271.0 | 0.014% | 0.315 / 0.587 ms |
| H13 | 235.4 | 943.7 | 60 | 270.3 | 0.014% | 0.256 / 0.500 ms |
| H14 | 238.9 | 943.7 | 40 | 274.3 | 0.022% | 0.267 / 0.546 ms |
| H15 | 249.0 | - | 0 | - | 0.000% | 0.311 / 0.412 ms |

## 4 物理层误码（板 -> 主机方向，主机网卡 CRC）

- host rx_crc_errors：1366308 → 1366308，增量 **0**
- 板端发送帧数：37278149 → 249381042，增量 212102893
- 折算误码帧比例：**0.0 ppm（0.0000%）**（CRC 错帧 / 板端发送帧）

> 说明：R9 及以前的 1.5% 误码根因是 YT8011AN **RGMII TXC 延迟档位 1**，R10 起固定在 4（EXT 0x9001=0x0140），
> 本段用于长期复核，期望增量为 0；若换盒/换线后出现非零，按第 12 章 §12.7.5 重扫档位。

| 阶段 | CRC 错帧增量 | 板端发帧增量 | 板端收帧增量 | ppm |
|---|---|---|---|---|
| tcp_h2b | 0 | 214672654 | 429227943 | 0.0 |
| b2h_tcp | 0 | 152149361 | 52602561 | 0.0 |
| udp_h2b | 0 | 1899 | 226220641 | 0.0 |
| idle | 0 | 46932 | 59882 | 0.0 |

> 归属说明：只统计前后两条采样同阶段的区间，跨阶段区间丢弃（避免把上一阶段流量算进来）。
> R9 按旧算法得到的 udp_h2b 行（9.2 M 发帧 / 91 ppm）是归属错误，实际该段板端几乎不发帧；
> R10 已把板→主机 CRC 误码的根因定位为 PHY RGMII TXC 延迟档位并修复，见交接 R10。

## 5 错误计数（首末采样差值）

| 计数 | 起始 | 结束 | 增量 |
|---|---|---|---|
| 板端收帧数 | 42883677 | 481405246 | **438521569** |
| 板端收字节 | 1312581302 | 550634468 | **3533020462** |
| 板端发帧数 | 37278149 | 249381042 | **212102893** |
| 板端发字节 | 636061218 | 2184605786 | **1548544568** |
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

- 板端 uptime：3919 s → 34116 s（差 30197 s）
- 主机链路速率采样：1000 → 1000 Mbit/s

## 6 事件时间线（异常/复位/链路）

```
2026-11-24T22:28:32.298+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T22:28:33.201+08:00 arm_server       udp -> already-listening
2026-11-24T22:33:33.315+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T22:38:34.146+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T22:38:35.105+08:00 arm_server       tcp -> already-listening
2026-11-24T22:48:35.234+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T22:53:41.569+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T22:53:42.380+08:00 arm_server       udp -> already-listening
2026-11-24T22:58:42.409+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T23:03:43.636+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T23:03:44.571+08:00 arm_server       tcp -> already-listening
2026-11-24T23:13:44.763+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T23:18:51.709+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T23:18:52.624+08:00 arm_server       udp -> already-listening
2026-11-24T23:23:52.772+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T23:28:53.583+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T23:28:54.514+08:00 arm_server       tcp -> already-listening
2026-11-24T23:38:54.707+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T23:44:01.838+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T23:44:02.678+08:00 arm_server       udp -> already-listening
2026-11-24T23:49:02.899+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T23:54:03.725+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-24T23:54:04.575+08:00 arm_server       tcp -> already-listening
2026-11-25T00:04:04.630+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T00:09:11.760+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T00:09:12.602+08:00 arm_server       udp -> already-listening
2026-11-25T00:14:12.811+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T00:19:13.633+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T00:19:14.518+08:00 arm_server       tcp -> already-listening
2026-11-25T00:29:14.698+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T00:34:20.630+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T00:34:21.514+08:00 arm_server       udp -> already-listening
2026-11-25T00:39:21.692+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T00:44:22.518+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T00:44:23.434+08:00 arm_server       tcp -> already-listening
2026-11-25T00:54:23.614+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T00:59:30.358+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T00:59:31.318+08:00 arm_server       udp -> already-listening
2026-11-25T01:04:31.448+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T01:09:32.280+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T01:09:33.197+08:00 arm_server       tcp -> already-listening
2026-11-25T01:19:33.413+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T01:24:40.163+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T01:24:41.101+08:00 arm_server       udp -> already-listening
2026-11-25T01:29:41.231+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T01:34:42.062+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T01:34:42.996+08:00 arm_server       tcp -> already-listening
2026-11-25T01:44:43.202+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T01:49:49.554+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T01:49:50.473+08:00 arm_server       udp -> already-listening
2026-11-25T01:54:50.610+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T01:59:51.431+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T01:59:52.361+08:00 arm_server       tcp -> already-listening
2026-11-25T02:09:52.565+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T02:14:58.710+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T02:14:59.530+08:00 arm_server       udp -> already-listening
2026-11-25T02:19:59.557+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T02:25:00.789+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T02:25:01.718+08:00 arm_server       tcp -> already-listening
2026-11-25T02:35:01.914+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T02:40:08.064+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T02:40:09.023+08:00 arm_server       udp -> already-listening
2026-11-25T02:45:09.106+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T02:50:10.139+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T02:50:11.062+08:00 arm_server       tcp -> already-listening
2026-11-25T03:00:11.229+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T03:05:17.365+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T03:05:18.239+08:00 arm_server       udp -> already-listening
2026-11-25T03:10:18.421+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T03:15:19.237+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T03:15:20.134+08:00 arm_server       tcp -> already-listening
2026-11-25T03:25:20.341+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T03:30:25.677+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T03:30:26.555+08:00 arm_server       udp -> already-listening
2026-11-25T03:35:26.734+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T03:40:27.540+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T03:40:28.452+08:00 arm_server       tcp -> already-listening
2026-11-25T03:50:28.673+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T03:55:35.812+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T03:55:36.690+08:00 arm_server       udp -> already-listening
2026-11-25T04:00:36.867+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T04:05:37.685+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T04:05:38.588+08:00 arm_server       tcp -> already-listening
2026-11-25T04:15:38.816+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T04:20:45.339+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T04:20:46.219+08:00 arm_server       udp -> already-listening
2026-11-25T04:25:46.393+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T04:30:47.213+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T04:30:48.117+08:00 arm_server       tcp -> already-listening
2026-11-25T04:40:48.324+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T04:45:54.664+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T04:45:55.474+08:00 arm_server       udp -> already-listening
2026-11-25T04:50:55.686+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T04:55:56.503+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T04:55:57.370+08:00 arm_server       tcp -> already-listening
2026-11-25T05:05:57.601+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T05:11:04.532+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T05:11:05.462+08:00 arm_server       udp -> already-listening
2026-11-25T05:16:05.580+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T05:21:06.594+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T05:21:07.517+08:00 arm_server       tcp -> already-listening
2026-11-25T05:31:07.719+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T05:36:13.659+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T05:36:14.549+08:00 arm_server       udp -> already-listening
2026-11-25T05:41:14.687+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T05:46:15.513+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T05:46:16.439+08:00 arm_server       tcp -> already-listening
2026-11-25T05:56:16.635+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T06:01:22.571+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T06:01:23.491+08:00 arm_server       udp -> already-listening
2026-11-25T06:06:23.610+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T06:11:24.624+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T06:11:25.545+08:00 arm_server       tcp -> already-listening
2026-11-25T06:21:25.748+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T06:26:31.894+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T06:26:32.779+08:00 arm_server       udp -> already-listening
2026-11-25T06:31:32.962+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T06:36:33.777+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
2026-11-25T06:36:34.667+08:00 arm_server       tcp -> already-listening
2026-11-25T06:46:34.764+08:00 phy              [lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
```

## 长稳退化对比（前 1.5 h vs 后 1.5 h）

| 阶段 | 前段平均 | 后段平均 | 变化 |
|---|---|---|---|
| tcp_h2b | 249.0 | 238.9 | -4.1% |
| udp_h2b | 943.7 | 943.7 | -0.0% |
| b2h_tcp | 268.9 | 274.0 | +1.9% |

## 附：复跑方法

```bash
cd .
python3 host/soak/soak_runner.py --duration-h 15 --tag <tag>
python3 host/soak/soak_report.py logs/soak-<tag>   # 随时重出报告
```

