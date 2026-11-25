# lan8651_plca_slave —— LAN8651 10BASE-T1S PLCA 跟随者（Slave / node 1）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

## 功能

把 PLCA 的 **跟随者** 角色做成独立例程：上电自动完成 LAN8651 bring-up（SPI2 若拿不到 echo 就自动降级到位翻转
传输），按 跟随者 角色配置 TC14 的 PLCA 寄存器（`node_id=1`），并**回读自证**：

- `PLCA STATUS` 的 **bit15（PST）= 1** → 已与协调者的 BEACON 同步（跟随者角色的判据）；
- 主循环每 1 ms 收包、每收到一帧自定义 EtherType（`0x88B5`）立即回一帧；
- 每 2 s 打印心跳：`[alive] ... PLCA en=.. node_id=.. PST=.. | rx=.. tx=.. replies=..`。

本例程与 `lan8651_plca_slave`/`lan8651_plca_master` 共用 `samples/lan8651/common/plca_demo.c`，只差角色常量。

## 接线

| 板端 | 说明 |
|---|---|
| SPI2 PA20/21/22/23 | LAN8651 SPI（Mode 0） |
| nRST PA19 / nINT PA24 | 复位 / 中断 |
| TRX_P/TRX_N（10BASE-T1S） | 接主机 USB-10BASE-T1S 适配器 `enx001ec0d1c337`，段两端 100 Ω 终端 |

## 主机侧配套命令（必须先执行）

主机适配器作为**协调者（node 0）**，它是 BEACON 的发出者。

```bash
sudo bash host/lan8651/t1s_host.sh plca0 8 32 0 128
sudo python3 host/lan8651/t1s_peer.py --loop --rate 4   # 可选：raw socket 对端收发
```

> 两端参数必须一致：`node_cnt=8 / to_tmr=32 / burst_cnt=0 / burst_tmr=128`；`node_id` 不能冲突
> （本例程固定 `node_id=1`，所以主机侧必须用另一侧的角色）。

## 板端命令

| 命令 | 作用 |
|---|---|
| `status` | 打印 PLCA 配置 + PST、PHY STS1、TC6 收发统计 |
| `send [n]` | 发 n 个 EtherType `0x88B5` 帧（已知对端则单播，否则广播） |
| `plca <on\|off> [id] [cnt] [to] [bc] [bt]` | 运行时重配 PLCA |
| `trans <bb\|hw>` | 切换 TC6 传输层（位翻转 / SPI2 控制器） |
| `levels` | nRST / nINT 电平 |

## 实测（板端串口日志）

```text
$ bash scripts/hwrun.sh lan8651/lan8651_plca_slave --secs 14 --send 'status;send 2;status' --no-build --lock-wait 900
[auto] HW SPI : read STATUS0 -> ret 0 ... echo OK -> using the SPI2 controller
[init] PLCA FOLLOWER (node 1): node_id=1 node_cnt=8 to_tmr=32 burst_cnt=0 burst_tmr=128
status
[status] PLCA en=1 node_id=1 node_cnt=8 to_tmr=32 burst_cnt=0 burst_tmr=128 status=0x8000 (PST=1)
[status] PHY STS1=0x0180 (bit11 PLCA change, bit13 link change)
[status] TC6 ctrl=96 err=0 | data=0 | tx_frames=0 tx_bytes=0 | rx_frames=0 rx_bytes=0
send 2
[tx] #0 len=58 ret=ok tx_credits=31
[tx] #1 len=58 ret=ok tx_credits=31
[rx] len=64 src=00:1e:c0:d1:c3:37 type=0x88b5
[rx] len=64 src=00:1e:c0:d1:c3:37 type=0x88b5
[rx] len=64 src=00:1e:c0:d1:c3:37 type=0x88b5
status
[status] PLCA en=1 node_id=1 ... status=0x8000 (PST=1)
[status] TC6 ctrl=96 err=0 | data=5 | tx_frames=5 tx_bytes=290 | rx_frames=3 rx_bytes=192 dropped=1 replies=3
[status] peer MAC 00:1e:c0:d1:c3:37
```

**结论**：跟随者角色下 PST=1（已同步到主机协调者的 BEACON），且与主机双向收发（发 5 帧 / 收 3 帧 / 应答 3 帧）正常。

日志：`logs/20261030-151459-lan8651_plca_slave.log`（硬件 SPI 传输，`ctrl err=0`）

## 关键点 / 易错点

1. **PST=1 才是"PLCA 真的在工作"**：只把寄存器写进去、回读配置正确并不等于同步上了；PST 位来自 BEACON 接收。
2. **两端参数必须完全一致**，且恰好一端 `node_id=0`；否则 PST 不置位、表现为"能收不能发"或丢帧。
3. **配置顺序**：关 PLCA → RST → 写参数 → 使能（`lan865x_plca_configure()` 已按此实现）；先使能再改 ID 会掉帧。
4. **接收帧带 4 B FCS**（实测 `len=64` = 60 + 4），接 lwIP 时要减 4；本例程只打印前几字节，不剥 FCS。
5. 位翻转传输下 `SCLK ≈1.4 MHz`，本例程的目标是"PLCA 角色可复现 + 自证"，吞吐见第 11 章（硬件 SPI 待修）。
