# lan8651_lwip_iperf —— LAN8651（10BASE-T1S，TC6/SPI2）+ lwIP + iperf2

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

把 `components/oa_tc6/` 的数据通道接到 lwIP 2.x（bare-metal、NO_SYS=1、轮询），
在 10BASE-T1S 混合段上跑 ping 与 iperf2。这是第 11 章 U-11-1 的落地例程。

## 功能

- TC6 数据通道：68 B chunk、TX credits、RX chunk footer 重组（复用 `oa_tc6.c`）；
- lwIP netif：`src/oa_tc6_netif.c`（copy 型 TX/RX，RX 侧剥掉 LAN8651 追加的 4 B FCS）；
- 静态 IP 192.168.1.100/24，网关 192.168.1.1（主机 USB 10BASE-T1S 适配器）；
- iperf2（lwiP `lwiperf`）：TCP/UDP 服务端与客户端；
- 链路：2 s 轮询 BMSR（读两次取与，因为 LINK 位是锁存的），驱动 `netif_set_link_up/down()`。

## 接线与对端

| 项 | 值 |
|---|---|
| 板端 SPI2 | SCLK PA20 / MISO PA22 / MOSI PA23 / CS PA21；nRST PA19 |
| 10BASE-T1S | TRX_P/TRX_N 接 USB 适配器 `enx001ec0d1c337`（MAC 00:1e:c0:d1:c3:37） |
| 主机 IP | 192.168.1.1/24（`ip addr add 192.168.1.1/24 dev enx001ec0d1c337`） |
| PLCA | 默认关闭（CSMA/CD），与 `t1s_host.sh csma` 对应 |

## 命令

| 命令 | 说明 |
|---|---|
| `ip` | 打印 MAC/IP/掩码/网关/netif 状态 |
| `link` | 打印 LAN8651 PHY（BMSR/PHYID）与 PLCA 状态 |
| `iserver [tcp\|udp]` | 板端做 iperf2 服务端（主机 `iperf -c 192.168.1.100`） |
| `iclient <ip> [tcp\|udp] [secs] [kbps]` | 板端做 iperf2 客户端（主机先 `iperf -s`） |
| `istop` | 停止当前 iperf 会话 |
| `stats` | TC6 与 netif 统计（含 rx_dropped、tx_fail） |
| `speed <hz>` | 改 SPI2 SCLK（默认 20 MHz） |

## 构建与运行

```bash
bash scripts/build.sh lan8651/lan8651_lwip_iperf
# 板端：抓串口并执行命令
bash scripts/hwrun.sh lan8651/lan8651_lwip_iperf --no-build --secs 30 --send "ip;link"
# 主机侧（另一个终端）
ping -c 5 -I enx001ec0d1c337 192.168.1.100
iperf -c 192.168.1.100 -t 10          # 下行（主机 -> 板）
iperf -s                              # 上行：配合板端 iclient 192.168.1.1 tcp 10
```

## 已知限制

- iperf2 协议（与 `lwiperf` 对应），不能用 iperf3；
- 10BASE-T1S 是 10 Mbit/s 半双工共享段，TCP 吞吐上界约 9 Mbit/s（见第 11 章实测）；
- RX 队列深度 2（`OA_TC6_RX_QUEUE_DEPTH`），突发小包会丢，iperf 下用默认 1470 B 报文；
- 本示例不启用 PLCA（需要主机适配器同时配 PLCA）；PLCA 下 1PPS/事件功能见第 10 章。
