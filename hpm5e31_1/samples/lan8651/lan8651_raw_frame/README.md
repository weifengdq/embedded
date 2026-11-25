# lan8651_raw_frame —— 10BASE-T1S 裸以太网帧双向收发

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

不经过 IP 协议栈，直接用 LAN8651 + TC6 数据通道收发**自定义 EtherType（0x88B5）**的以太网帧，与主机
USB-10BASE-T1S 适配器对联，用于验证：

- TC6 数据块（68 B：4 B 头 + 64 B 负载）与 TX credits 流程；
- 接收帧重组（START_VALID / END_VALID / START_WORD_OFFSET / END_BYTE_OFFSET）；
- MAC 过滤（promiscuous / 单播）与 MAC 地址配置；
- PLCA 开关（默认 CSMA/CD，可用 `plca on ...` 打开）。

## 接线

| 板端 | 说明 |
|---|---|
| SPI2 PA20/21/22/23 | LAN8651 SPI（Mode 0） |
| nRST PA19 / nINT PA24 | 复位 / 中断 |
| TRX_P/TRX_N | 接主机 USB-10BASE-T1S 适配器 `enx001ec0d1c337`，段两端 100 Ω 终端 |

主机侧配置（需要 sudo）：

```bash
sudo bash host/lan8651/t1s_host.sh csma      # IP 192.168.1.1/24, PLCA off
sudo python3 host/lan8651/t1s_peer.py --loop --rate 5   # raw socket 收发
```

## 命令

| 命令 | 作用 |
|---|---|
| `send [n]` | 发 n 帧自定义 EtherType（对端已知则单播，否则广播） |
| `poll` | 手动服务一次接收路径并打印收到的帧 |
| `stats` | 打印 TC6 数据通道统计（tx/rx 帧数、credits、错误位） |
| `plca <on\|off> [id] [cnt] [to] [bc] [bt]` | PLCA 配置（默认 8/32/0/128） |
| `trans <bb\|hw>` | 切换 TC6 传输层 |

主循环每 1 ms 调一次 `oa_tc6_poll_rx()`（只在 `rx_chunks_available > 0` 时才真正发 SPI 事务），
所以空闲时不占 SPI 带宽；收到帧立即打印并回一帧。

## 吞吐实测（硬件 SPI，2026-10-30）

`bench <帧数> [帧长]` 连续发包并用内核周期计数器计时（不含线上 PLCA 仲裁时间）：

| SCLK | 帧长 | 帧率 | 有效载荷 | 日志 |
|---|---|---|---|---|
| 10 MHz | 60 B | 15 849 帧/s | 7.61 Mbit/s | `logs/20261030-150912-lan8651_raw_frame.log` |
| 10 MHz | 1518 B | 748 帧/s | **9.08 Mbit/s** | 同上 |
| **20 MHz** | 60 B | **18 489 帧/s** | **8.87 Mbit/s** | `logs/20261030-151134-lan8651_raw_frame.log` |

硬件 SPI 修复前（位翻转 ≈1.4 MHz）1518 B 帧只有约 0.7 Mbit/s，**吞吐提升约 13 倍**，见第 10 章"硬件 SPI2 修复"四步。

## 实测现象（2026-10-30）

板端日志 `logs/20261030-114617-lan8651_raw_frame.log`；主机侧 `/tmp/t1s_peer.log`：

```text
[auto] HW SPI : read STATUS0 -> ret 0 ... echo OK -> using the SPI2 controller
[init] LAN8651: ok (0), SPI2 SCLK 10000000 Hz
[init] PLCA off (CSMA/CD), node_cnt=8 to_tmr=32
[init] local MAC 02:00:00:5e:31:01

send 5
[tx] #0 len=58 ret=ok tx_credits=31
[rx] len=64 dst=ff:ff:ff:ff:ff:ff src=00:1e:c0:d1:c3:37 type=0x88b5
      payload: 11 22 bf 33 00 01 02 03 ...
      replied with 58 bytes (total replies 1)
...
[stats] data xfers=7 | tx_frames=7 tx_bytes=406 | rx_frames=2 rx_bytes=128 dropped=1
```

