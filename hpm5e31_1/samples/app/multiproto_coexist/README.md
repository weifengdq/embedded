# multiproto_coexist —— 多协议 / 4 通道共存（U-39-1 / U-39-6）

> 源码仓库（公开镜像，暂定）：<https://github.com/weifengdq/embedded>；本书章节与实测日志见仓库 hpm5e31_1/docs/ 目录。

同一颗 HPM5E31（480 MHz）上**同时**运行 4 路 CAN、两类应用层协议和一个 cannelloni 网关：

| 通道 | 角色 | 协议/流量 | 引脚 |
|---|---|---|---|
| CAN0 | J1939 ECU（SA=0x00） | EEC1 10 ms、CCVS1 100 ms、ET1/DM1 1 s，500 kbit/s | PD00/PD01 |
| CAN1 | app_can_gw（cannelloni v2） | CAN1 ↔ UDP 4000 ↔ 主机 vcan0，全部 29 位帧原样搬运 | PD05/PD04 |
| CAN2 | CANopenNode v4 从站（节点 1） | 心跳 1 s + SYNC 型 TPDO（10 ms 一帧） | PC08/PA09 |
| CAN3 | app_canopen 最小主站 | SYNC 10 ms + SDO 读 0x2102（2 次/s）+ 心跳监视 | PC15/PC14 |

板上拓扑：**CAN0↔CAN1、CAN2↔CAN3 各是一段独立总线**（120 Ω 终端已接），所以本固件同时验证
"4 路通道并发"与"两段总线互不影响"；唯一的共享资源是 CPU、RAM 和 lwIP。

## 硬件与接线

- 不需要外部 CAN 分析仪：J1939 的"对端"是主机（经 CAN1 网关 / vcan0），CANopen 的主从都在本板。
- Bench B（主机侧）额外需要以太网：板 192.168.2.100/24 ↔ 主机 enp6s0 192.168.2.1/24。
  `host_benchb.sh` 会自动建 vcan0 + cannelloni 隧道。

## 命令

```bash
# 纯板内（Bench A）：上电自动 mix 15 s + mixj 8 s，串口打印两组 PASS/FAIL 与全部指标
bash scripts/hwrun.sh samples/app/multiproto_coexist --secs 32 --lock-wait 1800

# 带主机（Bench B）：串口抓完之后，在持锁窗口内跑 host_benchb.sh
bash scripts/hwrun.sh samples/app/multiproto_coexist --secs 32 --no-build \
     --post "timeout 80 bash samples/app/multiproto_coexist/host_benchb.sh" --lock-wait 1800

# shell
mix [secs]   4 协议/4 通道全开（默认 10 s，≤40 s）
mixj [secs]  只跑 J1939 + 网关（CANopen 任务暂停），用于对比 CPU 争用
quad [secs]  同 mix，额外强调 4 通道/串扰/丢帧对账
stat / info
```

## 指标与判据

| 指标 | 采集方式 | 预期 |
|---|---|---|
| CPU 负载 | 主循环空闲计数 loops/s、单次循环最大耗时 | 见实测表 |
| J1939 周期抖动 | EEC1 填充回调记录 mcycle 时间戳，算间隔 min/avg/max | 10 ms ± 数 ms |
| CANopen 调度抖动 | CAN3 SYNC 发送时刻间隔 min/avg/max | 10 ms ± 数 ms |
| CANopen SDO 时延 | 主站 SDO 读 0x2102（2 Hz）计时 | < 5 ms |
| 网关丢包 | `app_can_gw_stats()` drop_can/drop_udp/bad_pkt | 0 |
| 4 通道错误 | MCAN PSR/ECR（LEC/TEC/REC/bus-off） | 全 0 |
| 跨通道对账 | node_tx vs master_rx、master_tx vs node_rx | 差 ≈ 0 |
| RAM | 链接输出 DLM/AXI/AHB + 运行时 sizeof/CANopen heap | 见实测 |

## 实测（2026-11-02，500 kbit/s，-O3/XIP）

**资源**：FLASH 171 760 B（16.38%）、**DLM 95 168 B（72.61%）**、AXI SRAM 62 336 B（31.71%）、
AHB SRAM 17 920 B（54.69%）、ILM 1 544 B。运行时：j1939_stack 9 760 B、CANopen heap 2 876 B。

**Bench A**（`logs/20261102-102105-multiproto_coexist.log`，上电自动 mix 15 s + mixj 8 s）：

| 指标 | mix（4 通道全开） | mixj（只 J1939+网关） |
|---|---|---|
| 主循环空闲计数 | 155 575 次/s | 277 707 次/s（-44% 对比） |
| 单次循环最大耗时 | 1 029 µs | 594 µs |
| J1939 EEC1 | 1 500 帧 / gap 9 068/9 999/10 053 µs | 800 帧 / 9 409/9 999/10 004 µs |
| CANopen SYNC | 1 500 帧 / avg/max 10 000/12 231 µs | 暂停 |
| CANopen SDO 0x2102 | 30/30 成功，avg 490 µs | — |
| CANopen 主站侧观测 | master_rx 1 559、hb 14 | — |
| 网关 | CAN1->UDP 1 680 帧，drop_can=0，bad=0 | 896 帧，drop_can=0 |
| 4 路 MCAN 错误 | CAN0~3 全部 LEC/TEC/REC/bus-off=0 | 全 0 |

**Bench B**（`logs/20261102-102326-multiproto_coexist-host.log`，`host_benchb.sh`）：

- 主机 vcan0 8 s 收 896 帧：EEC1 800（avg 10.013 ms，max 20.06 ms 为 cannelloni 批量）、
  DM1/ET1 各 8、CCVS1 80；DM1 解出 lamp=0x10(AWL)/SPN=100/FMI=4/OC=1，**BENCHB RESULT: PASS**。
- 主机注入 402 帧 11 位帧：网关 UDP->CAN 402 帧、drop_can=0、bad_pkt=0；CAN0 J1939 bus.dropped=402
  （跨协议帧按 29 位/11 位类型隔离，不进 J1939 协议栈）；注入期间单次循环 max 5 062 µs，J1939 无丢帧。

**结论**：4 路 CAN + 两类协议 + 网关三合一后 DLM 仍 72.6%，J1939 10 ms 周期最大 10.053 ms
（比单跑多 49 µs）；CPU 空闲率下降 44%，是共存的主要代价。

## 易错点

- 四个通道必须由**各自组件**初始化：CAN0 用 app_j1939 的端口层（自带 mcan_init），CAN1 用 app_can
  （网关内部初始化），CAN2 用 CANopenNode 的 CO_driver，CAN3 用 co_master；任何两处重复 mcan_init
  同一实例都会把对方配置冲掉。
- app_can 只提供 500 kbit/s / FD 两档，所以本固件统一 500 kbit/s（J1939 标准是 250 kbit/s，见第 31 章）。
- CAN1 网关会把 CAN0/CAN1 总线上的**所有**帧搬到 UDP（这就是 cannelloni 的语义），所以 vcan0 上
  看到 29 位 J1939 帧属正常；反向主机注入的 11 位帧会出现在 CAN0 总线上（J1939 栈计入 bus.dropped）。
- mix 期间不要用 printf 打断测量（本固件只在每组结束时打印一次）。