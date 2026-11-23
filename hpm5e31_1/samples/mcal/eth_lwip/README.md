# eth_lwip —— AUTOSAR MCAL Eth + lwIP 2.1.2（裸机 NO_SYS=1）ping / iperf2 实测

把 lwIP 的 netif 架在 **Eth MCAL 驱动**（`mcal/Eth` + `mcal/EthTrcv`）之上，验证
"AUTOSAR MCAL 驱动的以太网能不能跑协议栈"。这是第 49 章 `§49.6 lwIP 移植` 的配套例程。

- 适配层：`components/app_lwip_eth/`（netif + MCAL 后端 + iperf/ping/命令）
- 硬件：hpm5e31_1，ENET0(RGMII) + YT8011AN 1000BASE-T1，经转换盒接主机 `enp6s0`
- 网络：板 `192.168.2.100/24`，主机 `192.168.2.1/24`（网关同主机）
- 协议：lwIP 自带 `lwiperf` 是 **iperf2**（端口 5001），主机请用 `iperf`（v2），**不是 iperf3**

## 1 构建与烧写

```bash
cd hpm5e31_1
bash scripts/build.sh samples/mcal/eth_lwip
bash scripts/hwrun.sh samples/mcal/eth_lwip --secs 20 --lock-wait 1800
```

上电会打印初始化序列（顺序不能变：`Eth_Init → EthTrcv_Init → Eth_SetControllerMode(ACTIVE)`）：

```
=== mcal_eth_lwip: AUTOSAR MCAL Eth + lwIP 2.1.2 (baremetal NO_SYS=1) ===
[init] Eth MCAL v1.0.0 (module 88)
[init] Eth controller 0 ACTIVE
[lwip-eth] backend=mcal-eth ctrl=0 max_frame=1518 tx_buf=8 rx_buf=16
[lwip-eth] netif=e0 idx=1 MAC=02:00:00:00:01:00
[lwip-eth] IP=192.168.2.100
[lwip-eth] GW=192.168.2.1  link=UP
[lwip-eth] phy: link=UP known=1 speed=1000 Mbps duplex=full role=SLAVE
[mcal-eth] PHY BMCR=0140 BMSR=000D GBSR=3000 PHYSR=8400
[lwip-eth] link=UP after 1004 ms
[init] app_lwip_eth_init rc=0 (0=link up, 1=link down)
[selftest] ping gw: ...
```

## 2 串口命令

| 命令 | 作用 |
|---|---|
| `eth` | 打印 netif/MAC/IP/PHY 寄存器/软件与驱动统计 |
| `eth stat` | 只打印统计（含 loop/tx/rx 耗时画像） |
| `eth ping <ip> [n] [ms]` | ICMP echo，输出每次 RTT 与汇总（min/avg/max/jitter） |
| `eth iserver [tcp\|udp]` | 启动 iperf2 服务端（端口 5001） |
| `eth iclient <ip> [tcp\|udp]` | 启动 iperf2 客户端（10 s） |
| `eth istop` | 中止会话 |
| `sleep <ms>` | 延时（内部继续轮询 lwIP，脚本化测试用） |

打流示例（板作 server，主机作 client，靠 `--post` 在持锁期间跑上位机）：

```bash
bash scripts/hwrun.sh samples/mcal/eth_lwip --secs 10 --send "eth iserver" \
  --post "sleep 1; iperf -c 192.168.2.100 -t 10 -i 5 -f m"
```

板作 client（先起主机 server）：

```bash
iperf -s -u -t 30 &
bash scripts/hwrun.sh samples/mcal/eth_lwip --secs 20 --send "eth iclient 192.168.2.1 udp"
```

## 3 预期输出

- 初始化：`link=UP`、`speed=1000 Mbps`、`PHYSR=8400`（1000M/Slave/实时链路）
- `eth ping`：每包打印 `[ping] seq=N 192.168.2.1: rtt=XXX us`，末尾汇总
- `eth iclient ... udp`：lwiperf 打印 `Ideal frame delay` / `Send 90 frame(s) once per 1000 us`，
  10 s 后打印 `[iperf] type=7 ... => XXX kbit/s`
- 每 10 s 自动打印一次统计（打流时建议关掉高频打印，见"坑"）