主机侧（raw socket）：

```text
[host rx] len=60 src=02:00:00:5e:31:01 payload=a55a0019000102030405
[host rx] len=60 src=02:00:00:5e:31:01 payload=a55a011b010203040506
... 共 7 帧
```

**结论**：板 → 主机 7 帧、主机 → 板 2 帧、板端逐帧应答，双向收发全部成功。

## PLCA 主从实测（2026-10-30）

| 角色 | 主机侧 | 板端命令 | 板端回读 | 帧收发 |
|---|---|---|---|---|
| 板 = 跟随者 | `t1s_host.sh plca0`（node-id 0，协调者） | `plca on 1 8 32 0 128` | `status=0x8000 (PST=1)` | 收 3 / 发 5 |
| 板 = 协调者 | `t1s_host.sh plca1`（node-id 1，跟随者） | `plca on 0 8 32 0 128` | `status=0x8000 (PST=1)` | 收 4 / 发 6 |

日志：`logs/20261030-115606-lan8651_raw_frame.log`（跟随者）、`logs/20261030-120336-lan8651_raw_frame.log`（协调者）。
**PST=1 且双向帧收发正常**，说明 PLCA 真的在工作（而不只是寄存器写进去了）。

### 如何复现（两条命令，主机侧 + 板端各一条）

参数固定为 `node_cnt=8 / to_tmr=32 / burst_cnt=0 / burst_tmr=128`（两端必须一致）。

**场景 A：主机当协调者（节点 0），板端当跟随者（节点 1）**

```bash
# 主机（在 <user>@<host> 上执行，需要 sudo）
sudo bash host/lan8651/t1s_host.sh plca0 8 32 0 128     # 期望 Status: on
```

```text
# 板端串口（letter-shell）
plca on 1 8 32 0 128
# 期望回读: PLCA: en=1 node_id=1 node_cnt=8 to_tmr=32 burst_cnt=0 burst_tmr=128 status=0x8000 (PST=1)
send 2        # 发两帧并观察收到的对端帧（每收到一帧会自动回一帧）
stats
```

**场景 B：板端当协调者（节点 0），主机当跟随者（节点 1）**

```bash
# 主机
sudo bash host/lan8651/t1s_host.sh plca1 8 32 0 128
```

```text
# 板端串口
plca on 0 8 32 0 128
# 期望回读: PLCA: en=1 node_id=0 node_cnt=8 to_tmr=32 burst_cnt=0 burst_tmr=128 status=0x8000 (PST=1)
send 2
stats
```

判据：板端 `status` 的 **bit15（PST）= 1** 且主机 `ethtool --get-plca-status` 显示 `Status: on`，同时 `send`/`stats` 能看到双向帧计数都增长；恢复 CSMA 只需 `plca off` + `t1s_host.sh csma`。

## 关键点 / 易错点

1. **接收帧带 FCS**：LAN8651 交给主机的接收帧包含 4 B FCS（实测 `len=64` = 60 B 最小帧 + 4 B FCS）。接 lwIP 时要在 `low_level_input` 里减掉 4 B。
2. **接收队列只有 2 槽**：一次连发多帧时会 `dropped`，示例里刻意保持最小实现；产品代码应按 MTU 扩队列或在中途服务。
3. **自己发的帧可能被自己收到**：10BASE-T1S 半双工 + 广播时尤其明显，示例按"源 MAC ≠ 本机 MAC"过滤。
4. **不要每帧一次 SPI 事务**：TC6 一个事务可以塞多个 68 B 块，按 `tx_credits` 批量发送才能提速。
5. **PLCA 参数两端必须一致**，且恰好一端 `node-id 0`；否则 `PST` 位不会置起、链路表现为"能收不能发"或丢帧。
