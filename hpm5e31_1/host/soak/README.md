# 千兆车载以太网耐久测试（host/soak）

被测链路：HPM5E31（ENET0 RGMII + YT8011AN 1000BASE-T1）→ 转换盒 → 主机 enp6s0。
网络：板 192.168.2.100/24，主机 192.168.2.1/24（enp6s0 上加了 secondary 地址，不改原 192.168.0.1/24）。
串口：/dev/ttyACM1 @ 921600（CH343；J-Link 是 /dev/ttyACM0），J-Link：HPM5E31xGNx。

## 1 一次 15 h 跑法

```bash
cd hpm5e31_1
nohup setsid bash host/soak/soak_loop.sh 20261123-1935 15 >/dev/null 2>&1 &
```

- `soak_loop.sh` 是守护：runner 异常退出后同一 tag 自动续跑（最多 30 次），正常跑完写 `DONE` 后退出。
- 日志：`logs/soak-<tag>/`（serial.log / ping.log / samples.csv / phases.jsonl / events.log / summary.md / status.json）。

## 2 每个周期（默认 25 min，36 个周期 = 15 h）

| 阶段 | 时长 | 内容 |
|---|---|---|
| tcp_h2b | 600 s | 主机 → 板 TCP（板作 iperf2 server，实测 ~250 Mbit/s） |
| b2h_tcp | 300 s | 板 → 主机 TCP，10 s 一发（lwiperf client 固定 10 s），间隔 5 s |
| udp_h2b | 300 s | 主机 → 板 UDP 900 Mbit/s（压 MAC/DMA/PHY 线速；之后接 300 s idle 让 ICMP 恢复） |
| idle | 300 s | 空载，链路保持 |

全程另有：ping 5 次/s；每 30 s 采样板端统计 + 主机网卡计数；每 5 min `eth`+`uptime`（PHY 链路/速率、uptime 防复位漏检）。

## 3 看门狗与恢复

- 串口静默 > 45 s（板子每 10 s 必打统计）或（非 UDP 段）30 s 内 ping 丢包 > 60% → 判为异常。
- 恢复 L1：串口 shell 还能回 prompt → `eth istop` 后继续。
- 恢复 L2：shell 无响应 → J-Link 复位（`r;g`，不重新烧写），等启动 banner；计一次复位。
- iperf 监听丢失（0 字节）→ 自动 `eth istop` + 重建监听重跑一次；仍失败则按 L2 复位。

注意：lwIP lwiperf 的 TCP 监听 pcb 在一次会话结束后不会关闭（`s_iperf_session` 被回调置 NULL，但 5001 仍在 listen），
所以第二次起 `eth iserver` 会回 `server start FAILED (已有会话在跑?)`，**这是正常的**，旧监听仍在服务；
脚本把它当 already-listening 处理。

## 4 判定标准

PASS 需同时满足：

1. 非 UDP 压测段（含 UDP 结束后 120 s 冷却）ping 丢包 < 1%；
2. 板端 hw_rx_err / hw_tx_err / sw_rx_pbuf_fail / tx_drop_len / rx_input_err / retry_fail 无增长；
3. 无阶段失败（traffic 阶段 0 字节算失败）；
4. 板子无复位（banner 次数 / uptime 回退）。

UDP 900 Mbit/s 压测段的 ICMP 丢包是**软件容量性**的（lwIP NO_SYS 单线程收包上限约 285 Mbit/s），单独统计、不计入判定。
主机 rx_crc_errors（板→主机方向）是 R8 已定位未闭环的**已知物理层问题**，单独量化，不计入判定。

## 5 早上怎么看

```bash
cd hpm5e31_1
bash host/soak/soak_status.sh <tag>          # 当前状态/最后事件
python3 host/soak/soak_report.py logs/soak-<tag>   # 重新生成 summary.md（随时可跑）
less logs/soak-<tag>/summary.md
```

## 6 中途停止

```bash
pkill -TERM -f soak_runner.py     # runner 收到 SIGTERM 会写 status/summary 后退出
pkill -f soak_loop.sh             # 守护也停掉（注意：pkill -f 的 pattern 不要写成会匹配到自己的形式）
```

## 7 已知注意事项

- 打流时不要往串口发大量命令：921600 波特率下 printf 是忙等，会顶住主循环（README 易错点 5）。
- 板端每 10 s 自动打印 6 行统计，单次约 6 ms，是观测窗口也是本底扰动。
- 主机需要 192.168.2.1/24：runner 的 preflight 会检查并用 `sudo -n ip addr add` 补上。
- 转换盒与板卡 1000BASE-T1 链路：`eth` 命令里看 `phy: link=UP ... speed=1000 Mbps duplex=full role=SLAVE`。

## 8 R9 首次 15 h 实测（2026-11-23 19:40 ~ 11-24 10:44）

`logs/soak-20261123-1942/summary.md`（已入库）。要点：

- 15.05 h / 142 个阶段全部 ok；TCP 主机→板 249.0 Mbit/s（672 GB）、板→主机 burst 271.6 Mbit/s（720/720，245 GB）、
  UDP 900M 943.7 Mbit/s（1239 GB）；ping（非 UDP 段）丢 0.213%；板端错误计数全 0；前后段吞吐无退化。