## 4 实测数据（真实日志，2026-11-02）

表 1 ICMP echo（板 → 主机 192.168.2.1，**R8 修复后**）

| 场景 | 发出 | 收到 | 丢包 | RTT min/avg/max (µs) | 抖动 (µs) | 日志 |
|---|---|---|---|---|---|---|
| 上电自证 | 5 | **5** | 0% | 204/309/468 | 98 | `logs/20261102-182016-eth_lwip.log` |
| 命令 20 次 @100 ms | 20 | **19** | 5% | 97/301/391 | 49 | `logs/20261102-182016-eth_lwip.log` |
| 修复前（对照） | 5 | 2 | 60% | 189/254/319 | 130 | `logs/20261102-162802-eth_lwip.log` |

表 2 iperf2 吞吐（**R8 修复后**；修复前 TCP 双向只有 0.02~0.08 Mbit/s）

| 方向 | 协议 | 板侧 | 主机侧 | 日志 |
|---|---|---|---|---|
| 主机→板 | TCP（板 server） | — | **251~253 Mbit/s**（5 轮里 3 轮有效，251/251/252） | `logs/20261102-183321-t1-rounds.summary` |
| 板→主机 | TCP（板 client） | **169~261 Mbit/s**（3 轮）/ 4~8 Mbit/s（2 轮，双峰） | 169/179/261 Mbit/s | 同上，`-b2h-host-*.log` |
| 主机→板 | UDP 900 Mbit/s 打流 | — | 1125 MB/10 s = 944 Mbit/s（已发出） | `logs/20261102-163638-eth_lwip.log` |
| 板→主机 | UDP（lwiperf 1000M 档） | 120 789 144 B/10.5 s = 92.0 Mbit/s | 未收齐 | `logs/20261102-163313-eth_lwip.log` |

耗时画像（`eth stat`）：`tx_max=36 µs`（单帧 linkoutput 全路径）、`rx_drain_max=89 µs`、
`link poll max=105 µs`、`loop gap_max=3.3 ms`（空闲）/9.6 ms（打流 + printf 时）。

对照第 13 章（SDK enet 驱动 + 同一套 lwIP 内存预算）：下行 935 Mbit/s、TCP 上行 466 Mbit/s、
UDP 上行 641 Mbit/s。**当前 MCAL 版本 TCP 与 ping 都明显偏差，见第 5 节。**

## 5 坑（都是本机实测踩出来的）

1. **EthTrcv_Init 之前读 MDIO 全是 0**：`Eth_Init` 里才做板级（RGMII 引脚/时钟/PHY 硬复位），
   在那之前 `Eth_ReadMii` 读回 `BMCR/BMSR/GBSR/PHYSR = 0000`，链路永远是 DOWN。
   正确顺序：`Eth_Init(&Eth_Config) → EthTrcv_Init(&EthTrcv_Config) → Eth_SetControllerMode(0, ETH_MODE_ACTIVE)`。
   日志对照：`logs/20261102-160239-eth_lwip.log`（顺序错，PHY 全 0）与
   `logs/20261102-162802-eth_lwip.log`（顺序对，PHYSR=8400）。
2. **不要自己配 PHY**：RGMII 延迟 / 主从 / 速率都在 `EthTrcv_Init` 里（YT8011AN EXT 0x9001、
   MMD1 0x834），netif 层再写一遍会覆盖它，链路要重新训练 1~2 s。
3. **`Eth_ReadMii` 的 TrcvIdx 是"收发器索引"（0），不是 PHY 地址（1）**，传 1 读回来全是 0。
4. **lwiperf 的 UDP 客户端必须每轮轮询** `lwiperf_poll_udp_client()`，否则只发头几帧就停
   （日志里表现为上行只有几 KB/s）。
5. **打流时不要开高频 printf**：921600 波特率下 300 字符约 3 ms，主循环被顶住 9.6 ms，
   这段时间的帧只能靠 16 个 Rx 缓冲兜，直接掉包。观测请用 `eth stat` 单次打印。