- **判定 FAIL 的唯一原因：板子复位 3 次**（每约 4 h 14 min 一次），根因是 lwIP TCP pcb 池被脱链会话耗尽
  （详见第 49 章 §49.9.17），已在 R9 修复。
- 复位由外部看门狗（连续 2 次失败事件 → J-Link 复位）兜住，每次只损失约 10~13 min 的一个阶段。
- 物理层：主机 `rx_crc_errors` 15 h 共 1 004 936，占板端发帧 1.52%；分阶段 TCP-ACK 段 4217 ppm、
  1518 B 数据帧 135 ppm（表 49-36，R10 修正归属口径）。**根因已在 R10 定位为 PHY RGMII TXC 延迟档位 1
  并修复（TxDelaySel=4）**，见第 49 章 §49.9.16 与第 12 章 §12.7.5。

## 9 R9 修复与回归

- 固件：`app_lwip_eth_iperf_report()` 任何终态都清 `s_iperf_session`；`MEMP_NUM_TCP_PCB` 10→24。
- 新增诊断命令：`lwipstat`（mem/memp 用量）、`tcppcb`（pcb 链表）。runner 的 housekeeping 每 5 min 自动发一次
  `lwipstat`，输出落在 `serial.log`，事后可核对池/堆。
- 回归：`logs/soak-val-20261124/`（48 min，压缩周期，tcp 120 s / b2h 180 s / udp 60 s / idle 60 s）。
- 判定口径更新：复位次数用 `max(banner 次数, uptime 回退次数)` 统计（run 开始时板子可能已经在跑）。

## 10 R10：CRC 误码定向测量工具（`crc_probe.py`）

15 h 耐久只能给出每 30 s 一个点的 ppm，分不清"误码随帧、随时间还是随帧长"。`crc_probe.py` 用受控流量
+ 秒级主机计数器 + 板端发帧计数做定向测量：

```bash
python3 host/soak/crc_probe.py ack  --secs 30               # 主机->板 TCP，板只回 60 B ACK（最敏感）
python3 host/soak/crc_probe.py ack  --secs 30 --txdly 4     # 先下发 eth txdly 4 再测
python3 host/soak/crc_probe.py data --secs 30               # 板->主机 TCP 1518 B
python3 host/soak/crc_probe.py udp  --secs 20               # 主机->板 UDP 900M（板端几乎不发帧）
python3 host/soak/crc_probe.py ping --secs 15               # ping flood
```

输出一行 JSON：`host_crc_delta`、`board_tx_frames`、`crc_ppm`、`crc_per_sec`、`per_sec`（每秒分布）。
关键用法：`--txdly N` 会先发 `eth txdly N`（需要 R10 及以后固件），因此可以在**不重烧**的情况下扫完
YT8011AN 的 0~15 档，直接比较 ppm。

R10 结论：档位 1 约 9.1~11.8 kppm，2~15 共约 478 万帧零误码，0 数据不可用；固件默认已改为 4。
另外 `soak_report.py` 的分阶段 CRC 归属已修正（只统计前后采样同阶段的区间），旧的 udp_h2b 行
（9.2 M 发帧 / 91 ppm）是跨阶段归属错误。
## 11 R10 15 h 重跑（2026-11-24/25）与 R10.1 残余 pcb 泄漏修复

- tag `20261124-1540`，15.14 h，145/145 阶段 ok；TCP 主机→板 242.8 Mbit/s、板→主机 271.5 Mbit/s、
  UDP 943.7 Mbit/s、ping 非 UDP 段 **0.020%**、720/720 burst；**主机 `rx_crc_errors` 增量 0
  （212 102 893 板端发帧 / 0 ppm）**、板端错误计数全 0——R10 的 PHY 修复在 15 h 尺度成立。
- 判定 **FAIL**：21:05:56（cycle 14）一次板子挂死（板端收帧掉到 6 fps、新 TCP 连接建不起来），
  runner 的 L2 与外部看门狗对同一事件各复位一次（banner 2 / 合并后 **1 次独立故障**）。
- 根因（R9 修复不完整）：报告回调把 **server 监听句柄**也一起清了，下个周期 `eth iserver` 重复
  `tcp_bind(5001)` 失败；该 SDK 版本的 lwiperf 在 bind 失败时**不释放**已分配的 state 与 tcp_pcb
  → 每 25 min 周期漏 **1 个 TCP_PCB + 192 B HEAP**。15 h 开始时已有 13 个孤儿（R10 调试打流遗留），
  到 cycle 14 池（24）耗尽。复位后 9.5 h 又漏到 22（离下次耗尽只差 2 个）。
- 修复（R10.1，应用层三句柄：`s_iperf_client` / `s_iperf_tcp_server` / `s_iperf_udp_server`）：
  监听句柄常驻，重复 arm 直接返回"已监听"；`eth istop` 统一 abort 三者。
- 验证：重复 arm + 真实 iperf 会话后 `MEM TCP_PCB used=0` 不增长、`HEAP` 保持 192 B；
  12 周期压缩耐久 `logs/soak-val3-20261125`。
- 报告工具同步改进：复位次数同时给出"合并 10 min 内重复复位的独立故障次数"；32 位字节计数回绕已修。
- 下一步：用 R10.1 固件再跑一次 15 h 全量（见第 49 章 §49.7.8 / §49.9.18）。