6. **TCP 停摆的根因不在 lwIP，在 TX 描述符发放顺序（R8 已修）**：`Eth_ProvideTxBuffer` 原来按
   "下标最小的空闲缓冲"发放，而 `Eth_TxConfirmation` 会把 DMA **已经走过**的描述符标回空闲；一旦新帧被塞到
   DMA 身后的描述符，发送 DMA 就停在原地（它只能沿链顺序前进，`DMA_TX_POLL_DEMAND` 唤醒后仍从原位置取），
   已提交的帧全部积压，直到整环 8 个缓冲填满才在同一微秒内一起喷出。线上表现就是 ping 只有 2/5、
   TCP 的 SYN-ACK 攒 17 s 后重发 8 份。修法：驱动加 `TxNext` 游标，只发放游标指向的缓冲（与 SDK 的
   `tx_desc_list_cur` 一致）。细节见第 49 章 §49.9.12 / §49.8.10。
7. **ACK 方向的 TX 缓冲回收**：TCP 的 ACK 在 `netif->input()` 里同步产生，而 `Eth_TxConfirmation`
   只在主循环的 `Eth_MainFunction()` 里跑；8 个 TX 缓冲被 ACK 占满后其余 ACK 被丢，TCP 只能等
   200~500 ms 延迟 ACK。修法：`linkoutput` 拿不到缓冲时就地跑一次 `Eth_MainFunction()` 再重试一次
   （`tx recycle: retry_ok` 计数可观测）。见 §49.9.13。
8. **RX drain 必须有上限**：一次 `app_lwip_eth_poll()` 原来会一直收到环空，1 Gbit/s 打流时能连收 2.4 s，
   期间 `sys_check_timeouts()` 与 shell 全被饿死。现在单次上限 32 帧（`APP_LWIP_ETH_RX_DRAIN_MAX`），
   实测 `rx_drain_max` 从 2 408 360 µs 降到 1 303~1 610 µs。见 §49.9.14。
9. **`--send` 多条命令会丢**：`hwrun.sh --send` 在复位后 0.3 s 一次性写串口，此时板子还在跑上电自证
   （约 4 s 不读串口），FIFO 装不下的命令会丢。脚本化测试建议一条命令，或把命令写成板端的单个 shell 命令。

## 6 复跑命令汇总

```bash
# 链路 / PHY / 统计
bash scripts/hwrun.sh samples/mcal/eth_lwip --secs 12 --send "eth"
# ping（15 次 / 100 ms）
bash scripts/hwrun.sh samples/mcal/eth_lwip --secs 20 --send "eth ping 192.168.2.1 15 100"
# UDP 上行（主机先起 server）
iperf -s -u -t 30 & bash scripts/hwrun.sh samples/mcal/eth_lwip --secs 18 --send "eth iclient 192.168.2.1 udp"
# TCP 主机→板（板 server；iperf 放在持锁窗口内跑，才能同时看到板端统计）
( sleep 12; iperf -c 192.168.2.100 -t 6 -i 1 ) &
bash scripts/hwrun.sh samples/mcal/eth_lwip --secs 24 --no-build --send "eth iserver tcp"
# TCP 板→主机（先起主机 server）
iperf -s -i 1 &
bash scripts/hwrun.sh samples/mcal/eth_lwip --secs 30 --no-build   --send "sleep 5000;eth iclient 192.168.2.1 tcp;sleep 14000"
```

## 7 关键配置

| 项 | 值 | 出处 |
|---|---|---|
| lwIP 内存预算 | `MEM_SIZE=24K`、`PBUF_POOL_SIZE=16`、`TCP_SND_BUF/WND=8×MSS` | `inc/lwipopts_app.h`（与第 13 章基线一致） |
| Rx 零拷贝 pbuf 池 | 32（>= 16 个 Rx 缓冲的 2 倍） | `APP_LWIP_ETH_RX_PBUF_NUM` |
| 单帧上限 | 1518（含 14 B 头，不含 FCS） | `Eth.h` 的 `ETH_MAX_FRAME_LEN` |
| 校验和 | 软件计算（未定义 `CHECKSUM_BY_HARDWARE`） | MCAL Eth 无校验和卸载 API |
| 以太网时钟/引脚 | 由 `Eth_Init` 内部板级钩子配置 | `mcal/Eth/cfg/Eth_PBcfg.c` |
